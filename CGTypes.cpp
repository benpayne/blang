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

	// Check type substitution first (active during generic instantiation).
	// Guard against a self-mapping (T -> T) — recursing on it never
	// terminates; fall through and treat the name as unresolved instead.
	auto subIt = mTypeSubstitution.find( name );
	if ( subIt != mTypeSubstitution.end() &&
		 subIt->second != nullptr && subIt->second->getName() != name )
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

	// Check for generic type with type arguments (e.g., Box<int>). Type
	// arguments are resolved through the ACTIVE substitution first, so a
	// `Pair<T>` mentioned inside a monomorphized body (T -> int) instantiates
	// Pair_int, not a bogus Pair_T.
	if ( type->getNumTypeParams() > 0 )
	{
		std::vector<SmartPtr<Type>> typeArgs;
		for ( int i = 0; i < type->getNumTypeParams(); i++ )
		{
			Type *arg = type->getTypeParam( i );
			auto argSub = mTypeSubstitution.find( arg->getName() );
			if ( argSub != mTypeSubstitution.end() && argSub->second != nullptr &&
				 argSub->second->getName() != arg->getName() )
				arg = argSub->second;
			typeArgs.push_back( arg );
		}

		std::string mangledName = mangleGenericName( name, typeArgs );

		// Check if already instantiated — ensure layout is created
		auto instIt = mGenericInstanceMap.find( mangledName );
		if ( instIt != mGenericInstanceMap.end() )
			return llvm::PointerType::get( *mContext, 0 );

		// Look up the generic struct definition and instantiate — unless any
		// argument is still the definition's own UNRESOLVED generic param
		// (e.g. a generic method's `-> Pair<T>` signature examined outside
		// any instantiation): instantiating Pair with its own param creates a
		// T -> T substitution and recurses forever. Structs lower to pointers
		// either way, so just return ptr and let a real instantiation happen
		// at a concrete use site.
		auto defIt = mStructDefMap.find( name );
		if ( defIt != mStructDefMap.end() && defIt->second->isGeneric() )
		{
			bool unresolved = false;
			for ( auto &gp : defIt->second->mGenericParams )
			{
				for ( auto &ta : typeArgs )
				{
					if ( ( (Type *)ta )->getName() == gp.mName )
					{
						unresolved = true;
						break;
					}
				}
				if ( unresolved )
					break;
			}
			if ( !unresolved )
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

			// A payload that names an enum is BOXED (stored as a pointer to a
			// heap-allocated child), so recursive enums have finite layout.
			// Must be checked before getLLVMType: sizing the enclosing enum's
			// own struct here would recurse forever.
			if ( mEnumDefMap.count( assocType->getName() ) != 0 )
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
		// linkonce_odr: a library and its consumers may each monomorphize the
		// same specialization (cross-module generics ship bodies in .bmod);
		// the linker keeps one copy instead of reporting a duplicate symbol.
		llvm::Function *llvmFunc = llvm::Function::Create(
			ft, llvm::Function::LinkOnceODRLinkage, methodMangledName, mModule.get() );

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
	// linkonce_odr: see instantiateGenericStruct's method instantiation — the
	// same specialization may exist in a library and its consumers.
	llvm::Function *llvmFunc = llvm::Function::Create(
		ft, llvm::Function::LinkOnceODRLinkage, mangledName, mModule.get() );

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
	if ( auto *mx = dynamic_cast<MatchExpression*>( expr ) )
		return mx->getResolvedType() != nullptr &&
			resolvedTypeName( mx->getResolvedType() ) == "string";
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
		// callReturnTypeName maps a generic call's declared return (e.g. "T")
		// through its (explicit or inferred) type arguments.
		if ( callReturnTypeName( ce ) == "string" )
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
		// Builtin string methods that return strings.
		const string &method = mc->mMethodName;
		if ( isStringType( mc->mObject ) &&
			 ( method == "to_upper" || method == "to_lower" || method == "trim" ||
			   method == "substring" || method == "replace" || method == "concat" ) )
			return true;
		// User-defined method whose resolved return type is `string` (B3): the
		// Sema pass annotates the method call with its return type. Without this,
		// `obj.method() == "x"` (a string-returning method used directly as a
		// comparison operand) would fall through to non-string (pointer/int)
		// comparison instead of __blang_string_equals.
		if ( Type *rt = mc->getResolvedType() )
		{
			string rtName = rt->getName();
			auto subIt = mTypeSubstitution.find( rtName );
			if ( subIt != mTypeSubstitution.end() )
				rtName = subIt->second->getName();
			if ( rtName == "string" )
				return true;
		}
		// Generic-struct instance method (Map<K,string>.get returning V):
		// map the declared return through the object's type arguments.
		if ( methodReturnTypeName( mc ) == "string" )
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

	Type *instanceType = nullptr;   // object's declared type WITH type args
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
				instanceType = varType;
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

				// Instance mapping at the CALLER (no substitution active): a
				// generic struct's field type mentions the struct's generic
				// params (Map<K,V>'s `Array<K> keys`); map them through the
				// object's declared type arguments so the caller sees the
				// concrete type (Array<string> for a Map<string,int> object).
				if ( instanceType != nullptr && structDef->isGeneric() )
				{
					Type *mapped = mapTypeForInstance(
						fieldType, structDef, instanceType );
					if ( mapped != nullptr )
						return mapped;
				}
			}
			return fieldType;
		}
	}
	return nullptr;
}

