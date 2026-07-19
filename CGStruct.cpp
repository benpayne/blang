#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

#include <iostream>

using namespace QLang;
using namespace std;

Type *CodeGen::resolveVariantPayloadType( Type *assocType, EnumDefinition *enumDef,
	Type *concreteEnumType )
{
	if ( assocType == nullptr || enumDef == nullptr || concreteEnumType == nullptr )
		return assocType;
	const auto &gps = enumDef->getGenericParams();
	for ( size_t i = 0; i < gps.size(); i++ )
	{
		if ( gps[i].mName == assocType->getName() &&
			 (int)i < concreteEnumType->getNumTypeParams() )
			return concreteEnumType->getTypeParam( (int)i );
	}
	return assocType;
}

void CodeGen::emitEnumPayloadRelease( llvm::AllocaInst *alloca, EnumDefinition *enumDef,
	Type *concreteEnumType )
{
	if ( enumDef == nullptr )
		return;

	llvm::StructType *enumType = getOrCreateEnumType( enumDef );
	llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );

	// Load the enum value from the alloca
	llvm::Value *enumVal = mBuilder->CreateLoad( enumType, alloca, "enum.cleanup" );
	llvm::AllocaInst *enumTmp = mBuilder->CreateAlloca( enumType, nullptr, "enum.cleanup.tmp" );
	mBuilder->CreateStore( enumVal, enumTmp );

	// Load the tag
	llvm::Value *tagPtr = mBuilder->CreateStructGEP( enumType, enumTmp, 0, "enum.cleanup.tag.ptr" );
	llvm::Value *tag = mBuilder->CreateLoad(
		llvm::Type::getInt32Ty( *mContext ), tagPtr, "enum.cleanup.tag" );

	// Create basic blocks for the switch
	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "enum.cleanup.done", func );

	// Build switch for variants with refcounted payloads
	llvm::SwitchInst *sw = mBuilder->CreateSwitch( tag, mergeBB, enumDef->mVariants.size() );

	for ( size_t vi = 0; vi < enumDef->mVariants.size(); vi++ )
	{
		auto &variant = enumDef->mVariants[vi];
		bool hasRef = false;
		for ( auto &at : variant.mAssociatedTypes )
		{
			string atn = resolveVariantPayloadType(
				(Type *)at, enumDef, concreteEnumType )->getName();
			if ( atn == "string" || atn == "Array" || atn == "Buffer" ||
				 isUserStructType( atn ) )
			{
				hasRef = true;
				break;
			}
		}
		if ( !hasRef )
			continue;

		llvm::BasicBlock *variantBB = llvm::BasicBlock::Create(
			*mContext, "enum.cleanup." + variant.mName, func );
		sw->addCase(
			llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), vi ),
			variantBB );

		mBuilder->SetInsertPoint( variantBB );

		// Get payload pointer
		llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
			enumType, enumTmp, 1, "enum.cleanup.payload" );
		llvm::Value *payloadBytePtr = mBuilder->CreateConstInBoundsGEP2_32(
			llvm::ArrayType::get( llvm::Type::getInt8Ty( *mContext ),
				enumType->getElementType( 1 )->getArrayNumElements() ),
			payloadPtr, 0, 0, "enum.cleanup.payload.byte" );

		// Release each refcounted payload field
		for ( auto &at : variant.mAssociatedTypes )
		{
			string atn = resolveVariantPayloadType(
				(Type *)at, enumDef, concreteEnumType )->getName();
			if ( atn == "string" )
			{
				llvm::Value *strVal = mBuilder->CreateLoad(
					ptrType, payloadBytePtr, "enum.cleanup.str" );
				mBuilder->CreateCall( getOrDeclareStringRelease(), { strVal } );
			}
			else if ( atn == "Array" )
			{
				llvm::Value *arrVal = mBuilder->CreateLoad(
					ptrType, payloadBytePtr, "enum.cleanup.arr" );
				mBuilder->CreateCall( getOrDeclareArrayRelease(), { arrVal } );
			}
			else if ( atn == "Buffer" )
			{
				llvm::Value *bufVal = mBuilder->CreateLoad(
					ptrType, payloadBytePtr, "enum.cleanup.buf" );
				mBuilder->CreateCall( getOrDeclareBufferRelease(), { bufVal } );
			}
			else if ( isUserStructType( atn ) )
			{
				llvm::Value *structVal = mBuilder->CreateLoad(
					ptrType, payloadBytePtr, "enum.cleanup.struct" );
				mBuilder->CreateCall( getOrDeclareRcRelease(), { structVal } );
			}
		}

		mBuilder->CreateBr( mergeBB );
	}

	mBuilder->SetInsertPoint( mergeBB );
}

llvm::Function *CodeGen::getOrGenStructDestructor( StructDefinition *sd,
	const std::map<std::string, std::string> &typeSub )
{
	if ( sd == nullptr )
		return nullptr;

	// Determine the mangled name for this destructor (include type args for generics)
	string dtorName = "__" + sd->getName();
	if ( !typeSub.empty() )
	{
		for ( auto &gp : sd->mGenericParams )
		{
			auto it2 = typeSub.find( gp.mName );
			if ( it2 != typeSub.end() )
				dtorName += "_" + it2->second;
		}
	}
	dtorName += "_dtor";

	// Check cache
	auto it = mStructDtorMap.find( dtorName );
	if ( it != mStructDtorMap.end() )
		return it->second;

	// Check if the struct has any refcounted fields that need cleanup
	// For generic structs, look up the mangled instantiation name (e.g., "Box_string")
	llvm::StructType *structType = nullptr;
	string structTypeName = sd->getName();
	if ( !typeSub.empty() )
	{
		for ( auto &gp : sd->mGenericParams )
		{
			auto it2 = typeSub.find( gp.mName );
			if ( it2 != typeSub.end() )
				structTypeName += "_" + it2->second;
		}
	}
	auto stIt = mStructTypeMap.find( structTypeName );
	if ( stIt != mStructTypeMap.end() )
		structType = stIt->second;
	else
		structType = getOrCreateStructType( sd );

	const auto &fields = sd->getFields();
	bool hasRefField = false;
	for ( auto &f : fields )
	{
		if ( f->getVariableType() == nullptr )
			continue;
		string fName = f->getVariableType()->getName();
		auto subIt = typeSub.find( fName );
		if ( subIt != typeSub.end() )
			fName = subIt->second;
		if ( fName == "string" || fName == "Array" || fName == "Buffer" ||
			 f->getVariableType()->isFunctionType() ||
			 isUserStructType( fName ) )
		{
			hasRefField = true;
			break;
		}
	}

	if ( !hasRefField )
	{
		mStructDtorMap[dtorName] = nullptr;
		return nullptr;
	}

	// Generate the destructor function: void __StructName_dtor(void *ptr)
	llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *dtorFT = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrType }, false );

	// Check if already declared (e.g., from combine mode)
	llvm::Function *dtorFn = mModule->getFunction( dtorName );
	if ( dtorFn == nullptr )
	{
		dtorFn = llvm::Function::Create(
			dtorFT, llvm::Function::InternalLinkage, dtorName, mModule.get() );
	}

	// Save and restore builder state
	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	llvm::BasicBlock::iterator savedPt;
	bool hadInsertPoint = ( savedBB != nullptr );
	if ( hadInsertPoint )
		savedPt = mBuilder->GetInsertPoint();

	// Create the destructor body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", dtorFn );
	mBuilder->SetInsertPoint( entryBB );

	llvm::Value *selfPtr = dtorFn->getArg( 0 );

	// Release each refcounted field
	for ( size_t fi = 0; fi < fields.size(); fi++ )
	{
		if ( fields[fi]->getVariableType() == nullptr )
			continue;
		string fieldTypeName = fields[fi]->getVariableType()->getName();
		auto subIt = typeSub.find( fieldTypeName );
		if ( subIt != typeSub.end() )
			fieldTypeName = subIt->second;

		if ( fieldTypeName == "string" )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				structType, selfPtr, fi, "dtor.str.ptr" );
			llvm::Value *strVal = mBuilder->CreateLoad( ptrType, fieldPtr, "dtor.str" );
			mBuilder->CreateCall( getOrDeclareStringRelease(), { strVal } );
		}
		else if ( fieldTypeName == "Array" )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				structType, selfPtr, fi, "dtor.arr.ptr" );
			llvm::Value *arrVal = mBuilder->CreateLoad( ptrType, fieldPtr, "dtor.arr" );
			mBuilder->CreateCall( getOrDeclareArrayRelease(), { arrVal } );
		}
		else if ( fieldTypeName == "Buffer" )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				structType, selfPtr, fi, "dtor.buf.ptr" );
			llvm::Value *bufVal = mBuilder->CreateLoad( ptrType, fieldPtr, "dtor.buf" );
			mBuilder->CreateCall( getOrDeclareBufferRelease(), { bufVal } );
		}
		else if ( isUserStructType( fieldTypeName ) )
		{
			// Nested struct: release via __blang_rc_release (its own destructor runs)
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				structType, selfPtr, fi, "dtor.struct.ptr" );
			llvm::Value *structVal = mBuilder->CreateLoad( ptrType, fieldPtr, "dtor.struct" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { structVal } );
		}
		else if ( fields[fi]->getVariableType()->isFunctionType() )
		{
			// Release fn-typed field's lambda context
			llvm::Type *pairType = llvm::StructType::get( *mContext, { ptrType, ptrType } );
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				structType, selfPtr, fi, "dtor.fn.ptr" );
			llvm::Value *pairVal = mBuilder->CreateLoad(
				pairType, fieldPtr, "dtor.fn.pair" );
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
				pairVal, 1, "dtor.fn.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRelease(), { ctxPtr } );
		}
	}

	mBuilder->CreateRetVoid();

	// Restore builder state
	if ( hadInsertPoint )
		mBuilder->SetInsertPoint( savedBB, savedPt );
	else if ( savedBB != nullptr )
		mBuilder->SetInsertPoint( savedBB );

	mStructDtorMap[dtorName] = dtorFn;
	return dtorFn;
}

