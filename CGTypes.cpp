#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

#include <iostream>

using namespace QLang;
using namespace std;

std::string CodeGen::mangleGenericName(
	const std::string &baseName,
	const std::vector<SmartPtr<Type>> &typeArgs )
{
	std::string mangled = baseName;
	for ( auto &arg : typeArgs )
	{
		mangled += "_" + arg->getName();
	}
	return mangled;
}

llvm::Type *CodeGen::getLLVMType( Type *type )
{
	if ( type == nullptr )
		return llvm::Type::getVoidTy( *mContext );

	const std::string &name = type->getName();

	// Check type substitution first (active during generic instantiation)
	auto subIt = mTypeSubstitution.find( name );
	if ( subIt != mTypeSubstitution.end() )
		return getLLVMType( subIt->second );

	if ( name == "int" )
		return llvm::Type::getInt32Ty( *mContext );
	else if ( name == "char" )
		return llvm::Type::getInt8Ty( *mContext );
	else if ( name == "short" )
		return llvm::Type::getInt16Ty( *mContext );
	else if ( name == "long" )
		return llvm::Type::getInt64Ty( *mContext );
	else if ( name == "float" )
		return llvm::Type::getFloatTy( *mContext );
	else if ( name == "double" )
		return llvm::Type::getDoubleTy( *mContext );
	else if ( name == "string" )
		return llvm::PointerType::get( *mContext, 0 );
	else if ( name == "cstring" )
		return llvm::PointerType::get( *mContext, 0 );
	else if ( name == "void" )
		return llvm::Type::getVoidTy( *mContext );
	else if ( name == "bool" )
		return llvm::Type::getInt1Ty( *mContext );
	else if ( name == "byte" )
		return llvm::Type::getInt8Ty( *mContext );
	else if ( name == "Task" )
		return llvm::PointerType::get( *mContext, 0 );

	// Function type: {ptr fn_ptr, ptr ctx_ptr} callback pair
	if ( type->isFunctionType() )
	{
		return llvm::StructType::get( *mContext, {
			llvm::PointerType::get( *mContext, 0 ),
			llvm::PointerType::get( *mContext, 0 )
		} );
	}

	// Built-in Array<T> and carray types — opaque pointers
	if ( name == "Array" )
		return llvm::PointerType::get( *mContext, 0 );
	if ( name == "carray" )
		return llvm::PointerType::get( *mContext, 0 );

	// Check for generic type with type arguments (e.g., Box<int>)
	if ( type->getNumTypeParams() > 0 )
	{
		std::vector<SmartPtr<Type>> typeArgs;
		for ( int i = 0; i < type->getNumTypeParams(); i++ )
			typeArgs.push_back( type->getTypeParam( i ) );

		std::string mangledName = mangleGenericName( name, typeArgs );

		// Check if already instantiated — ensure layout is created
		auto instIt = mGenericInstanceMap.find( mangledName );
		if ( instIt != mGenericInstanceMap.end() )
			return llvm::PointerType::get( *mContext, 0 );

		// Look up the generic struct definition and instantiate
		auto defIt = mStructDefMap.find( name );
		if ( defIt != mStructDefMap.end() && defIt->second->isGeneric() )
		{
			instantiateGenericStruct( defIt->second, typeArgs );
			return llvm::PointerType::get( *mContext, 0 );
		}
	}

	// Check for known struct types — return ptr (structs are heap-allocated)
	auto structIt = mStructTypeMap.find( name );
	if ( structIt != mStructTypeMap.end() )
		return llvm::PointerType::get( *mContext, 0 );

	// Look up struct definitions registered during generate()
	auto defIt = mStructDefMap.find( name );
	if ( defIt != mStructDefMap.end() )
	{
		getOrCreateStructType( defIt->second );
		return llvm::PointerType::get( *mContext, 0 );
	}

	// Check for known enum types
	auto enumIt = mEnumDefMap.find( name );
	if ( enumIt != mEnumDefMap.end() )
	{
		if ( enumHasPayload( enumIt->second ) )
			return getOrCreateEnumType( enumIt->second );
		else
			return llvm::Type::getInt32Ty( *mContext );
	}

	// Fallback for unknown types (generics, unresolved)
	return llvm::Type::getInt32Ty( *mContext );
}