// Map a declared type from inside `structDef` to its concrete form for an
// instance typed `instanceType` — a generic param maps to the matching type
// argument; a compound type (Array<K>) is re-synthesized with mapped params.
// Returns nullptr when nothing needed mapping (caller keeps the declared type).
// Synthesized types are owned by mSyntheticTypes.
Type *CodeGen::mapTypeForInstance( Type *declared, StructDefinition *structDef,
	Type *instanceType )
{
	if ( declared == nullptr || structDef == nullptr || instanceType == nullptr )
		return nullptr;

	const auto &gps = structDef->getGenericParams();

	// Direct generic param: K -> instance arg.
	for ( size_t i = 0; i < gps.size() &&
		  (int)i < instanceType->getNumTypeParams(); i++ )
	{
		if ( gps[i].mName == declared->getName() )
			return instanceType->getTypeParam( (int)i );
	}

	// Compound type whose params mention generic params: Array<K>.
	if ( declared->getNumTypeParams() > 0 )
	{
		bool mappedAny = false;
		std::vector<Type *> params;
		for ( int p = 0; p < declared->getNumTypeParams(); p++ )
		{
			Type *sub = mapTypeForInstance(
				declared->getTypeParam( p ), structDef, instanceType );
			if ( sub != nullptr )
				mappedAny = true;
			params.push_back( sub != nullptr ? sub : declared->getTypeParam( p ) );
		}
		if ( mappedAny )
		{
			Type *synth = new Type( declared->getName() );
			for ( Type *p : params )
				synth->addTypeParam( p );
			mSyntheticTypes.push_back( synth );
			return synth;
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
	if ( auto *mx = dynamic_cast<MatchExpression*>( expr ) )
		return mx->getResolvedType() != nullptr &&
			resolvedTypeName( mx->getResolvedType() ) == "Array";
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
		// Maps a generic call's declared return ("T") through its type args.
		if ( callReturnTypeName( ce ) == "Array" )
			return true;
	}
	// Check FieldAccessExpression — field type might be Array
	if ( auto *fa = dynamic_cast<FieldAccessExpression*>( expr ) )
	{
		string typeName = getFieldTypeName( fa );
		return typeName == "Array";
	}
	// Method call whose (resolved or instance-mapped) return type is Array —
	// e.g. Map<string, Array<int>>.get returning V. Mirrors isStringType.
	if ( auto *mc = dynamic_cast<MethodCallExpression*>( expr ) )
	{
		if ( Type *rt = mc->getResolvedType() )
		{
			auto subIt = mTypeSubstitution.find( rt->getName() );
			string rtName = subIt != mTypeSubstitution.end()
				? subIt->second->getName() : rt->getName();
			if ( rtName == "Array" )
				return true;
		}
		if ( methodReturnTypeName( mc ) == "Array" )
			return true;
	}
	// Array element that is itself an Array — nested generics: grid[i], or a
	// generic struct method returning self.values[idx] where V=Array<...>.
	if ( auto *ie = dynamic_cast<IndexExpression*>( expr ) )
	{
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)ie->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 &&
				 resolvedTypeName( varType->getTypeParam( 0 ) ) == "Array" )
				return true;
		}
		else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)ie->mObject ) )
		{
			Type *fieldType = getFieldType( fa );
			if ( fieldType != nullptr && fieldType->getNumTypeParams() > 0 &&
				 resolvedTypeName( fieldType->getTypeParam( 0 ) ) == "Array" )
				return true;
		}
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