llvm::Function *CodeGen::getOrDeclareRcAllocDtor()
{
	llvm::Function *fn = mModule->getFunction( "__blang_rc_alloc_dtor" );
	if ( fn != nullptr )
		return fn;

	llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
	// void *__blang_rc_alloc_dtor(size_t data_size, void (*dtor)(void*))
	llvm::FunctionType *ft = llvm::FunctionType::get(
		ptrType,
		{ llvm::Type::getInt64Ty( *mContext ), ptrType },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_rc_alloc_dtor", mModule.get() );
}

llvm::Value *CodeGen::genStructLiteral( StructLiteralExpression *expr )
{
	auto defIt = mStructDefMap.find( expr->mTypeName );
	if ( defIt == mStructDefMap.end() )
		return nullptr;

	StructDefinition *structDef = defIt->second;
	llvm::StructType *structType = nullptr;

	// Handle generic struct instantiation
	std::map<std::string, Type*> savedSub = mTypeSubstitution;
	if ( !expr->mTypeArgs.empty() && structDef->isGeneric() )
	{
		structType = instantiateGenericStruct( structDef, expr->mTypeArgs );
		// Re-establish substitution map for field value generation
		for ( size_t i = 0; i < structDef->mGenericParams.size() && i < expr->mTypeArgs.size(); i++ )
		{
			SmartPtr<Type> arg = expr->mTypeArgs[i];
			mTypeSubstitution[structDef->mGenericParams[i].mName] = (Type*)arg;
		}
	}
	else
	{
		structType = getOrCreateStructType( structDef );
	}

	// Heap-allocate the struct via ARC with a destructor for refcounted field cleanup
	llvm::DataLayout dl( mModule.get() );
	uint64_t dataSize = dl.getTypeAllocSize( structType );
	llvm::Value *sizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), dataSize );

	// Build type substitution map for destructor generation
	std::map<std::string, std::string> dtorSub;
	if ( !expr->mTypeArgs.empty() && structDef->isGeneric() )
	{
		for ( size_t i = 0; i < structDef->mGenericParams.size() && i < expr->mTypeArgs.size(); i++ )
		{
			SmartPtr<Type> arg = expr->mTypeArgs[i];
			string resolvedName = arg->getName();
			auto sIt2 = mTypeSubstitution.find( resolvedName );
			if ( sIt2 != mTypeSubstitution.end() )
				resolvedName = sIt2->second->getName();
			dtorSub[structDef->mGenericParams[i].mName] = resolvedName;
		}
	}

	llvm::Function *dtorFn = getOrGenStructDestructor( structDef, dtorSub );
	llvm::Value *heapPtr = nullptr;
	if ( dtorFn != nullptr )
	{
		heapPtr = mBuilder->CreateCall(
			getOrDeclareRcAllocDtor(), { sizeVal, dtorFn }, "struct.ptr" );
	}
	else
	{
		heapPtr = mBuilder->CreateCall(
			getOrDeclareRcAlloc(), { sizeVal }, "struct.ptr" );
	}

	// Store each field value
	for ( size_t i = 0; i < expr->mFieldNames.size(); i++ )
	{
		// Find the field index in the struct definition
		int fieldIdx = -1;
		for ( size_t f = 0; f < structDef->mFields.size(); f++ )
		{
			if ( structDef->mFields[f]->getName() == expr->mFieldNames[i] )
			{
				fieldIdx = static_cast<int>( f );
				break;
			}
		}

		if ( fieldIdx < 0 )
			continue;

		llvm::Value *fieldVal = nullptr;

		// For empty array literals assigned to Array<T> fields, use the correct
		// element size from the field's type parameter instead of the default 4
		auto *arrLit = dynamic_cast<ArrayLiteralExpression*>( (Expression*)expr->mFieldValues[i] );
		if ( arrLit != nullptr && arrLit->mElements.empty() )
		{
			Type *fieldType = structDef->mFields[fieldIdx]->getVariableType();
			if ( fieldType != nullptr && fieldType->getName() == "Array" &&
				 fieldType->getNumTypeParams() > 0 )
			{
				Type *elemType = fieldType->getTypeParam( 0 );
				string elemTypeName = elemType->getName();
				auto subIt = mTypeSubstitution.find( elemTypeName );
				if ( subIt != mTypeSubstitution.end() )
					elemType = subIt->second;
				llvm::Type *llvmElemType = getLLVMType( elemType );
				llvm::DataLayout dl( mModule.get() );
				int elemSize = dl.getTypeAllocSize( llvmElemType );
				llvm::Value *elemSizeVal = llvm::ConstantInt::get(
					llvm::Type::getInt32Ty( *mContext ), elemSize );
				llvm::Value *capVal = llvm::ConstantInt::get(
					llvm::Type::getInt64Ty( *mContext ), 8 );
				fieldVal = mBuilder->CreateCall(
					getOrDeclareArrayCreate(), { elemSizeVal, capVal }, "arr" );

				// Set element destructor for refcounted element types
				string resolvedElemName = elemType->getName();
				auto subIt2 = mTypeSubstitution.find( resolvedElemName );
				if ( subIt2 != mTypeSubstitution.end() )
					resolvedElemName = subIt2->second->getName();
				emitArrayElemDtor( fieldVal, resolvedElemName );
			}
		}

		if ( fieldVal == nullptr )
			fieldVal = genExpression( expr->mFieldValues[i] );
		if ( fieldVal == nullptr )
			continue;

		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, heapPtr, fieldIdx, "field" );

		// Coerce value type to match field type (e.g., double literal -> float field)
		llvm::Type *fieldType = structType->getElementType( fieldIdx );
		if ( fieldVal->getType() != fieldType )
		{
			if ( fieldType->isFloatTy() && fieldVal->getType()->isDoubleTy() )
				fieldVal = mBuilder->CreateFPTrunc( fieldVal, fieldType, "fptrunc" );
			else if ( fieldType->isDoubleTy() && fieldVal->getType()->isFloatTy() )
				fieldVal = mBuilder->CreateFPExt( fieldVal, fieldType, "fpext" );
			else if ( fieldType->isIntegerTy() && fieldVal->getType()->isIntegerTy() )
			{
				bool isSigned = true;
				if ( fieldIdx >= 0 && (size_t)fieldIdx < structDef->mFields.size() )
				{
					Type *ft = structDef->mFields[fieldIdx]->getVariableType();
					if ( ft != nullptr && ft->getName() == "byte" )
						isSigned = false;
				}
				fieldVal = mBuilder->CreateIntCast( fieldVal, fieldType,
					isSigned, "icast" );
			}
		}

		mBuilder->CreateStore( fieldVal, fieldPtr );

		// Retain refcounted fields stored into the struct.
		// For strings: always retain (temp string release balances the extra refcount).
		// For arrays/buffers/structs from variables/field accesses: retain (source keeps ref).
		// For arrays/buffers/structs from literals/calls: skip retain (ownership transfers).
		if ( fieldIdx >= 0 && (size_t)fieldIdx < structDef->mFields.size() )
		{
			Type *fType = structDef->mFields[fieldIdx]->getVariableType();
			if ( fType != nullptr )
			{
				string fTypeName = fType->getName();
				// Resolve generic type params (e.g., T -> string)
				auto subIt = mTypeSubstitution.find( fTypeName );
				if ( subIt != mTypeSubstitution.end() )
					fTypeName = subIt->second->getName();
				// Check if source is an existing owner (variable/field access)
				bool srcIsExistingOwner = false;
				{
					auto *se = (Expression*)expr->mFieldValues[i];
					srcIsExistingOwner = ( dynamic_cast<VariableExpression*>( se ) != nullptr ||
										   dynamic_cast<FieldAccessExpression*>( se ) != nullptr );
				}

				if ( fTypeName == "string" )
					mBuilder->CreateCall( getOrDeclareStringRetain(), { fieldVal } );
				else if ( fTypeName == "Array" && srcIsExistingOwner )
					mBuilder->CreateCall( getOrDeclareArrayRetain(), { fieldVal } );
				else if ( fTypeName == "Array" && !srcIsExistingOwner )
					// Fresh array rvalue (call/method result) transfers ownership
					// into the struct field — untrack so it is not also released as
					// a statement temporary (which would double-free).
					untrackTempArray( fieldVal );
				else if ( fTypeName == "Buffer" && srcIsExistingOwner )
					mBuilder->CreateCall( getOrDeclareBufferRetain(), { fieldVal } );
				else if ( isUserStructType( fTypeName ) && srcIsExistingOwner )
					mBuilder->CreateCall( getOrDeclareRcRetain(), { fieldVal } );
				else if ( isUserStructType( fTypeName ) && !srcIsExistingOwner )
					untrackTempStruct( fieldVal );
				else if ( fType->isFunctionType() )
				{
					llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
						fieldVal, 1, "sl.fn.ctx" );
					mBuilder->CreateCall( getOrDeclareLambdaCtxRetain(), { ctxPtr } );
				}
			}
		}
	}

	// Restore substitution map
	mTypeSubstitution = savedSub;

	// Track this struct as a temporary — it will be released after the enclosing
	// statement unless it gets stored into a variable (which untracts it).
	trackTempStruct( heapPtr );

	// Return the heap pointer (struct is by-reference)
	return heapPtr;
}