llvm::StructType *CodeGen::getOrCreateStructType( StructDefinition *structDef )
{
	auto it = mStructTypeMap.find( structDef->getName() );
	if ( it != mStructTypeMap.end() )
		return it->second;

	std::vector<llvm::Type*> fieldTypes;
	for ( auto &field : structDef->mFields )
	{
		fieldTypes.push_back( getLLVMType( field->getVariableType() ) );
	}

	// Handle empty structs (add a dummy byte)
	if ( fieldTypes.empty() )
		fieldTypes.push_back( llvm::Type::getInt8Ty( *mContext ) );

	llvm::StructType *st = llvm::StructType::create(
		*mContext, fieldTypes, structDef->getName() );
	mStructTypeMap[structDef->getName()] = st;
	return st;
}

bool CodeGen::enumHasPayload( EnumDefinition *enumDef )
{
	for ( auto &variant : enumDef->mVariants )
	{
		if ( !variant.mAssociatedTypes.empty() )
			return true;
	}
	return false;
}

uint64_t CodeGen::getEnumMaxPayloadSize( EnumDefinition *enumDef )
{
	uint64_t maxSize = 0;
	llvm::DataLayout dl( mModule.get() );

	for ( auto &variant : enumDef->mVariants )
	{
		uint64_t variantSize = 0;
		for ( auto &assocType : variant.mAssociatedTypes )
		{
			// A variant payload that is one of the enum's own generic parameters
			// (e.g. built-in Option<T>.some(T) / Result<T,E>.err(E)) is
			// type-erased — the concrete type is recovered at the match/construct
			// site, not baked into the enum layout. Reserve a pointer-sized
			// (8-byte) slot so any primitive, pointer, or heap-struct payload fits
			// (the Option/Result erased-payload contract). Without this the slot
			// defaults to int width (4 bytes) and an 8-byte string/pointer payload
			// is truncated, corrupting memory.
			bool isGenericParam = false;
			for ( auto &gp : enumDef->mGenericParams )
			{
				if ( gp.mName == assocType->getName() )
				{
					isGenericParam = true;
					break;
				}
			}
			if ( isGenericParam )
			{
				variantSize += 8;
				continue;
			}

			llvm::Type *llvmType = getLLVMType( assocType );
			uint64_t typeSize = dl.getTypeAllocSize( llvmType );
			// Fallback for zero-sized types
			if ( typeSize == 0 )
			{
				if ( llvmType->isIntegerTy( 32 ) ) typeSize = 4;
				else if ( llvmType->isIntegerTy( 64 ) ) typeSize = 8;
				else if ( llvmType->isPointerTy() ) typeSize = 8;
				else if ( llvmType->isFloatTy() ) typeSize = 4;
				else if ( llvmType->isDoubleTy() ) typeSize = 8;
				else typeSize = 4;
			}
			variantSize += typeSize;
		}
		if ( variantSize > maxSize )
			maxSize = variantSize;
	}

	// Minimum payload of 1 byte to avoid zero-sized arrays
	if ( maxSize == 0 )
		maxSize = 1;

	return maxSize;
}

llvm::StructType *CodeGen::getOrCreateEnumType( EnumDefinition *enumDef )
{
	auto it = mEnumTypeMap.find( enumDef->getName() );
	if ( it != mEnumTypeMap.end() )
		return it->second;

	uint64_t payloadSize = getEnumMaxPayloadSize( enumDef );

	// Layout: { i32 tag, [N x i8] payload }
	std::vector<llvm::Type*> fields;
	fields.push_back( llvm::Type::getInt32Ty( *mContext ) );
	fields.push_back( llvm::ArrayType::get(
		llvm::Type::getInt8Ty( *mContext ), payloadSize ) );

	llvm::StructType *st = llvm::StructType::create(
		*mContext, fields, "enum." + enumDef->getName() );
	mEnumTypeMap[enumDef->getName()] = st;
	return st;
}