// ---------------------------------------------------------------------------
// Generic-context type resolution (generic ARC unit)
//
// ARC decisions (scope tracking, bind-retain, untrack, temp tracking, the
// isStringType/isArrayType predicates) key on declared type NAMES. Inside a
// monomorphized generic those names are the erased parameters (T/K/V), so the
// values were invisible to refcounting — the root cause of the sort<string> /
// refcounted-Map crashes and leaks. These helpers give every site one shared
// way to recover the concrete name.
// ---------------------------------------------------------------------------

std::string CodeGen::resolvedTypeName( Type *t )
{
	if ( t == nullptr )
		return "";
	auto it = mTypeSubstitution.find( t->getName() );
	return it != mTypeSubstitution.end() ? it->second->getName() : t->getName();
}

// The static type of an argument expression, for type-argument inference.
// Prefers the Sema annotation; falls back to declared variable types and
// literal kinds. May return null (unknown), never throws.
static Type *staticArgType( CodeGen *cg, Expression *e,
	std::vector<SmartPtr<Type>> &owned )
{
	(void)cg;
	if ( e == nullptr )
		return nullptr;
	if ( Type *rt = e->getResolvedType() )
		return rt;
	if ( auto *ve = dynamic_cast<VariableExpression*>( e ) )
	{
		if ( ve->getVariable() != nullptr )
			return ve->getVariable()->getVariableType();
	}
	if ( dynamic_cast<ConstString*>( e ) != nullptr ||
		 dynamic_cast<StringInterpolation*>( e ) != nullptr )
	{
		owned.push_back( new Type( "string" ) );
		return owned.back();
	}
	if ( dynamic_cast<ConstInteger*>( e ) != nullptr )
	{
		owned.push_back( new Type( "int" ) );
		return owned.back();
	}
	if ( dynamic_cast<ConstFloat*>( e ) != nullptr )
	{
		owned.push_back( new Type( "double" ) );
		return owned.back();
	}
	return nullptr;
}

// Bind `paramName` by structurally matching a declared parameter type against
// the argument's actual type: declared "T" binds directly; declared Array<T>
// against actual Array<string> binds T=string (recursing through matching
// outer names).
static void unifyTypeParam( const std::string &paramName, Type *declared,
	Type *actual, SmartPtr<Type> &binding )
{
	if ( declared == nullptr || actual == nullptr || binding != nullptr )
		return;
	if ( declared->getName() == paramName )
	{
		binding = actual;
		return;
	}
	if ( declared->getName() == actual->getName() )
	{
		int n = declared->getNumTypeParams() < actual->getNumTypeParams()
			? declared->getNumTypeParams() : actual->getNumTypeParams();
		for ( int i = 0; i < n; i++ )
			unifyTypeParam( paramName, declared->getTypeParam( i ),
				actual->getTypeParam( i ), binding );
	}
}