llvm::Value *CodeGen::genConstructExpression( ConstructExpression *expr )
{
	StructDefinition *structDef = expr->mStructDef;
	if ( structDef == nullptr )
		return nullptr;

	string structName = structDef->getName();
	llvm::StructType *structType = nullptr;

	// Handle generic struct instantiation
	std::map<std::string, Type*> savedSub = mTypeSubstitution;
	if ( !expr->mTypeArgs.empty() && structDef->isGeneric() )
	{
		structType = instantiateGenericStruct( structDef, expr->mTypeArgs );
		for ( size_t i = 0; i < structDef->mGenericParams.size() && i < expr->mTypeArgs.size(); i++ )
		{
			SmartPtr<Type> arg = expr->mTypeArgs[i];
			mTypeSubstitution[structDef->mGenericParams[i].mName] = (Type*)arg;
		}
		std::vector<SmartPtr<Type>> typeArgs( expr->mTypeArgs.begin(), expr->mTypeArgs.end() );
		structName = mangleGenericName( structName, typeArgs );
	}
	else
	{
		structType = getOrCreateStructType( structDef );
	}

	// Heap-allocate the struct with ARC
	llvm::DataLayout dl( mModule.get() );
	uint64_t dataSize = dl.getTypeAllocSize( structType );
	llvm::Value *sizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), dataSize );

	std::map<std::string, std::string> dtorSub;
	if ( !expr->mTypeArgs.empty() && structDef->isGeneric() )
	{
		for ( size_t i = 0; i < structDef->mGenericParams.size() && i < expr->mTypeArgs.size(); i++ )
		{
			SmartPtr<Type> arg = expr->mTypeArgs[i];
			string resolvedName = arg->getName();
			auto sIt2 = mTypeSubstitution.find( resolvedName );
			if ( sIt2 != mTypeSubstitution.end() )
				resolvedName = sIt2->second->getName();
			dtorSub[structDef->mGenericParams[i].mName] = resolvedName;
		}
	}

	llvm::Function *dtorFn = getOrGenStructDestructor( structDef, dtorSub );
	llvm::Value *heapPtr = nullptr;
	if ( dtorFn != nullptr )
		heapPtr = mBuilder->CreateCall( getOrDeclareRcAllocDtor(), { sizeVal, dtorFn }, "ctor.ptr" );
	else
		heapPtr = mBuilder->CreateCall( getOrDeclareRcAlloc(), { sizeVal }, "ctor.ptr" );

	// Call the init method: StructName_init(heapPtr, args...)
	string initName = structName + "_init";
	llvm::Function *initFn = mModule->getFunction( initName );

	// Also try with module prefix
	if ( initFn == nullptr )
	{
		for ( auto &fn : *mModule )
		{
			string fname = fn.getName().str();
			if ( fname.size() > initName.size() + 2 &&
				 fname.substr( fname.size() - initName.size() ) == initName &&
				 fname[fname.size() - initName.size() - 1] == '_' &&
				 fname[fname.size() - initName.size() - 2] == '_' )
			{
				initFn = &fn;
				break;
			}
		}
	}

	if ( initFn == nullptr )
	{
		// Check mFunctionMap for the init method
		FunctionDefinition *initMethod = structDef->getInitMethod();
		if ( initMethod != nullptr )
		{
			auto fIt = mFunctionMap.find( initMethod );
			if ( fIt != mFunctionMap.end() )
				initFn = fIt->second;
		}
	}

	if ( initFn != nullptr )
	{
		std::vector<llvm::Value*> args;
		args.push_back( heapPtr ); // self
		for ( auto &argExpr : expr->mArgs )
		{
			llvm::Value *argVal = genExpression( argExpr );
			if ( argVal == nullptr )
				return nullptr;
			args.push_back( argVal );
		}
		mBuilder->CreateCall( initFn, args );
	}

	mTypeSubstitution = savedSub;
	trackTempStruct( heapPtr );
	return heapPtr;
}

// ---- Builtin string/array field and method codegen ----

llvm::Value *CodeGen::genStringFieldAccess( FieldAccessExpression *expr )
{
	llvm::Value *strVal = genExpression( expr->mObject );
	if ( strVal == nullptr )
		return nullptr;

	if ( expr->mFieldName == "length" )
		return mBuilder->CreateCall( getOrDeclareStringLength(), { strVal }, "str.len" );
	if ( expr->mFieldName == "is_empty" )
		return mBuilder->CreateCall( getOrDeclareStringIsEmpty(), { strVal }, "str.empty" );

	return nullptr;
}

llvm::Value *CodeGen::genArrayFieldAccess( FieldAccessExpression *expr )
{
	llvm::Value *arrVal = genExpression( expr->mObject );
	if ( arrVal == nullptr )
		return nullptr;

	if ( expr->mFieldName == "length" )
		return mBuilder->CreateCall( getOrDeclareArrayLength(), { arrVal }, "arr.len" );
	if ( expr->mFieldName == "capacity" )
		return mBuilder->CreateCall( getOrDeclareArrayCapacity(), { arrVal }, "arr.cap" );
	if ( expr->mFieldName == "is_empty" )
		return mBuilder->CreateCall( getOrDeclareArrayIsEmpty(), { arrVal }, "arr.empty" );

	return nullptr;
}

llvm::Value *CodeGen::genStringMethodCall( MethodCallExpression *expr )
{
	llvm::Value *strVal = genExpression( expr->mObject );
	if ( strVal == nullptr )
		return nullptr;

	const string &method = expr->mMethodName;

	// No-arg methods: to_upper, to_lower, trim, to_cstring, is_empty, length
	if ( method == "to_upper" )
	{
		llvm::Value *r = mBuilder->CreateCall( getOrDeclareStringToUpper(), { strVal }, "str.upper" );
		trackTempString( r );
		return r;
	}
	if ( method == "to_lower" )
	{
		llvm::Value *r = mBuilder->CreateCall( getOrDeclareStringToLower(), { strVal }, "str.lower" );
		trackTempString( r );
		return r;
	}
	if ( method == "trim" )
	{
		llvm::Value *r = mBuilder->CreateCall( getOrDeclareStringTrim(), { strVal }, "str.trim" );
		trackTempString( r );
		return r;
	}
	if ( method == "to_cstring" )
		return mBuilder->CreateCall( getOrDeclareStringToCstring(), { strVal }, "str.cstr" );
	if ( method == "is_empty" )
		return mBuilder->CreateCall( getOrDeclareStringIsEmpty(), { strVal }, "str.empty" );
	if ( method == "length" )
		return mBuilder->CreateCall( getOrDeclareStringLength(), { strVal }, "str.len" );
	if ( method == "to_int" )
	{
		// int64_t __blang_string_to_int( BlangString *s, bool *ok )
		// For simplicity, pass NULL for the ok pointer (ignore parse errors)
		llvm::Function *toIntFn = mModule->getFunction( "__blang_string_to_int" );
		if ( toIntFn == nullptr )
		{
			llvm::FunctionType *ft = llvm::FunctionType::get(
				llvm::Type::getInt64Ty( *mContext ),
				{ llvm::PointerType::get( *mContext, 0 ),
				  llvm::PointerType::get( *mContext, 0 ) },
				false );
			toIntFn = llvm::Function::Create(
				ft, llvm::Function::ExternalLinkage, "__blang_string_to_int", mModule.get() );
		}
		llvm::Value *nullPtr = llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) );
		llvm::Value *result = mBuilder->CreateCall( toIntFn, { strVal, nullPtr }, "str.toint" );
		// Truncate i64 to i32 for BLang int type
		return mBuilder->CreateTrunc( result, llvm::Type::getInt32Ty( *mContext ), "str.toint.i32" );
	}

	// Single string arg: contains, starts_with, ends_with, index_of, concat
	if ( method == "concat" && expr->mArgs.size() == 1 )
	{
		llvm::Value *argVal = genExpression( expr->mArgs[0] );
		if ( argVal == nullptr )
			return nullptr;
		llvm::Value *r = mBuilder->CreateCall( getOrDeclareStringConcat(), { strVal, argVal }, "str.concat" );
		trackTempString( r );
		return r;
	}

	// Single int arg: char_at, byte_at
	if ( ( method == "char_at" || method == "byte_at" ) && expr->mArgs.size() == 1 )
	{
		llvm::Value *idxVal = genExpression( expr->mArgs[0] );
		if ( idxVal == nullptr )
			return nullptr;
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );

		if ( method == "char_at" )
			return mBuilder->CreateCall( getOrDeclareStringCharAt(), { strVal, idxVal }, "str.charAt" );
		if ( method == "byte_at" )
			return mBuilder->CreateCall( getOrDeclareStringByteAt(), { strVal, idxVal }, "str.byteAt" );
	}

	// Single string arg: contains, starts_with, ends_with, index_of
	if ( ( method == "contains" || method == "starts_with" ||
		   method == "ends_with" || method == "index_of" ) &&
		 expr->mArgs.size() == 1 )
	{
		llvm::Value *argVal = genExpression( expr->mArgs[0] );
		if ( argVal == nullptr )
			return nullptr;

		if ( method == "contains" )
			return mBuilder->CreateCall( getOrDeclareStringContains(), { strVal, argVal }, "str.contains" );
		if ( method == "starts_with" )
			return mBuilder->CreateCall( getOrDeclareStringStartsWith(), { strVal, argVal }, "str.starts" );
		if ( method == "ends_with" )
			return mBuilder->CreateCall( getOrDeclareStringEndsWith(), { strVal, argVal }, "str.ends" );
		if ( method == "index_of" )
			return mBuilder->CreateCall( getOrDeclareStringIndexOf(), { strVal, argVal }, "str.indexOf" );
	}

	// Two int args: substring(start, end)
	if ( method == "substring" && expr->mArgs.size() == 2 )
	{
		llvm::Value *startVal = genExpression( expr->mArgs[0] );
		llvm::Value *endVal = genExpression( expr->mArgs[1] );
		if ( startVal == nullptr || endVal == nullptr )
			return nullptr;

		// Extend to i64 if needed
		if ( !startVal->getType()->isIntegerTy( 64 ) )
			startVal = mBuilder->CreateSExt( startVal,
				llvm::Type::getInt64Ty( *mContext ), "start.ext" );
		if ( !endVal->getType()->isIntegerTy( 64 ) )
			endVal = mBuilder->CreateSExt( endVal,
				llvm::Type::getInt64Ty( *mContext ), "end.ext" );

		llvm::Value *r = mBuilder->CreateCall(
			getOrDeclareStringSubstring(), { strVal, startVal, endVal }, "str.sub" );
		trackTempString( r );
		return r;
	}

	// Three string args: replace(old, new)
	if ( method == "replace" && expr->mArgs.size() == 2 )
	{
		llvm::Value *oldVal = genExpression( expr->mArgs[0] );
		llvm::Value *newVal = genExpression( expr->mArgs[1] );
		if ( oldVal == nullptr || newVal == nullptr )
			return nullptr;

		llvm::Value *r = mBuilder->CreateCall(
			getOrDeclareStringReplace(), { strVal, oldVal, newVal }, "str.replace" );
		trackTempString( r );
		return r;
	}

	return nullptr;
}