llvm::StructType *CodeGen::instantiateGenericStruct(
	StructDefinition *genericDef,
	const std::vector<SmartPtr<Type>> &typeArgs )
{
	std::string mangledName = mangleGenericName( genericDef->getName(), typeArgs );

	// Check if already instantiated
	auto it = mGenericInstanceMap.find( mangledName );
	if ( it != mGenericInstanceMap.end() )
		return it->second;

	// Build substitution map: generic param name -> concrete type
	std::map<std::string, Type*> savedSub = mTypeSubstitution;
	for ( size_t i = 0; i < genericDef->mGenericParams.size() && i < typeArgs.size(); i++ )
	{
		SmartPtr<Type> arg = typeArgs[i];
		mTypeSubstitution[genericDef->mGenericParams[i].mName] = (Type*)arg;
	}

	// Create concrete field types by resolving through substitution
	std::vector<llvm::Type*> fieldTypes;
	for ( auto &field : genericDef->mFields )
	{
		fieldTypes.push_back( getLLVMType( field->getVariableType() ) );
	}

	// Handle empty structs
	if ( fieldTypes.empty() )
		fieldTypes.push_back( llvm::Type::getInt8Ty( *mContext ) );

	llvm::StructType *st = llvm::StructType::create(
		*mContext, fieldTypes, mangledName );
	mGenericInstanceMap[mangledName] = st;

	// Also register in mStructTypeMap so field access can find it
	mStructTypeMap[mangledName] = st;
	// Register the generic def under the mangled name so field lookups work
	mStructDefMap[mangledName] = genericDef;

	// Monomorphize methods for this generic struct instantiation
	// The substitution map is still active, so type params resolve correctly
	for ( auto &method : genericDef->mMethods )
	{
		// Build mangled method name: e.g. Box_int_get
		string methodMangledName = mangledName + "_" + method->getName();

		// Check if already generated
		if ( mModule->getFunction( methodMangledName ) != nullptr )
			continue;

		// Build concrete method type with substituted types
		llvm::Type *retType = getLLVMType( method->mReturnType );
		std::vector<llvm::Type*> paramTypes;
		for ( auto &param : method->mParameters )
		{
			if ( param->getVariableType() != nullptr &&
				 param->getVariableType()->getName() == "self" )
			{
				// self parameter is passed as opaque pointer
				paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
			}
			else
			{
				paramTypes.push_back( getLLVMType( param->getVariableType() ) );
			}
		}

		llvm::FunctionType *ft = llvm::FunctionType::get(
			retType, paramTypes, method->isVariadic() );
		llvm::Function *llvmFunc = llvm::Function::Create(
			ft, llvm::Function::ExternalLinkage, methodMangledName, mModule.get() );

		mGenericFunctionMap[methodMangledName] = llvmFunc;

		// Name parameters
		unsigned idx = 0;
		for ( auto &arg : llvmFunc->args() )
		{
			arg.setName( method->mParameters[idx]->getName() );
			idx++;
		}

		// Save and restore builder insert point
		llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
		llvm::BasicBlock::iterator savedPt;
		bool hadInsertPoint = ( savedBB != nullptr );
		if ( hadInsertPoint )
			savedPt = mBuilder->GetInsertPoint();

		// Save and restore variable map and scope stacks
		auto savedVarMap = mVariableMap;
		auto savedCurrentFunc = mCurrentFunction;
		auto savedResultAlloca = mResultAlloca;
		auto savedArcStack = mArcScopeStack;
		auto savedStringStack = mStringScopeStack;
		auto savedArrayStack = mArrayScopeStack;
		auto savedBufferStack = mBufferScopeStack;
		auto savedLambdaStack = mLambdaScopeStack;
		auto savedStructStack = mStructScopeStack;
		auto savedEnumStack = mEnumScopeStack;
		auto savedTempStrings = mTempStrings;
		auto savedTempLambdaCtxs = mTempLambdaCtxs;
		auto savedSelfStructMap = mSelfStructMap;
		auto savedSelfMangledName = mSelfStructMangledName;

		mCurrentFunction = method;
		mResultAlloca = nullptr;
		mArcScopeStack.clear();
		mStringScopeStack.clear();
		mArrayScopeStack.clear();
		mBufferScopeStack.clear();
		mLambdaScopeStack.clear();
		mStructScopeStack.clear();
		mEnumScopeStack.clear();
		mTempStrings.clear();
		mTempLambdaCtxs.clear();

		// Create entry block and generate body
		llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
			*mContext, "entry", llvmFunc );
		mBuilder->SetInsertPoint( entryBB );

		// Record self → struct mapping for field access
		for ( auto &param : method->mParameters )
		{
			if ( param->getVariableType() &&
				 param->getVariableType()->getName() == "self" )
			{
				mSelfStructMap[param] = genericDef;
				mSelfStructMangledName[param] = mangledName;
			}
		}

		// Create parameter allocas
		idx = 0;
		for ( auto &arg : llvmFunc->args() )
		{
			VariableDefinition *paramDef = method->mParameters[idx];
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				arg.getType(), nullptr, paramDef->getName() );
			mBuilder->CreateStore( &arg, alloca );
			mVariableMap[paramDef] = alloca;
			idx++;
		}

		// Generate method body
		if ( method->mFuncBody != nullptr )
			genBlock( method->mFuncBody );

		// Add implicit return
		llvm::BasicBlock *currentBB = mBuilder->GetInsertBlock();
		if ( currentBB->getTerminator() == nullptr )
		{
			if ( retType->isVoidTy() )
				mBuilder->CreateRetVoid();
			else
				mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
		}

		// Restore state
		mVariableMap = savedVarMap;
		mCurrentFunction = savedCurrentFunc;
		mResultAlloca = savedResultAlloca;
		mArcScopeStack = savedArcStack;
		mStringScopeStack = savedStringStack;
		mArrayScopeStack = savedArrayStack;
		mBufferScopeStack = savedBufferStack;
		mLambdaScopeStack = savedLambdaStack;
		mStructScopeStack = savedStructStack;
		mEnumScopeStack = savedEnumStack;
		mTempStrings = savedTempStrings;
		mTempLambdaCtxs = savedTempLambdaCtxs;
		mSelfStructMap = savedSelfStructMap;
		mSelfStructMangledName = savedSelfMangledName;

		if ( hadInsertPoint )
			mBuilder->SetInsertPoint( savedBB, savedPt );
	}

	// Restore substitution map
	mTypeSubstitution = savedSub;

	return st;
}