bool CodeGen::inferCallTypeArgs( CallExpression *call, FunctionDefinition *funcDef )
{
	if ( call == nullptr || funcDef == nullptr || !funcDef->isGeneric() )
		return false;
	if ( !call->mTypeArgs.empty() )
		return true;   // explicit args already present

	const auto &gps = funcDef->getGenericParams();
	std::vector<SmartPtr<Type>> bindings( gps.size() );

	for ( size_t g = 0; g < gps.size(); g++ )
	{
		for ( size_t a = 0; a < call->mParams.size() &&
			  a < (size_t)funcDef->getNumberParams(); a++ )
		{
			VariableDefinition *param = funcDef->getParam( (int)a );
			if ( param == nullptr )
				continue;
			Type *actual = staticArgType( this, call->mParams[a], mSyntheticTypes );
			unifyTypeParam( gps[g].mName, param->getVariableType(), actual,
				bindings[g] );
			if ( bindings[g] == nullptr )
				continue;

			// Reject a binding that is itself an UNRESOLVED generic param name
			// (e.g. a nested call to the same generic — its declared return
			// type is "T"). Accepting it stamps out a bogus largest_T instance
			// whose params fall back to i32, silently miscompiling pointer
			// instantiations (fine for int by accident, wrong for string).
			bool unresolvedName = false;
			for ( auto &gp2 : gps )
			{
				if ( bindings[g]->getName() == gp2.mName )
				{
					unresolvedName = true;
					break;
				}
			}
			if ( unresolvedName )
			{
				// Inside a monomorphized body the enclosing substitution may
				// resolve it (helper(x) where x: T under T -> string).
				auto s = mTypeSubstitution.find( bindings[g]->getName() );
				if ( s != mTypeSubstitution.end() && s->second != nullptr &&
					 s->second->getName() != bindings[g]->getName() )
				{
					bindings[g] = s->second;
					unresolvedName = false;
				}
			}
			if ( unresolvedName )
			{
				// A nested generic call: infer ITS type arguments recursively,
				// then map its declared return param to the inferred argument.
				auto *innerCall = dynamic_cast<CallExpression*>(
					(Expression *)call->mParams[a] );
				FunctionDefinition *innerDef =
					( innerCall != nullptr ) ? innerCall->mFunction : nullptr;
				if ( innerDef != nullptr && innerDef->isGeneric() &&
					 innerDef->getReturnType() != nullptr &&
					 inferCallTypeArgs( innerCall, innerDef ) )
				{
					const auto &igps = innerDef->getGenericParams();
					const string &rn = innerDef->getReturnType()->getName();
					for ( size_t ig = 0; ig < igps.size() &&
						  ig < innerCall->mTypeArgs.size(); ig++ )
					{
						if ( igps[ig].mName == rn )
						{
							bindings[g] = innerCall->mTypeArgs[ig];
							unresolvedName = false;
							break;
						}
					}
				}
			}
			if ( unresolvedName )
			{
				bindings[g] = nullptr;   // let a later concrete argument bind
				continue;
			}
			break;
		}
		if ( bindings[g] == nullptr )
			return false;   // this param could not be inferred
	}

	for ( auto &b : bindings )
	{
		mSyntheticTypes.push_back( b );
		call->mTypeArgs.push_back( b );
	}
	return true;
}

std::string CodeGen::callReturnTypeName( CallExpression *call )
{
	if ( call == nullptr || call->mFunction == nullptr )
		return "";
	FunctionDefinition *fd = call->mFunction;
	Type *rt = fd->getReturnType();
	if ( rt == nullptr )
		return "";
	std::string name = rt->getName();

	if ( fd->isGeneric() )
	{
		const auto &gps = fd->getGenericParams();
		for ( size_t i = 0; i < gps.size() && i < call->mTypeArgs.size(); i++ )
		{
			if ( gps[i].mName == name )
				return call->mTypeArgs[i]->getName();
		}
	}
	return resolvedTypeName( rt );
}