llvm::Value *CodeGen::genArrayMethodCall( MethodCallExpression *expr )
{
	llvm::Value *arrVal = genExpression( expr->mObject );
	if ( arrVal == nullptr )
		return nullptr;

	const string &method = expr->mMethodName;

	// No-arg property-like methods: is_empty(), length(), capacity()
	if ( method == "is_empty" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareArrayIsEmpty(), { arrVal }, "arr.empty" );
	if ( method == "length" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareArrayLength(), { arrVal }, "arr.len" );
	if ( method == "capacity" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareArrayCapacity(), { arrVal }, "arr.cap" );

	// push(value): store value to temp alloca, pass its address
	if ( method == "push" && expr->mArgs.size() == 1 )
	{
		llvm::Value *elemVal = genExpression( expr->mArgs[0] );
		if ( elemVal == nullptr )
			return nullptr;

		// Retain refcounted elements pushed into arrays so the array's owned
		// reference (released via the elem_dtor set at array creation) is real;
		// __blang_array_push does not retain. See emitArrayElemRetain. The
		// array's declared Array<T> element type is the authoritative key (it
		// matches the elem_dtor); fall back to the arg's resolved type.
		{
			std::string pushElemType;
			Type *arrElemQType = nullptr;
			if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
			{
				Type *vt = ve->mVariable->getVariableType();
				if ( vt != nullptr && vt->getNumTypeParams() > 0 )
					arrElemQType = vt->getTypeParam( 0 );
			}
			else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mObject ) )
			{
				Type *ft = getFieldType( fa );
				if ( ft != nullptr && ft->getNumTypeParams() > 0 )
					arrElemQType = ft->getTypeParam( 0 );
			}
			if ( arrElemQType != nullptr )
			{
				std::string en = arrElemQType->getName();
				auto subIt = mTypeSubstitution.find( en );
				pushElemType = subIt != mTypeSubstitution.end()
					? subIt->second->getName() : en;
			}
			if ( pushElemType.empty() )
			{
				if ( Type *argQType = ( (Expression*)expr->mArgs[0] )->getResolvedType() )
					pushElemType = argQType->getName();
			}
			emitArrayElemRetain( elemVal, pushElemType );
		}

		// Determine element type from Array<T> and cast if needed
		llvm::Type *elemType = elemVal->getType();
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
			{
				Type *ep = varType->getTypeParam( 0 );
				string en = ep->getName();
				auto subIt = mTypeSubstitution.find( en );
				llvm::Type *declaredType = subIt != mTypeSubstitution.end()
					? getLLVMType( subIt->second ) : getLLVMType( ep );
				if ( declaredType != elemType && declaredType->isIntegerTy() && elemType->isIntegerTy() )
				{
					bool isSigned = ( en != "byte" );
					elemVal = mBuilder->CreateIntCast( elemVal, declaredType, isSigned, "push.cast" );
					elemType = declaredType;
				}
			}
		}

		llvm::AllocaInst *tmpAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, "push.tmp" );
		mBuilder->CreateStore( elemVal, tmpAlloca );
		mBuilder->CreateCall( getOrDeclareArrayPush(), { arrVal, tmpAlloca } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// pop(): get element type, create out alloca, call pop, return value
	if ( method == "pop" && expr->mArgs.empty() )
	{
		// Determine element type from the Array<T> type annotation
		llvm::Type *elemType = llvm::Type::getInt32Ty( *mContext ); // default
		// Resolved element type NAME (after generic substitution), used to give
		// the popped value the same ARC temp-tracking as a function return: pop
		// transfers the array's owned reference to the returned value, so a
		// refcounted element (string/struct/Array) must be tracked as a temporary
		// — released at statement end if discarded (e.g. `arr.pop();` inside
		// Map.remove, which otherwise leaks the popped element), or adopted by an
		// assignment if consumed (`x = arr.pop()`, which untracks the temp).
		string popElemName;
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
			{
				Type *ep = varType->getTypeParam( 0 );
				string en = ep->getName();
				auto subIt = mTypeSubstitution.find( en );
				if ( subIt != mTypeSubstitution.end() )
				{
					elemType = getLLVMType( subIt->second );
					popElemName = subIt->second->getName();
				}
				else
				{
					elemType = getLLVMType( ep );
					popElemName = en;
				}
			}
		}
		else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mObject ) )
		{
			Type *fieldType = getFieldType( fa );
			if ( fieldType != nullptr && fieldType->getNumTypeParams() > 0 )
			{
				Type *ep = fieldType->getTypeParam( 0 );
				string en = ep->getName();
				auto subIt = mTypeSubstitution.find( en );
				if ( subIt != mTypeSubstitution.end() )
				{
					elemType = getLLVMType( subIt->second );
					popElemName = subIt->second->getName();
				}
				else
				{
					elemType = getLLVMType( ep );
					popElemName = en;
				}
			}
		}

		llvm::AllocaInst *outAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, "pop.out" );
		mBuilder->CreateCall( getOrDeclareArrayPop(), { arrVal, outAlloca } );
		llvm::Value *popVal = mBuilder->CreateLoad( elemType, outAlloca, "pop.val" );

		// ARC: track the transferred reference of a refcounted popped element so
		// it is not leaked when the result is discarded (see comment above).
		if ( popElemName == "string" )
			trackTempString( popVal );
		else if ( popElemName == "Array" )
			trackTempArray( popVal );
		else if ( isUserStructType( popElemName ) )
			trackTempStruct( popVal );

		return popVal;
	}

	// clear(): no args
	if ( method == "clear" && expr->mArgs.empty() )
	{
		mBuilder->CreateCall( getOrDeclareArrayClear(), { arrVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	return nullptr;
}

llvm::Value *CodeGen::genFieldAccess( FieldAccessExpression *expr )
{
	// Built-in string property access
	if ( isStringType( expr->mObject ) )
	{
		llvm::Value *result = genStringFieldAccess( expr );
		if ( result != nullptr )
			return result;
	}

	// Built-in array property access
	if ( isArrayType( expr->mObject ) )
	{
		llvm::Value *result = genArrayFieldAccess( expr );
		if ( result != nullptr )
			return result;
	}

	// Built-in buffer property access
	if ( isBufferType( expr->mObject ) )
	{
		llvm::Value *result = genBufferFieldAccess( expr );
		if ( result != nullptr )
			return result;
	}

	// Get the address of the object so we can GEP into it
	llvm::AllocaInst *objAddr = getExpressionAddress( expr->mObject );

	if ( objAddr == nullptr )
	{
		// If we can't get the address directly, generate the value and store
		// it in a temporary alloca
		llvm::Value *objVal = genExpression( expr->mObject );
		if ( objVal == nullptr )
			return nullptr;
		objAddr = mBuilder->CreateAlloca( objVal->getType(), nullptr, "tmp" );
		mBuilder->CreateStore( objVal, objAddr );
	}

	// Determine the struct type from the alloca
	llvm::Type *allocType = objAddr->getAllocatedType();
	llvm::StructType *structType = llvm::dyn_cast<llvm::StructType>( allocType );
	llvm::Value *gepBase = objAddr;

	// If allocType is a pointer (self parameter or shared variable), load the pointer and find the struct type
	if ( structType == nullptr && allocType->isPointerTy() )
	{
		// Load the pointer to get the actual struct address
		gepBase = mBuilder->CreateLoad( allocType, objAddr, "self.ptr" );

		// Find the struct type from the BLang variable definition
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			auto selfIt = mSelfStructMap.find( ve->mVariable );
			if ( selfIt != mSelfStructMap.end() )
			{
				StructDefinition *sd = selfIt->second;

				// For generic struct methods, use the mangled name (e.g. "Box_int")
				auto mangledIt = mSelfStructMangledName.find( ve->mVariable );
				if ( mangledIt != mSelfStructMangledName.end() )
				{
					auto stIt = mStructTypeMap.find( mangledIt->second );
					if ( stIt != mStructTypeMap.end() )
						structType = stIt->second;
				}

				if ( structType == nullptr )
				{
					auto stIt = mStructTypeMap.find( sd->getName() );
					if ( stIt != mStructTypeMap.end() )
						structType = stIt->second;
					else
						structType = getOrCreateStructType( sd );
				}
			}

			// Fallback: resolve struct type from BLang variable type
			// (handles shared/sync variables whose alloca is a pointer)
			if ( structType == nullptr )
			{
				Type *varType = ve->mVariable->getVariableType();
				if ( varType != nullptr )
				{
					string typeName = varType->getName();
					if ( varType->getNumTypeParams() > 0 )
					{
						std::vector<SmartPtr<Type>> typeArgs;
						for ( int i = 0; i < varType->getNumTypeParams(); i++ )
							typeArgs.push_back( varType->getTypeParam( i ) );
						typeName = mangleGenericName( varType->getName(), typeArgs );
					}
					auto stIt = mStructTypeMap.find( typeName );
					if ( stIt != mStructTypeMap.end() )
						structType = stIt->second;
					else
					{
						auto defIt = mStructDefMap.find( typeName );
						if ( defIt != mStructDefMap.end() )
							structType = getOrCreateStructType( defIt->second );
					}
				}
			}
		}
	}

	// General fallback: when the object is not a simple VariableExpression
	// (e.g. a chained field access `o.inner.x`, or a call result), resolve its
	// struct type from the Sema-annotated resolved type of the object expression.
	if ( structType == nullptr )
	{
		if ( Type *objType = ( (Expression*)expr->mObject )->getResolvedType() )
		{
			string typeName = objType->getName();
			if ( objType->getNumTypeParams() > 0 )
			{
				std::vector<SmartPtr<Type>> typeArgs;
				for ( int i = 0; i < objType->getNumTypeParams(); i++ )
					typeArgs.push_back( objType->getTypeParam( i ) );
				typeName = mangleGenericName( objType->getName(), typeArgs );
			}
			auto stIt = mStructTypeMap.find( typeName );
			if ( stIt != mStructTypeMap.end() )
				structType = stIt->second;
			else
			{
				auto defIt = mStructDefMap.find( typeName );
				if ( defIt != mStructDefMap.end() )
					structType = getOrCreateStructType( defIt->second );
			}
		}
	}

	if ( structType == nullptr )
		return nullptr;

	// Find the struct definition to get the field index
	StructDefinition *structDef = nullptr;
	if ( structType->hasName() )
	{
		auto defIt = mStructDefMap.find( structType->getName().str() );
		if ( defIt != mStructDefMap.end() )
			structDef = defIt->second;
	}

	if ( structDef == nullptr )
		return nullptr;

	// Find the field index
	int fieldIdx = -1;
	for ( size_t i = 0; i < structDef->mFields.size(); i++ )
	{
		if ( structDef->mFields[i]->getName() == expr->mFieldName )
		{
			fieldIdx = static_cast<int>( i );
			break;
		}
	}

	if ( fieldIdx < 0 )
		return nullptr;  // U3: unreachable for a concrete struct base — sema (Sema.cpp
		                 // resolveFieldAccess) rejects unknown fields before codegen.
		                 // Kept as-is; loud-ICE hardening of codegen fallbacks is U4.

	llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, gepBase, fieldIdx, expr->mFieldName );
	llvm::Value *fieldVal = mBuilder->CreateLoad(
		structType->getElementType( fieldIdx ), fieldPtr, expr->mFieldName + ".val" );

	// Retain refcounted fields so the loaded value survives independently of the struct.
	// The struct will release its reference at scope exit (emitStructFieldRelease),
	// and this retain ensures the loaded value stays valid until it is released
	// (either as a temp string or when the variable storing it goes out of scope).
	if ( structDef != nullptr && fieldIdx < (int)structDef->mFields.size() )
	{
		// U3 typed AST (FR-011): read the field type the semantic pass resolved
		// for this access when available; fall back to the struct field's
		// declared type (sema leaves generic-parameter fields unannotated, and
		// those still need the substitution the declared type carries).
		Type *fType = expr->getResolvedType();
		if ( fType == nullptr )
			fType = structDef->mFields[fieldIdx]->getVariableType();
		if ( fType != nullptr )
		{
			string fName = fType->getName();
			auto subIt = mTypeSubstitution.find( fName );
			if ( subIt != mTypeSubstitution.end() )
				fName = subIt->second->getName();

			if ( fName == "string" )
			{
				mBuilder->CreateCall( getOrDeclareStringRetain(), { fieldVal } );
				trackTempString( fieldVal );
			}
			// Note: Array and Buffer fields are NOT retained on field access.
			// The struct owns them and won't release during the statement.
			// If stored to a variable, genVariableDeclaration handles retain.
			else if ( fType->isFunctionType() )
			{
				llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
					fieldVal, 1, "fa.fn.ctx" );
				mBuilder->CreateCall( getOrDeclareLambdaCtxRetain(), { ctxPtr } );
			}
		}
	}

	return fieldVal;
}