llvm::Function *CodeGen::instantiateGenericFunction(
	FunctionDefinition *genericDef,
	const std::vector<SmartPtr<Type>> &typeArgs )
{
	std::string mangledName = mangleGenericName( genericDef->getName(), typeArgs );

	// Check if already instantiated
	auto it = mGenericFunctionMap.find( mangledName );
	if ( it != mGenericFunctionMap.end() )
		return it->second;

	// Build substitution map
	std::map<std::string, Type*> savedSub = mTypeSubstitution;
	for ( size_t i = 0; i < genericDef->mGenericParams.size() && i < typeArgs.size(); i++ )
	{
		SmartPtr<Type> arg = typeArgs[i];
		mTypeSubstitution[genericDef->mGenericParams[i].mName] = (Type*)arg;
	}

	// Build the concrete function type
	llvm::Type *retType = getLLVMType( genericDef->mReturnType );

	std::vector<llvm::Type*> paramTypes;
	for ( auto &param : genericDef->mParameters )
	{
		paramTypes.push_back( getLLVMType( param->getVariableType() ) );
	}

	llvm::FunctionType *ft = llvm::FunctionType::get(
		retType, paramTypes, genericDef->isVariadic() );
	llvm::Function *llvmFunc = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, mangledName, mModule.get() );

	mGenericFunctionMap[mangledName] = llvmFunc;

	// Name the parameters
	unsigned idx = 0;
	for ( auto &arg : llvmFunc->args() )
	{
		arg.setName( genericDef->mParameters[idx]->getName() );
		idx++;
	}

	// Generate the function body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", llvmFunc );

	// Save and restore builder insert point
	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	llvm::BasicBlock::iterator savedPt;
	bool hadInsertPoint = ( savedBB != nullptr );
	if ( hadInsertPoint )
		savedPt = mBuilder->GetInsertPoint();

	mBuilder->SetInsertPoint( entryBB );

	// Save and restore variable map and scope stacks
	auto savedVarMap = mVariableMap;
	auto savedCurrentFunc = mCurrentFunction;
	auto savedResultAlloca = mResultAlloca;
	auto savedArcStack = mArcScopeStack;
	auto savedStringStack = mStringScopeStack;
	auto savedArrayStack = mArrayScopeStack;
	auto savedBufferStack = mBufferScopeStack;
	auto savedLambdaStack = mLambdaScopeStack;
	auto savedStructStack = mStructScopeStack;
	auto savedEnumStack = mEnumScopeStack;
	auto savedTempStrings = mTempStrings;
	auto savedTempLambdaCtxs = mTempLambdaCtxs;

	mCurrentFunction = genericDef;
	mResultAlloca = nullptr;
	mArcScopeStack.clear();
	mStringScopeStack.clear();
	mArrayScopeStack.clear();
	mBufferScopeStack.clear();
	mLambdaScopeStack.clear();
	mStructScopeStack.clear();
	mEnumScopeStack.clear();
	mTempStrings.clear();
	mTempLambdaCtxs.clear();

	// Create allocas for parameters
	idx = 0;
	for ( auto &arg : llvmFunc->args() )
	{
		VariableDefinition *paramDef = genericDef->mParameters[idx];
		llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
			arg.getType(), nullptr, paramDef->getName() );
		mBuilder->CreateStore( &arg, alloca );
		mVariableMap[paramDef] = alloca;
		idx++;
	}

	// Generate the body
	if ( genericDef->mFuncBody != nullptr )
		genBlock( genericDef->mFuncBody );

	// Add implicit return if needed
	llvm::BasicBlock *currentBB = mBuilder->GetInsertBlock();
	if ( currentBB->getTerminator() == nullptr )
	{
		if ( retType->isVoidTy() )
			mBuilder->CreateRetVoid();
		else
			mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
	}

	// Restore state
	mVariableMap = savedVarMap;
	mCurrentFunction = savedCurrentFunc;
	mResultAlloca = savedResultAlloca;
	mArcScopeStack = savedArcStack;
	mStringScopeStack = savedStringStack;
	mArrayScopeStack = savedArrayStack;
	mBufferScopeStack = savedBufferStack;
	mLambdaScopeStack = savedLambdaStack;
	mStructScopeStack = savedStructStack;
	mEnumScopeStack = savedEnumStack;
	mTempStrings = savedTempStrings;
	mTempLambdaCtxs = savedTempLambdaCtxs;
	mTypeSubstitution = savedSub;

	if ( hadInsertPoint )
		mBuilder->SetInsertPoint( savedBB, savedPt );

	return llvmFunc;
}