// The struct a RECEIVER expression denotes.
//
// The implicit `self` parameter's declared type name is the literal string
// "self" — it does not name the enclosing struct — so every predicate that
// resolves a receiver by looking its declared TYPE NAME up in mStructDefMap
// silently misses on a self receiver and reports "not a struct". That single
// fact produced two separate silent wrong answers (known-issues KI-10: `self`
// as a print argument reached the string runtime as a raw struct pointer;
// KI-8(b): `return self.describe()` was not recognised as a string return, so
// the returned string was released before it was returned). Resolving it in one
// place is what stops a third: mSelfStructMap holds the binding the parameter
// actually carries, including the definition a monomorphized generic method is
// being generated for.
StructDefinition *CodeGen::receiverStructDef( Expression *obj )
{
	if ( obj == nullptr )
		return nullptr;

	std::string typeName;
	if ( auto *ve = dynamic_cast<VariableExpression*>( obj ) )
	{
		auto selfIt = mSelfStructMap.find( ve->getVariable() );
		if ( selfIt != mSelfStructMap.end() && selfIt->second != nullptr )
			return selfIt->second;
		if ( ve->getVariable() != nullptr &&
			 ve->getVariable()->getVariableType() != nullptr )
			typeName = ve->getVariable()->getVariableType()->getName();
	}
	else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( obj ) )
	{
		// getFieldTypeName, not getResolvedType: Sema records only CONCRETE
		// field types and leaves a SELF-based access unannotated, so
		// `self.inner.describe()` and `"{self.inner}"` would otherwise resolve
		// to nothing — the same self-shaped hole one level down.
		typeName = getFieldTypeName( fa );
	}
	if ( typeName.empty() )
	{
		if ( Type *rt = obj->getResolvedType() )
			typeName = rt->getName();
	}
	if ( typeName.empty() )
		return nullptr;

	// A generic parameter (T) resolves through the active monomorphization
	// substitution before it can name a struct.
	auto subIt = mTypeSubstitution.find( typeName );
	if ( subIt != mTypeSubstitution.end() && subIt->second != nullptr )
		typeName = subIt->second->getName();

	// isUserStructType, not a bare map lookup: it is the single place that
	// excludes the builtin names a struct map may also carry.
	if ( !isUserStructType( typeName ) )
		return nullptr;
	auto it = mStructDefMap.find( typeName );
	return ( it != mStructDefMap.end() ) ? it->second : nullptr;
}

std::string CodeGen::methodReturnTypeName( MethodCallExpression *mc )
{
	if ( mc == nullptr )
		return "";

	// The object's declared/resolved type carries the instance's type args
	// (e.g. Map<string, Array<int>>). It is absent for a `self` receiver, whose
	// declared type is the placeholder "self" and carries no type arguments —
	// there the generic mapping below is skipped and the active substitution
	// (applied by resolvedTypeName) does the work instead.
	Type *objType = nullptr;
	if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)mc->mObject ) )
	{
		if ( ve->getVariable() != nullptr )
			objType = ve->getVariable()->getVariableType();
	}
	if ( objType == nullptr )
		objType = ( (Expression*)mc->mObject )->getResolvedType();

	// Find the GENERIC (base-name) struct definition and the method. Routed
	// through receiverStructDef so a `self` receiver resolves at all.
	StructDefinition *sd = receiverStructDef( (Expression*)mc->mObject );
	if ( sd == nullptr )
		return "";

	FunctionDefinition *methodDef = nullptr;
	for ( auto &m : sd->mMethods )
	{
		if ( m->getName() == mc->mMethodName )
		{
			methodDef = m;
			break;
		}
	}
	if ( methodDef == nullptr || methodDef->getReturnType() == nullptr )
		return "";

	std::string name = methodDef->getReturnType()->getName();

	// Map the struct's generic params through the object's type arguments:
	// Map<K,V>.get declared V, object Map<string,int> -> "int".
	const auto &gps = sd->getGenericParams();
	for ( size_t i = 0; objType != nullptr && i < gps.size() &&
		  (int)i < objType->getNumTypeParams(); i++ )
	{
		if ( gps[i].mName == name )
			return objType->getTypeParam( (int)i )->getName();
	}
	return resolvedTypeName( methodDef->getReturnType() );
}