llvm::Value *CodeGen::genFieldAssignment( FieldAssignmentExpression *expr )
{
	// Get the object address
	llvm::AllocaInst *objAddr = getExpressionAddress( expr->mObject );
	if ( objAddr == nullptr )
	{
		// The object is not a simple variable (e.g. a chained field assignment
		// `o.inner.x = v`, or a call result). Structs are heap pointers, so
		// generating the object expression yields a pointer to the target
		// struct; stash it in a temp alloca so the GEP/store logic below works.
		llvm::Value *objVal = genExpression( expr->mObject );
		if ( objVal == nullptr )
			return nullptr;
		objAddr = mBuilder->CreateAlloca( objVal->getType(), nullptr, "tmp.obj" );
		mBuilder->CreateStore( objVal, objAddr );
	}

	// Determine struct type from the alloca
	llvm::Type *allocType = objAddr->getAllocatedType();
	llvm::StructType *structType = llvm::dyn_cast<llvm::StructType>( allocType );
	llvm::Value *gepBase = objAddr;

	// If allocType is a pointer (self parameter or shared variable), load the pointer and find the struct type
	if ( structType == nullptr && allocType->isPointerTy() )
	{
		gepBase = mBuilder->CreateLoad( allocType, objAddr, "self.ptr" );

		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			auto selfIt = mSelfStructMap.find( ve->mVariable );
			if ( selfIt != mSelfStructMap.end() )
			{
				StructDefinition *sd = selfIt->second;

				// For generic struct methods, use the mangled name (e.g. "Box_int")
				auto mangledIt = mSelfStructMangledName.find( ve->mVariable );
				if ( mangledIt != mSelfStructMangledName.end() )
				{
					auto stIt = mStructTypeMap.find( mangledIt->second );
					if ( stIt != mStructTypeMap.end() )
						structType = stIt->second;
				}

				if ( structType == nullptr )
				{
					auto stIt = mStructTypeMap.find( sd->getName() );
					if ( stIt != mStructTypeMap.end() )
						structType = stIt->second;
					else
						structType = getOrCreateStructType( sd );
				}
			}

			// Fallback: resolve struct type from BLang variable type
			// (handles shared/sync variables whose alloca is a pointer)
			if ( structType == nullptr )
			{
				Type *varType = ve->mVariable->getVariableType();
				if ( varType != nullptr )
				{
					string typeName = varType->getName();
					if ( varType->getNumTypeParams() > 0 )
					{
						std::vector<SmartPtr<Type>> typeArgs;
						for ( int i = 0; i < varType->getNumTypeParams(); i++ )
							typeArgs.push_back( varType->getTypeParam( i ) );
						typeName = mangleGenericName( varType->getName(), typeArgs );
					}
					auto stIt = mStructTypeMap.find( typeName );
					if ( stIt != mStructTypeMap.end() )
						structType = stIt->second;
					else
					{
						auto defIt = mStructDefMap.find( typeName );
						if ( defIt != mStructDefMap.end() )
							structType = getOrCreateStructType( defIt->second );
					}
				}
			}
		}
	}

	// General fallback: chained field assignment (`o.inner.x = v`) or a
	// call-result object resolves its struct type from the Sema-annotated
	// resolved type of the object expression.
	if ( structType == nullptr )
	{
		if ( Type *objType = ( (Expression*)expr->mObject )->getResolvedType() )
		{
			string typeName = objType->getName();
			if ( objType->getNumTypeParams() > 0 )
			{
				std::vector<SmartPtr<Type>> typeArgs;
				for ( int i = 0; i < objType->getNumTypeParams(); i++ )
					typeArgs.push_back( objType->getTypeParam( i ) );
				typeName = mangleGenericName( objType->getName(), typeArgs );
			}
			auto stIt = mStructTypeMap.find( typeName );
			if ( stIt != mStructTypeMap.end() )
				structType = stIt->second;
			else
			{
				auto defIt = mStructDefMap.find( typeName );
				if ( defIt != mStructDefMap.end() )
					structType = getOrCreateStructType( defIt->second );
			}
		}
	}

	if ( structType == nullptr )
		return nullptr;

	// Find the struct definition to get the field index
	StructDefinition *structDef = nullptr;
	if ( structType->hasName() )
	{
		auto defIt = mStructDefMap.find( structType->getName().str() );
		if ( defIt != mStructDefMap.end() )
			structDef = defIt->second;
	}

	if ( structDef == nullptr )
		return nullptr;

	// Find the field index
	int fieldIdx = -1;
	for ( size_t i = 0; i < structDef->mFields.size(); i++ )
	{
		if ( structDef->mFields[i]->getName() == expr->mFieldName )
		{
			fieldIdx = static_cast<int>( i );
			break;
		}
	}

	if ( fieldIdx < 0 )
		return nullptr;

	// Generate the value to assign
	// If the field is Array<T> and the RHS is an array literal, set the element type hint
	// so that the correct elem_size is used (e.g., 1 for byte instead of default 4)
	VariableDefinition *fieldDef = structDef->mFields[fieldIdx];
	Type *fieldType = fieldDef->getVariableType();
	if ( fieldType != nullptr && fieldType->getName() == "Array" &&
		 fieldType->getNumTypeParams() > 0 &&
		 dynamic_cast<ArrayLiteralExpression*>( (Expression*)expr->mValue ) != nullptr )
	{
		Type *elemType = fieldType->getTypeParam( 0 );
		mArrayElemTypeHint = getLLVMType( elemType );
		string etn = elemType->getName();
		auto subEtn = mTypeSubstitution.find( etn );
		if ( subEtn != mTypeSubstitution.end() )
			etn = subEtn->second->getName();
		mArrayElemTypeNameHint = etn;
	}

	llvm::Value *val = genExpression( expr->mValue );
	if ( val == nullptr )
		return nullptr;

	// Handle compound assignment operators (+=, -=, etc.)
	if ( expr->mOperation != "=" )
	{
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, gepBase, fieldIdx, expr->mFieldName );
		llvm::Value *currentVal = mBuilder->CreateLoad(
			structType->getElementType( fieldIdx ), fieldPtr, expr->mFieldName + ".cur" );

		if ( expr->mOperation == "+=" )
			val = mBuilder->CreateAdd( currentVal, val, "addtmp" );
		else if ( expr->mOperation == "-=" )
			val = mBuilder->CreateSub( currentVal, val, "subtmp" );
		else if ( expr->mOperation == "*=" )
			val = mBuilder->CreateMul( currentVal, val, "multmp" );
		else if ( expr->mOperation == "/=" )
			val = mBuilder->CreateSDiv( currentVal, val, "divtmp" );
		else if ( expr->mOperation == "%=" )
			val = mBuilder->CreateSRem( currentVal, val, "modtmp" );
	}

	llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, gepBase, fieldIdx, expr->mFieldName + ".ptr" );

	// For a refcounted string field, take ownership of the new value and drop
	// the previous one so the field holds a counted reference — this mirrors
	// struct initialization, which stores + retains. Without the retain the
	// stored string is under-counted and the statement-temp release frees it,
	// leaving the field dangling (a double-free / use-after-free). Retain the
	// new value before releasing the old so self-assignment is safe.
	if ( fieldType != nullptr && fieldType->getName() == "string" &&
		 expr->mOperation == "=" )
	{
		llvm::Value *oldVal = mBuilder->CreateLoad(
			structType->getElementType( fieldIdx ), fieldPtr,
			expr->mFieldName + ".old" );
		mBuilder->CreateStore( val, fieldPtr );
		mBuilder->CreateCall( getOrDeclareStringRetain(), { val } );
		mBuilder->CreateCall( getOrDeclareStringRelease(), { oldVal } );
	}
	else
	{
		// Resolve the field's type name, mapping generic params (e.g. T -> Inner).
		string fTypeName = fieldType != nullptr ? fieldType->getName() : "";
		auto subIt = mTypeSubstitution.find( fTypeName );
		if ( subIt != mTypeSubstitution.end() )
			fTypeName = subIt->second->getName();

		if ( expr->mOperation == "=" && isUserStructType( fTypeName ) )
		{
			// Refcounted user-struct field reassignment (the S1 fix). A struct is
			// a heap pointer; the RHS struct literal/call is tracked as a statement
			// temporary, so a bare store leaves the field owning an un-counted
			// reference that is then freed when the temp is released at end of
			// statement — the field dangles and later reads hit freed memory (a
			// read-path-dependent use-after-free: the first read may still see the
			// value, but any intervening allocation, e.g. print/interpolation,
			// reuses the freed block and later reads return stale data). Mirror
			// struct initialization: take ownership of the new value and drop the
			// previously-held struct. Whether the RHS is a fresh temporary
			// (literal/call — ownership transfers) or an existing owner
			// (variable/field access — must retain) decides how ownership is taken.
			// Retain the new value BEFORE releasing the old so a self-assignment
			// (o.inner = o.inner) cannot free a value it is about to keep.
			bool srcIsExistingOwner =
				( dynamic_cast<VariableExpression*>( (Expression*)expr->mValue ) != nullptr ||
				  dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mValue ) != nullptr );

			llvm::Value *oldVal = mBuilder->CreateLoad(
				structType->getElementType( fieldIdx ), fieldPtr,
				expr->mFieldName + ".old" );
			mBuilder->CreateStore( val, fieldPtr );
			if ( srcIsExistingOwner )
				mBuilder->CreateCall( getOrDeclareRcRetain(), { val } );
			else
				untrackTempStruct( val );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { oldVal } );
		}
		else
		{
			mBuilder->CreateStore( val, fieldPtr );

			// If a fresh Array<T> rvalue temporary is stored into a field, ownership
			// transfers to the struct — untrack it so it is not also released as a
			// statement temporary (which would leave the field pointing at freed memory).
			if ( fTypeName == "Array" )
				untrackTempArray( val );
		}
	}

	return val;
}