bool CodeGen::isUserStructType( const std::string &typeName )
{
	if ( typeName == "int" || typeName == "float" || typeName == "double" ||
		 typeName == "char" || typeName == "short" || typeName == "long" ||
		 typeName == "bool" || typeName == "byte" || typeName == "void" || typeName == "string" ||
		 typeName == "cstring" || typeName == "Array" ||
		 typeName == "carray" || typeName == "Task" || typeName == "self" )
		return false;
	return mStructDefMap.find( typeName ) != mStructDefMap.end();
}

bool CodeGen::isStringType( Expression *expr )
{
	if ( dynamic_cast<ConstString*>( expr ) )
		return true;
	if ( dynamic_cast<StringInterpolation*>( expr ) )
		return true;
	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		VariableDefinition *varDef = ve->mVariable;
		if ( varDef != nullptr && varDef->getVariableType() != nullptr )
		{
			string typeName = varDef->getVariableType()->getName();
			// Check substitution map for generic type params (e.g., K -> string)
			auto subIt = mTypeSubstitution.find( typeName );
			if ( subIt != mTypeSubstitution.end() )
				typeName = subIt->second->getName();
			if ( typeName == "string" )
				return true;
		}
	}
	if ( auto *ce = dynamic_cast<CallExpression*>( expr ) )
	{
		FunctionDefinition *funcDef = ce->mFunction;
		if ( funcDef != nullptr && funcDef->getReturnType() != nullptr &&
			 funcDef->getReturnType()->getName() == "string" )
			return true;
	}
	if ( auto *ops = dynamic_cast<OperationsExpression*>( expr ) )
	{
		// String concat produces a string
		if ( ops->mOperation == "+" && isStringType( ops->mOp1 ) )
			return true;
	}
	if ( auto *fa = dynamic_cast<FieldAccessExpression*>( expr ) )
	{
		// Use getFieldTypeName which handles self, generics, and substitution
		string typeName = getFieldTypeName( fa );
		if ( typeName == "string" )
			return true;
	}
	if ( auto *mc = dynamic_cast<MethodCallExpression*>( expr ) )
	{
		// String methods that return strings
		const string &method = mc->mMethodName;
		if ( isStringType( mc->mObject ) &&
			 ( method == "to_upper" || method == "to_lower" || method == "trim" ||
			   method == "substring" || method == "replace" || method == "concat" ) )
			return true;
	}
	// Check IndexExpression — array element type might be string
	if ( auto *ie = dynamic_cast<IndexExpression*>( expr ) )
	{
		if ( isArrayType( ie->mObject ) )
		{
			// Determine element type from Array<T>
			if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)ie->mObject ) )
			{
				Type *varType = ve->mVariable->getVariableType();
				if ( varType != nullptr && varType->getNumTypeParams() > 0 )
				{
					string elemName = varType->getTypeParam( 0 )->getName();
					auto subIt = mTypeSubstitution.find( elemName );
					if ( subIt != mTypeSubstitution.end() )
						elemName = subIt->second->getName();
					if ( elemName == "string" )
						return true;
				}
			}
			else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)ie->mObject ) )
			{
				Type *fieldType = getFieldType( fa );
				if ( fieldType != nullptr && fieldType->getNumTypeParams() > 0 )
				{
					string elemName = fieldType->getTypeParam( 0 )->getName();
					auto subIt = mTypeSubstitution.find( elemName );
					if ( subIt != mTypeSubstitution.end() )
						elemName = subIt->second->getName();
					if ( elemName == "string" )
						return true;
				}
			}
		}
	}
	return false;
}