llvm::Value *CodeGen::genIndexAssignment( IndexAssignmentExpression *expr )
{
	// Check if the object is an array
	if ( isArrayType( expr->mObject ) )
	{
		llvm::Value *arrVal = genExpression( expr->mObject );
		llvm::Value *idxVal = genExpression( expr->mIndex );
		llvm::Value *val = genExpression( expr->mValue );
		if ( arrVal == nullptr || idxVal == nullptr || val == nullptr )
			return nullptr;

		// Extend index to i64 if needed
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );

		// For compound assignments, load current value first
		if ( expr->mOperation != "=" )
		{
			llvm::Function *getFn = getOrDeclareArrayGet();
			llvm::AllocaInst *tmpAlloca = mBuilder->CreateAlloca(
				val->getType(), nullptr, "getval" );
			mBuilder->CreateCall( getFn, { arrVal, idxVal, tmpAlloca } );
			llvm::Value *currentVal = mBuilder->CreateLoad( val->getType(), tmpAlloca, "curval" );

			if ( expr->mOperation == "+=" )
				val = mBuilder->CreateAdd( currentVal, val, "addtmp" );
			else if ( expr->mOperation == "-=" )
				val = mBuilder->CreateSub( currentVal, val, "subtmp" );
			else if ( expr->mOperation == "*=" )
				val = mBuilder->CreateMul( currentVal, val, "multmp" );
			else if ( expr->mOperation == "/=" )
				val = mBuilder->CreateSDiv( currentVal, val, "divtmp" );
		}

		// Call __blang_array_set(arr, index, &value)
		llvm::Function *setFn = getOrDeclareArraySet();
		llvm::AllocaInst *valAlloca = mBuilder->CreateAlloca( val->getType(), nullptr, "setval" );
		mBuilder->CreateStore( val, valAlloca );

		mBuilder->CreateCall( setFn, { arrVal, idxVal, valAlloca } );

		// ARC: the slot now owns a reference to `val`, so retain it for a
		// refcounted element type — mirroring the retain-on-push path. Without
		// this, `arr[i] = x` under-counts: __blang_array_set releases the old
		// element but the new one is stored un-retained, so a temporary RHS is
		// freed at statement end (dangling slot) and a shift `arr[j] = arr[j+1]`
		// leaves two slots sharing one reference (double-free once pop releases
		// the transferred reference). emitArrayElemRetain no-ops for value types.
		if ( expr->mOperation == "=" )
		{
			std::string setElemType;
			Type *arrElemQType = nullptr;
			if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
			{
				Type *vt = ve->mVariable->getVariableType();
				if ( vt != nullptr && vt->getNumTypeParams() > 0 )
					arrElemQType = vt->getTypeParam( 0 );
			}
			else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mObject ) )
			{
				Type *ft = getFieldType( fa );
				if ( ft != nullptr && ft->getNumTypeParams() > 0 )
					arrElemQType = ft->getTypeParam( 0 );
			}
			if ( arrElemQType != nullptr )
			{
				std::string en = arrElemQType->getName();
				auto subIt = mTypeSubstitution.find( en );
				setElemType = subIt != mTypeSubstitution.end()
					? subIt->second->getName() : en;
				emitArrayElemRetain( val, setElemType );
			}
		}

		return val;
	}

	return nullptr;
}

bool CodeGen::isChanType( Expression *expr )
{
	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		return varType != nullptr && varType->getName() == "chan";
	}
	return false;
}

// Resolve the QLang element type T of a chan<T> object expression.
// Returns nullptr if the type parameter is missing (treated as int-sized).
Type *CodeGen::getChanElementQType( Expression *expr )
{
	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		if ( varType != nullptr && varType->getNumTypeParams() > 0 )
			return varType->getTypeParam( 0 );
	}
	return nullptr;
}

// Codegen for channel method calls: send(value), recv(), close().
//   chan<T> declarations store a BlangChan* (see genVariableDeclaration).
//   send marshals the element through a stack slot of T's LLVM type;
//   recv returns the built-in Option<T> (some(value) on success, none on
//   closed+empty), matching the byte-copy contract of __blang_chan_send/recv.
// (Ported from origin's monolithic CodeGen.cpp into the CG* structure; recv
// depends on the built-in Option registered in U4.)
llvm::Value *CodeGen::genChanMethodCall( MethodCallExpression *expr )
{
	const string &method = expr->mMethodName;

	// Load the BlangChan* from the channel variable.
	llvm::Value *chanVal = genExpression( expr->mObject );
	if ( chanVal == nullptr )
		return nullptr;

	// Determine the element LLVM type (default to i32 when unparameterized,
	// consistent with the default element size used at channel creation).
	Type *elemQType = getChanElementQType( expr->mObject );
	llvm::Type *elemType = elemQType != nullptr
		? getLLVMType( elemQType )
		: llvm::Type::getInt32Ty( *mContext );
	if ( elemType == nullptr )
		elemType = llvm::Type::getInt32Ty( *mContext );

	// close() -> void
	if ( method == "close" && expr->mArgs.empty() )
	{
		mBuilder->CreateCall( getOrDeclareChanClose(), { chanVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// send(value) -> void  (void __blang_chan_send(BlangChan*, const void*))
	if ( method == "send" && expr->mArgs.size() == 1 )
	{
		llvm::Value *valVal = genExpression( expr->mArgs[0] );
		if ( valVal == nullptr )
			return nullptr;

		// Coerce integer widths to the channel element type so the byte copy
		// transfers exactly elem_size bytes (e.g. an i32 literal into chan<long>).
		// Element types are restricted to value types (Sema rejects refcounted
		// element types — see Sema channel-element check), so no retain is needed:
		// the byte copy fully transfers a primitive value into the channel.
		if ( valVal->getType()->isIntegerTy() && elemType->isIntegerTy() &&
			 valVal->getType() != elemType )
			valVal = mBuilder->CreateIntCast( valVal, elemType, true, "chan.val" );

		llvm::Value *slot = mBuilder->CreateAlloca( elemType, nullptr, "chan.send.slot" );
		mBuilder->CreateStore( valVal, slot );
		mBuilder->CreateCall( getOrDeclareChanSend(), { chanVal, slot } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// recv() -> Option<T>
	//   int __blang_chan_recv(BlangChan*, void* out) returns 1 on success, 0 if
	//   the channel is closed and empty.  We recv directly into the payload area
	//   of an Option<T> enum, then set the tag from the success flag: some(value)
	//   on success, none on closed+empty.  Exhaustive match then forces callers
	//   to handle the closed case.
	if ( method == "recv" && expr->mArgs.empty() )
	{
		EnumDefinition *optEnum = nullptr;
		auto optIt = mEnumDefMap.find( "Option" );
		if ( optIt != mEnumDefMap.end() )
			optEnum = optIt->second;
		if ( optEnum == nullptr )
			return nullptr;

		int someIdx = -1, noneIdx = -1;
		for ( size_t v = 0; v < optEnum->mVariants.size(); v++ )
		{
			if ( optEnum->mVariants[v].mName == "some" ) someIdx = (int)v;
			else if ( optEnum->mVariants[v].mName == "none" ) noneIdx = (int)v;
		}
		if ( someIdx < 0 || noneIdx < 0 )
			return nullptr;

		llvm::StructType *optType = getOrCreateEnumType( optEnum );
		llvm::AllocaInst *resultAlloca = mBuilder->CreateAlloca(
			optType, nullptr, "chan.recv.opt" );

		// GEP to the payload byte area (field 1, byte 0) and zero-initialize it,
		// so the none case has a defined payload.
		llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
			optType, resultAlloca, 1, "opt.payload.ptr" );
		llvm::Type *payloadArrType = optType->getElementType( 1 );
		llvm::Value *bytePtr = mBuilder->CreateGEP(
			payloadArrType, payloadPtr,
			{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
			  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
			"opt.payload.byte" );
		mBuilder->CreateStore( llvm::Constant::getNullValue( elemType ), bytePtr );

		// Receive directly into the payload; flag is 1 on success, 0 on closed+empty.
		llvm::Value *flag = mBuilder->CreateCall(
			getOrDeclareChanRecv(), { chanVal, bytePtr }, "chan.recv.flag" );

		// tag = success ? some : none
		llvm::Value *isSuccess = mBuilder->CreateICmpNE(
			flag, llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 0 ),
			"chan.recv.ok" );
		llvm::Value *tagVal = mBuilder->CreateSelect(
			isSuccess,
			llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), someIdx ),
			llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), noneIdx ),
			"opt.tag" );
		llvm::Value *tagPtr = mBuilder->CreateStructGEP(
			optType, resultAlloca, 0, "opt.tag.ptr" );
		mBuilder->CreateStore( tagVal, tagPtr );

		return mBuilder->CreateLoad( optType, resultAlloca, "chan.recv.opt.val" );
	}

	return nullptr;
}