// ---- Field type resolution helper ----

Type *CodeGen::getFieldType( FieldAccessExpression *fa )
{
	// U3 typed AST (FR-011): prefer the field type the semantic pass already
	// resolved and recorded on this node. Sema records only CONCRETE field
	// types (it leaves generic-parameter fields nullptr), so consuming the
	// annotation never bypasses the monomorphization substitution performed
	// below. When sema left it nullptr — e.g. self-based access, which U3 does
	// not resolve — fall through to the codegen-local derivation.
	if ( Type *resolved = fa->getResolvedType() )
		return resolved;

	// Determine the struct definition for the object
	StructDefinition *structDef = nullptr;
	string structName;

	if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)fa->mObject ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		if ( varType == nullptr )
			return nullptr;

		structName = varType->getName();

		// Check if this is a self parameter pointing to a struct
		auto selfIt = mSelfStructMap.find( ve->mVariable );
		if ( selfIt != mSelfStructMap.end() )
		{
			structDef = selfIt->second;

			// For generic struct methods, use the mangled name
			auto mangledIt = mSelfStructMangledName.find( ve->mVariable );
			if ( mangledIt != mSelfStructMangledName.end() )
				structName = mangledIt->second;
		}

		if ( structDef == nullptr )
		{
			// Handle generic struct types
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
	// Handle chained field access (e.g., self.inner.field)
	else if ( auto *innerFa = dynamic_cast<FieldAccessExpression*>( (Expression*)fa->mObject ) )
	{
		Type *innerType = getFieldType( innerFa );
		if ( innerType != nullptr )
		{
			structName = innerType->getName();
			auto subIt = mTypeSubstitution.find( structName );
			if ( subIt != mTypeSubstitution.end() )
				structName = subIt->second->getName();

			auto defIt = mStructDefMap.find( structName );
			if ( defIt != mStructDefMap.end() )
				structDef = defIt->second;
		}
	}

	if ( structDef == nullptr )
		return nullptr;

	// Find the field
	for ( auto &field : structDef->mFields )
	{
		if ( field->getName() == fa->mFieldName )
		{
			Type *fieldType = field->getVariableType();
			if ( fieldType != nullptr )
			{
				// Apply type substitution for generic fields
				string typeName = fieldType->getName();
				auto subIt = mTypeSubstitution.find( typeName );
				if ( subIt != mTypeSubstitution.end() )
					return subIt->second;
			}
			return fieldType;
		}
	}
	return nullptr;
}

string CodeGen::getFieldTypeName( FieldAccessExpression *fa )
{
	Type *t = getFieldType( fa );
	if ( t != nullptr )
		return t->getName();
	return "";
}

// ---- Array type helper ----

bool CodeGen::isArrayType( Expression *expr )
{
	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		if ( varType == nullptr )
			return false;
		string typeName = varType->getName();
		// Check substitution map for generic type params
		auto subIt = mTypeSubstitution.find( typeName );
		if ( subIt != mTypeSubstitution.end() )
			typeName = subIt->second->getName();
		return typeName == "Array";
	}
	if ( dynamic_cast<ArrayLiteralExpression*>( expr ) )
		return true;
	if ( auto *ce = dynamic_cast<CallExpression*>( expr ) )
	{
		FunctionDefinition *funcDef = ce->mFunction;
		if ( funcDef != nullptr && funcDef->getReturnType() != nullptr &&
			 funcDef->getReturnType()->getName() == "Array" )
			return true;
	}
	// Check FieldAccessExpression — field type might be Array
	if ( auto *fa = dynamic_cast<FieldAccessExpression*>( expr ) )
	{
		string typeName = getFieldTypeName( fa );
		return typeName == "Array";
	}
	return false;
}

int CodeGen::getElementSize( Type *elemType )
{
	llvm::Type *llvmType = getLLVMType( elemType );
	llvm::DataLayout dl( mModule.get() );
	return dl.getTypeAllocSize( llvmType );
}

bool CodeGen::isByteExpression( Expression *expr )
{
	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		return varType != nullptr && varType->getName() == "byte";
	}
	// Index into Array<byte>
	if ( auto *ie = dynamic_cast<IndexExpression*>( expr ) )
	{
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)ie->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getName() == "Array" &&
				 varType->getNumTypeParams() > 0 &&
				 varType->getTypeParam( 0 )->getName() == "byte" )
				return true;
		}
	}
	return false;
}

bool CodeGen::isBufferType( Expression *expr )
{
	// If Buffer is defined as a BLang struct (via stdlib/buffer.b),
	// don't use the old builtin codegen path — let struct codegen handle it.
	if ( mStructDefMap.find( "Buffer" ) != mStructDefMap.end() )
		return false;

	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		return varType != nullptr && varType->getName() == "Buffer";
	}
	if ( auto *ce = dynamic_cast<CallExpression*>( expr ) )
	{
		FunctionDefinition *funcDef = ce->mFunction;
		if ( funcDef != nullptr && funcDef->getReturnType() != nullptr &&
			 funcDef->getReturnType()->getName() == "Buffer" )
			return true;
	}
	if ( auto *mc = dynamic_cast<MethodCallExpression*>( expr ) )
	{
		if ( isBufferType( mc->mObject ) &&
			 ( mc->mMethodName == "slice" ) )
			return true;
	}
	return false;
}