llvm::Value *CodeGen::genMethodCall( MethodCallExpression *expr )
{
	// Built-in channel method calls: send/recv/close on a chan<T>.
	if ( isChanType( expr->mObject ) )
	{
		llvm::Value *result = genChanMethodCall( expr );
		if ( result != nullptr )
			return result;
	}

	// Built-in string method calls
	if ( isStringType( expr->mObject ) )
	{
		llvm::Value *result = genStringMethodCall( expr );
		if ( result != nullptr )
			return result;
	}

	// Built-in array method calls
	if ( isArrayType( expr->mObject ) )
	{
		llvm::Value *result = genArrayMethodCall( expr );
		if ( result != nullptr )
			return result;
	}

	// Built-in buffer method calls
	if ( isBufferType( expr->mObject ) )
	{
		llvm::Value *result = genBufferMethodCall( expr );
		if ( result != nullptr )
			return result;
	}

	// Determine the struct type from the object
	StructDefinition *structDef = nullptr;
	string structName;

	if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		if ( varType != nullptr )
		{
			structName = varType->getName();

			// Handle generic struct types: Box<int> -> structName = "Box_int"
			if ( varType->getNumTypeParams() > 0 )
			{
				std::vector<SmartPtr<Type>> typeArgs;
				for ( int i = 0; i < varType->getNumTypeParams(); i++ )
					typeArgs.push_back( varType->getTypeParam( i ) );
				structName = mangleGenericName( varType->getName(), typeArgs );
			}

			auto defIt = mStructDefMap.find( structName );
			if ( defIt != mStructDefMap.end() )
				structDef = defIt->second;
		}
	}

	// If structDef is still null, check if this is a 'self' parameter
	if ( structDef == nullptr )
	{
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			auto selfIt = mSelfStructMap.find( ve->mVariable );
			if ( selfIt != mSelfStructMap.end() )
			{
				structDef = selfIt->second;
				structName = structDef->getName();
				// Check for generic mangled name
				auto mangledIt = mSelfStructMangledName.find( ve->mVariable );
				if ( mangledIt != mSelfStructMangledName.end() )
					structName = mangledIt->second;
			}
		}
	}

	// If structDef is still null, try to resolve from expression's return type
	// (e.g., CallExpression receiver: info(path).exists())
	if ( structDef == nullptr )
	{
		if ( auto *callExpr = dynamic_cast<CallExpression*>( (Expression*)expr->mObject ) )
		{
			if ( callExpr->mFunction != nullptr && callExpr->mFunction->getReturnType() != nullptr )
			{
				structName = callExpr->mFunction->getReturnType()->getName();
				auto defIt = mStructDefMap.find( structName );
				if ( defIt != mStructDefMap.end() )
					structDef = defIt->second;
			}
		}
		else if ( auto *mcExpr = dynamic_cast<MethodCallExpression*>( (Expression*)expr->mObject ) )
		{
			// Chained method call: resolve from the inner method's return type
			// We need to find the struct and method to get the return type
			// For now, generate the inner expression and check the result
			(void)mcExpr; // handled by genExpression fallback at self-generation
		}
	}

	if ( structDef == nullptr )
		return nullptr;

	// Find the method in the struct's method list
	FunctionDefinition *methodDef = nullptr;
	for ( auto &method : structDef->mMethods )
	{
		if ( method->getName() == expr->mMethodName )
		{
			methodDef = method;
			break;
		}
	}

	// Fallback: check if this is a fn-typed field call
	if ( methodDef == nullptr && structDef != nullptr )
	{
		// Find field with matching name
		int fieldIdx = -1;
		VariableDefinition *fieldDef = nullptr;
		for ( size_t i = 0; i < structDef->mFields.size(); i++ )
		{
			if ( structDef->mFields[i]->getName() == expr->mMethodName )
			{
				fieldIdx = static_cast<int>( i );
				fieldDef = structDef->mFields[i];
				break;
			}
		}

		if ( fieldDef != nullptr && fieldDef->getVariableType()->isFunctionType() )
		{
			// Load the {fn_ptr, ctx_ptr} from the struct field
			llvm::StructType *structType = nullptr;
			auto stIt = mStructTypeMap.find( structName );
			if ( stIt != mStructTypeMap.end() )
				structType = stIt->second;
			else
				structType = getOrCreateStructType( structDef );

			if ( structType == nullptr )
				return nullptr;

			llvm::AllocaInst *objAddr = getExpressionAddress( expr->mObject );
			llvm::Value *gepBase = (llvm::Value*)objAddr;

			if ( objAddr == nullptr )
			{
				llvm::Value *objVal = genExpression( expr->mObject );
				if ( objVal == nullptr )
					return nullptr;
				objAddr = mBuilder->CreateAlloca( objVal->getType(), nullptr, "tmp.fn" );
				mBuilder->CreateStore( objVal, objAddr );
				gepBase = objAddr;
			}

			// Handle self-by-pointer case (shared or self parameter)
			llvm::Type *allocType = objAddr->getAllocatedType();
			if ( allocType->isPointerTy() && !llvm::isa<llvm::StructType>( allocType ) )
			{
				gepBase = mBuilder->CreateLoad( allocType, objAddr, "self.ptr" );
			}

			llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, gepBase, fieldIdx, "fn.field" );
			llvm::Value *fnPair = mBuilder->CreateLoad( structType->getElementType( fieldIdx ), fieldPtr, "fn.pair" );

			// Extract fn_ptr and ctx_ptr
			llvm::Value *fnPtr = mBuilder->CreateExtractValue( fnPair, 0, "fn.ptr" );
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue( fnPair, 1, "fn.ctx" );

			// Build call: fn_ptr(ctx_ptr, args...)
			std::vector<llvm::Value*> callArgs;
			callArgs.push_back( ctxPtr );
			for ( auto &argExpr : expr->mArgs )
			{
				llvm::Value *argVal = genExpression( argExpr );
				if ( argVal == nullptr )
					return nullptr;
				callArgs.push_back( argVal );
			}

			// Build function type for the indirect call
			FunctionType *fnTypeDef = dynamic_cast<FunctionType*>( (Type*)fieldDef->getVariableType() );
			llvm::Type *retType = fnTypeDef->getReturnType() != nullptr
				? getLLVMType( fnTypeDef->getReturnType() )
				: llvm::Type::getVoidTy( *mContext );
			std::vector<llvm::Type*> paramTypes;
			paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) ); // ctx
			for ( int i = 0; i < fnTypeDef->getNumParamTypes(); i++ )
				paramTypes.push_back( getLLVMType( fnTypeDef->getParamType( i ) ) );

			llvm::FunctionType *callFnType = llvm::FunctionType::get( retType, paramTypes, false );

			if ( retType->isVoidTy() )
			{
				mBuilder->CreateCall( callFnType, fnPtr, callArgs );
				return nullptr;
			}
			return mBuilder->CreateCall( callFnType, fnPtr, callArgs, "fn.field.call" );
		}
	}

	if ( methodDef == nullptr )
		return nullptr;  // U3: unreachable for a concrete struct base — sema (Sema.cpp
		                 // resolveMethodCall) rejects unknown methods before codegen.
		                 // Kept as-is; loud-ICE hardening of codegen fallbacks is U4.

	// Look up the LLVM function for this method
	llvm::Function *llvmFunc = nullptr;
	auto it = mFunctionMap.find( methodDef );
	if ( it != mFunctionMap.end() )
		llvmFunc = it->second;

	if ( llvmFunc == nullptr )
	{
		// Try by mangled name: StructName_methodName
		string mangledName = structName + "_" + expr->mMethodName;
		llvmFunc = mModule->getFunction( mangledName );

		// Try in generic function map (for monomorphized generic struct methods)
		if ( llvmFunc == nullptr )
		{
			auto gIt = mGenericFunctionMap.find( mangledName );
			if ( gIt != mGenericFunctionMap.end() )
				llvmFunc = gIt->second;
		}

		// Also try with module namespace prefix: mod__StructName_methodName
		if ( llvmFunc == nullptr )
		{
			for ( auto &fn : *mModule )
			{
				string fname = fn.getName().str();
				if ( fname.size() > mangledName.size() + 2 &&
					 fname.substr( fname.size() - mangledName.size() ) == mangledName &&
					 fname[fname.size() - mangledName.size() - 1] == '_' &&
					 fname[fname.size() - mangledName.size() - 2] == '_' )
				{
					llvmFunc = &fn;
					break;
				}
			}
		}
	}

	if ( llvmFunc == nullptr )
		return nullptr;

	// Build arguments: self first (pass by pointer), then explicit args
	std::vector<llvm::Value*> args;

	// Generate self (pass pointer to struct data)
	llvm::AllocaInst *selfAddr = getExpressionAddress( expr->mObject );
	if ( selfAddr != nullptr )
	{
		if ( selfAddr->getAllocatedType()->isPointerTy() )
		{
			// Struct variable or self parameter: alloca holds a heap pointer,
			// load it and pass the heap pointer as self
			llvm::Value *heapPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), selfAddr, "self.heap" );
			args.push_back( heapPtr );
		}
		else
		{
			// Stack-allocated value (non-struct): pass address of alloca directly
			args.push_back( selfAddr );
		}
	}
	else
	{
		// If we can't get the address, generate the expression value.
		// For structs this produces a heap pointer; pass it directly.
		llvm::Value *selfVal = genExpression( expr->mObject );
		if ( selfVal == nullptr )
			return nullptr;
		args.push_back( selfVal );
	}

	// Generate explicit arguments
	for ( auto &argExpr : expr->mArgs )
	{
		llvm::Value *argVal = genExpression( argExpr );
		if ( argVal == nullptr )
			return nullptr;
		args.push_back( argVal );
	}

	if ( llvmFunc->getReturnType()->isVoidTy() )
	{
		mBuilder->CreateCall( llvmFunc, args );
		return nullptr;
	}

	llvm::Value *methodResult = mBuilder->CreateCall( llvmFunc, args, "methodcall" );

	// Track a struct-returning method result as a temporary — a fresh refcount-1
	// heap struct that must be released at statement end unless it is stored /
	// transferred (see genCallExpression for the full ownership rationale). This
	// covers chained method calls and struct-returning convenience methods.
	if ( methodDef->getReturnType() != nullptr )
	{
		string mRetName = methodDef->getReturnType()->getName();
		auto subIt = mTypeSubstitution.find( mRetName );
		if ( subIt != mTypeSubstitution.end() )
			mRetName = subIt->second->getName();
		if ( isUserStructType( mRetName ) )
			trackTempStruct( methodResult );
		// Track an Array<T>-returning method result as a temporary (e.g.
		// Buffer.get_bytes()): the caller receives an owned array reference that
		// must be released at statement end unless it is stored / transferred.
		else if ( mRetName == "Array" )
			trackTempArray( methodResult );
	}

	return methodResult;
}

llvm::Value *CodeGen::genBufferFieldAccess( FieldAccessExpression *expr )
{
	llvm::Value *bufVal = genExpression( expr->mObject );
	if ( bufVal == nullptr )
		return nullptr;

	if ( expr->mFieldName == "length" )
		return mBuilder->CreateCall( getOrDeclareBufferLength(), { bufVal }, "buf.len" );
	if ( expr->mFieldName == "capacity" )
		return mBuilder->CreateCall( getOrDeclareBufferCapacity(), { bufVal }, "buf.cap" );
	if ( expr->mFieldName == "is_empty" )
		return mBuilder->CreateCall( getOrDeclareBufferIsEmpty(), { bufVal }, "buf.empty" );

	return nullptr;
}

llvm::Value *CodeGen::genBufferMethodCall( MethodCallExpression *expr )
{
	llvm::Value *bufVal = genExpression( expr->mObject );
	if ( bufVal == nullptr )
		return nullptr;

	const string &method = expr->mMethodName;

	// No-arg property-like methods
	if ( method == "is_empty" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareBufferIsEmpty(), { bufVal }, "buf.empty" );
	if ( method == "length" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareBufferLength(), { bufVal }, "buf.len" );
	if ( method == "capacity" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareBufferCapacity(), { bufVal }, "buf.cap" );

	// get(index) -> int
	if ( method == "get" && expr->mArgs.size() == 1 )
	{
		llvm::Value *idxVal = genExpression( expr->mArgs[0] );
		if ( idxVal == nullptr )
			return nullptr;
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );
		return mBuilder->CreateCall( getOrDeclareBufferGet(), { bufVal, idxVal }, "buf.get" );
	}

	// set(index, value)
	if ( method == "set" && expr->mArgs.size() == 2 )
	{
		llvm::Value *idxVal = genExpression( expr->mArgs[0] );
		llvm::Value *valVal = genExpression( expr->mArgs[1] );
		if ( idxVal == nullptr || valVal == nullptr )
			return nullptr;
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );
		if ( !valVal->getType()->isIntegerTy( 32 ) )
			valVal = mBuilder->CreateIntCast( valVal,
				llvm::Type::getInt32Ty( *mContext ), true, "val.i32" );
		mBuilder->CreateCall( getOrDeclareBufferSet(), { bufVal, idxVal, valVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// append_byte(byte)
	if ( method == "append_byte" && expr->mArgs.size() == 1 )
	{
		llvm::Value *byteVal = genExpression( expr->mArgs[0] );
		if ( byteVal == nullptr )
			return nullptr;
		if ( !byteVal->getType()->isIntegerTy( 32 ) )
			byteVal = mBuilder->CreateIntCast( byteVal,
				llvm::Type::getInt32Ty( *mContext ), true, "byte.i32" );
		mBuilder->CreateCall( getOrDeclareBufferAppendByte(), { bufVal, byteVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// append_bytes(src, len)
	if ( method == "append_bytes" && expr->mArgs.size() == 2 )
	{
		llvm::Value *srcVal = genExpression( expr->mArgs[0] );
		llvm::Value *lenVal = genExpression( expr->mArgs[1] );
		if ( srcVal == nullptr || lenVal == nullptr )
			return nullptr;
		if ( !lenVal->getType()->isIntegerTy( 64 ) )
			lenVal = mBuilder->CreateSExt( lenVal,
				llvm::Type::getInt64Ty( *mContext ), "len.ext" );
		mBuilder->CreateCall( getOrDeclareBufferAppendBytes(), { bufVal, srcVal, lenVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// append_string(s)
	if ( method == "append_string" && expr->mArgs.size() == 1 )
	{
		llvm::Value *strVal = genExpression( expr->mArgs[0] );
		if ( strVal == nullptr )
			return nullptr;
		mBuilder->CreateCall( getOrDeclareBufferAppendString(), { bufVal, strVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// index_of(pattern, offset)
	if ( method == "index_of" && expr->mArgs.size() == 2 )
	{
		llvm::Value *patVal = genExpression( expr->mArgs[0] );
		llvm::Value *offVal = genExpression( expr->mArgs[1] );
		if ( patVal == nullptr || offVal == nullptr )
			return nullptr;
		if ( !offVal->getType()->isIntegerTy( 64 ) )
			offVal = mBuilder->CreateSExt( offVal,
				llvm::Type::getInt64Ty( *mContext ), "off.ext" );
		llvm::Value *result = mBuilder->CreateCall(
			getOrDeclareBufferIndexOf(), { bufVal, patVal, offVal }, "buf.indexOf" );
		// Truncate i64 to i32 for BLang int type
		return mBuilder->CreateTrunc( result, llvm::Type::getInt32Ty( *mContext ), "buf.indexOf.i32" );
	}

	// slice(start, end) -> Buffer
	if ( method == "slice" && expr->mArgs.size() == 2 )
	{
		llvm::Value *startVal = genExpression( expr->mArgs[0] );
		llvm::Value *endVal = genExpression( expr->mArgs[1] );
		if ( startVal == nullptr || endVal == nullptr )
			return nullptr;
		if ( !startVal->getType()->isIntegerTy( 64 ) )
			startVal = mBuilder->CreateSExt( startVal,
				llvm::Type::getInt64Ty( *mContext ), "start.ext" );
		if ( !endVal->getType()->isIntegerTy( 64 ) )
			endVal = mBuilder->CreateSExt( endVal,
				llvm::Type::getInt64Ty( *mContext ), "end.ext" );
		return mBuilder->CreateCall(
			getOrDeclareBufferSlice(), { bufVal, startVal, endVal }, "buf.slice" );
	}

	// to_string() -> string
	if ( method == "to_string" && expr->mArgs.empty() )
		return mBuilder->CreateCall( getOrDeclareBufferToString(), { bufVal }, "buf.tostr" );

	// to_string_range(start, end) -> string
	if ( method == "to_string_range" && expr->mArgs.size() == 2 )
	{
		llvm::Value *startVal = genExpression( expr->mArgs[0] );
		llvm::Value *endVal = genExpression( expr->mArgs[1] );
		if ( startVal == nullptr || endVal == nullptr )
			return nullptr;
		if ( !startVal->getType()->isIntegerTy( 64 ) )
			startVal = mBuilder->CreateSExt( startVal,
				llvm::Type::getInt64Ty( *mContext ), "start.ext" );
		if ( !endVal->getType()->isIntegerTy( 64 ) )
			endVal = mBuilder->CreateSExt( endVal,
				llvm::Type::getInt64Ty( *mContext ), "end.ext" );
		return mBuilder->CreateCall(
			getOrDeclareBufferToStringRange(), { bufVal, startVal, endVal }, "buf.tostrr" );
	}

	// clear()
	if ( method == "clear" && expr->mArgs.empty() )
	{
		mBuilder->CreateCall( getOrDeclareBufferClear(), { bufVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// compact(bytes)
	if ( method == "compact" && expr->mArgs.size() == 1 )
	{
		llvm::Value *bytesVal = genExpression( expr->mArgs[0] );
		if ( bytesVal == nullptr )
			return nullptr;
		if ( !bytesVal->getType()->isIntegerTy( 64 ) )
			bytesVal = mBuilder->CreateSExt( bytesVal,
				llvm::Type::getInt64Ty( *mContext ), "bytes.ext" );
		mBuilder->CreateCall( getOrDeclareBufferCompact(), { bufVal, bytesVal } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	return nullptr;
}
