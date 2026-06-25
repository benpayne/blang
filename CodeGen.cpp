#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>

using namespace QLang;
using namespace std;

CodeGen::CodeGen( const std::string &moduleName )
{
	mContext = std::make_unique<llvm::LLVMContext>();
	mModule = std::make_unique<llvm::Module>( moduleName, *mContext );
	mBuilder = std::make_unique<llvm::IRBuilder<>>( *mContext );
}

CodeGen::~CodeGen()
{
}

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

	// Built-in Array<T>, Buffer, and carray types — opaque pointers
	if ( name == "Array" )
		return llvm::PointerType::get( *mContext, 0 );
	if ( name == "Buffer" )
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

bool CodeGen::generate( Module *mod )
{
	// Store the module scope and pointer for type resolution and SQL gen
	mScope = mod->mScope;
	mQLangModule = mod;

	// Register all struct definitions and create LLVM struct types
	for ( auto &structDef : mod->mStructList )
	{
		mStructDefMap[structDef->getName()] = structDef;
		if ( !structDef->isGeneric() )
			getOrCreateStructType( structDef );
	}

	// Register all enum definitions
	for ( auto &enumDef : mod->mEnumList )
	{
		mEnumDefMap[enumDef->getName()] = enumDef;
	}

	// Forward-declare all module-level functions so that methods/lambdas
	// can reference them before they are fully generated below.
	for ( auto &func : mod->mFunctionList )
	{
		if ( func->isGeneric() )
			continue;

		// Build LLVM function type — must expand fn-typed params into
		// (fn_ptr, ctx_ptr) pairs, matching genFunction's ABI.
		llvm::Type *retType = getLLVMType( func->mReturnType );
		std::vector<llvm::Type*> paramTypes;
		for ( auto &param : func->mParameters )
		{
			if ( param->getVariableType()->isFunctionType() )
			{
				paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
				paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
			}
			else
			{
				paramTypes.push_back( getLLVMType( param->getVariableType() ) );
			}
		}

		bool isMainFunc = ( func->getName() == "main" );
		if ( isMainFunc && paramTypes.empty() )
		{
			paramTypes.push_back( llvm::Type::getInt32Ty( *mContext ) );
			paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
		}

		llvm::FunctionType *ft = llvm::FunctionType::get(
			retType, paramTypes, func->isVariadic() );

		std::string llvmFuncName = func->getName();
		if ( !mModulePrefix.empty() && !isMainFunc && !func->isExtern() )
			llvmFuncName = mModulePrefix + "__" + func->getName();

		llvm::Function *llvmFunc = mModule->getFunction( llvmFuncName );
		if ( llvmFunc == nullptr )
		{
			llvmFunc = llvm::Function::Create(
				ft, llvm::Function::ExternalLinkage, llvmFuncName, mModule.get() );
		}

		mFunctionMap[func] = llvmFunc;
	}

	// Generate methods from impl blocks as regular LLVM functions
	for ( auto &structDef : mod->mStructList )
	{
		// Skip generic structs — their methods are monomorphized
		// lazily during instantiateGenericStruct()
		if ( structDef->isGeneric() )
			continue;

		for ( auto &method : structDef->mMethods )
		{
			// Skip generic methods
			if ( method->isGeneric() )
				continue;

			// Generate the method with a mangled name: StructName_methodName
			// Apply module prefix if set (e.g. "net__Socket_read")
			string mangledName;
			if ( !mModulePrefix.empty() )
				mangledName = mModulePrefix + "__" + structDef->getName() + "_" + method->getName();
			else
				mangledName = structDef->getName() + "_" + method->getName();

			// Build the function type
			llvm::Type *retType = getLLVMType( method->mReturnType );
			std::vector<llvm::Type*> paramTypes;
			for ( auto &param : method->mParameters )
			{
				// Handle 'self' parameter — pass as pointer to struct
				if ( param->getVariableType() != nullptr &&
					 param->getVariableType()->getName() == "self" )
				{
					paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
					mSelfStructMap[param] = structDef;
				}
				else
				{
					paramTypes.push_back( getLLVMType( param->getVariableType() ) );
				}
			}

			llvm::FunctionType *ft = llvm::FunctionType::get(
				retType, paramTypes, method->isVariadic() );

			// Reuse existing function if already declared (combine mode)
			llvm::Function *llvmFunc = mModule->getFunction( mangledName );
			if ( llvmFunc == nullptr )
			{
				llvmFunc = llvm::Function::Create(
					ft, llvm::Function::ExternalLinkage, mangledName, mModule.get() );
			}

			mFunctionMap[method] = llvmFunc;

			// Name the parameters
			unsigned idx = 0;
			for ( auto &arg : llvmFunc->args() )
			{
				arg.setName( method->mParameters[idx]->getName() );
				idx++;
			}

			// Create the entry basic block
			llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
				*mContext, "entry", llvmFunc );
			mBuilder->SetInsertPoint( entryBB );

			// Create allocas for parameters
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

			// Generate the method body
			if ( method->mFuncBody != nullptr )
				genBlock( method->mFuncBody );

			// Add implicit return if needed
			llvm::BasicBlock *currentBB = mBuilder->GetInsertBlock();
			if ( currentBB->getTerminator() == nullptr )
			{
				if ( retType->isVoidTy() )
					mBuilder->CreateRetVoid();
				else
					mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
			}

			mVariableMap.clear();
			mMovedVariables.clear();
		}
	}

	// Generate to_json/from_json for @json annotated structs
	for ( auto &structDef : mod->mStructList )
	{
		for ( const auto &ann : structDef->getAnnotations() )
		{
			if ( ann.mName == "json" )
			{
				if ( !genJsonToJson( structDef ) )
					return false;
				if ( !genJsonFromJson( structDef ) )
					return false;
				break;
			}
		}
	}

	// Scan module for concurrency features to determine if runtime init/shutdown is needed
	for ( auto &func : mod->mFunctionList )
	{
		if ( func->isAsync() )
		{
			mUsesConcurrency = true;
			break;
		}
	}
	// Also check for spawn or shared/sync usage (set during codegen)

	// Generate top-level functions
	for ( auto &func : mod->mFunctionList )
	{
		// Skip generic functions — they're templates, not concrete code
		if ( func->isGeneric() )
			continue;

		if ( genFunction( func ) == nullptr )
			return false;
	}

	// Generate test blocks as callable functions
	std::vector<llvm::Function*> testFunctions;
	for ( auto &testBlock : mod->mTestBlocks )
	{
		llvm::Function *testFunc = genTestBlock( testBlock );
		if ( testFunc != nullptr )
			testFunctions.push_back( testFunc );
	}

	// Generate test runner function if there are tests
	if ( !testFunctions.empty() )
	{
		genTestRunner( testFunctions, mod->mTestBlocks );
	}

	// Return false if any ownership or other codegen errors occurred
	if ( mHasError )
		return false;

	return true;
}

void CodeGen::registerExternalTypes(
	const std::vector<SmartPtr<StructDefinition>> &structs,
	const std::vector<SmartPtr<EnumDefinition>> &enums )
{
	for ( auto &structDef : structs )
	{
		StructDefinition *sd = const_cast<StructDefinition*>( (const StructDefinition*)structDef );
		if ( mStructDefMap.find( sd->getName() ) == mStructDefMap.end() )
		{
			mStructDefMap[sd->getName()] = sd;
			if ( !sd->isGeneric() )
				getOrCreateStructType( sd );
		}
	}

	for ( auto &enumDef : enums )
	{
		EnumDefinition *ed = const_cast<EnumDefinition*>( (const EnumDefinition*)enumDef );
		if ( mEnumDefMap.find( ed->getName() ) == mEnumDefMap.end() )
			mEnumDefMap[ed->getName()] = ed;
	}
}

void CodeGen::print( llvm::raw_ostream &os )
{
	mModule->print( os, nullptr );
}

bool CodeGen::verify()
{
	std::string errStr;
	llvm::raw_string_ostream errStream( errStr );

	if ( llvm::verifyModule( *mModule, &errStream ) )
	{
		cerr << "Module verification failed:" << endl;
		cerr << errStr << endl;
		return false;
	}
	return true;
}

llvm::Function *CodeGen::genFunction( FunctionDefinition *func )
{
	// Build the function type
	llvm::Type *retType = getLLVMType( func->mReturnType );

	// Track which BLang params expand to two LLVM params (fn-typed callbacks)
	std::vector<int> fnTypeParamIndices;
	std::vector<llvm::Type*> paramTypes;
	for ( int pi = 0; pi < (int)func->mParameters.size(); pi++ )
	{
		auto &param = func->mParameters[pi];
		if ( param->getVariableType()->isFunctionType() )
		{
			// Expand to (fn_ptr, ctx_ptr) pair
			paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
			paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) );
			fnTypeParamIndices.push_back( pi );
		}
		else
		{
			paramTypes.push_back( getLLVMType( param->getVariableType() ) );
		}
	}

	// For main(): override LLVM signature to main(i32 argc, i8** argv) so
	// the C runtime receives command-line arguments for sys.args support.
	bool isMainFunc = ( func->getName() == "main" );
	if ( isMainFunc && paramTypes.empty() )
	{
		paramTypes.push_back( llvm::Type::getInt32Ty( *mContext ) );   // argc
		paramTypes.push_back( llvm::PointerType::get( *mContext, 0 ) ); // argv
	}

	llvm::FunctionType *ft = llvm::FunctionType::get( retType, paramTypes, func->isVariadic() );

	// Apply module prefix for namespace mangling (e.g. "sys" → "sys__funcName")
	// Extern functions keep their original names (they reference C runtime symbols)
	std::string llvmFuncName = func->getName();
	if ( !mModulePrefix.empty() && !isMainFunc && !func->isExtern() )
		llvmFuncName = mModulePrefix + "__" + func->getName();

	// In combine mode, a function may already exist from a previous module.
	// Reuse the existing declaration instead of creating a duplicate.
	llvm::Function *llvmFunc = mModule->getFunction( llvmFuncName );
	if ( llvmFunc == nullptr )
	{
		llvmFunc = llvm::Function::Create(
			ft, llvm::Function::ExternalLinkage, llvmFuncName, mModule.get() );
	}

	// Name the argc/argv args for main
	if ( isMainFunc && llvmFunc->arg_size() >= 2 )
	{
		llvmFunc->getArg( llvmFunc->arg_size() - 2 )->setName( "argc" );
		llvmFunc->getArg( llvmFunc->arg_size() - 1 )->setName( "argv" );
	}

	// Store the mapping
	mFunctionMap[func] = llvmFunc;

	// Extern functions are declarations only — no body
	if ( func->isExtern() )
		return llvmFunc;

	// Async functions: create a void*(void*) wrapper and generate the body inside it
	if ( func->isAsync() )
	{
		mUsesConcurrency = true;

		// Create a wrapper function: void* __blang_async_wrapper_name(void* arg)
		string wrapperName = "__blang_async_wrapper_" + func->getName();
		llvm::FunctionType *wrapperType = llvm::FunctionType::get(
			llvm::PointerType::get( *mContext, 0 ),
			{ llvm::PointerType::get( *mContext, 0 ) },
			false );
		llvm::Function *wrapperFn = llvm::Function::Create(
			wrapperType, llvm::Function::InternalLinkage, wrapperName, mModule.get() );
		wrapperFn->getArg( 0 )->setName( "arg" );

		// Generate the wrapper body
		llvm::BasicBlock *wrapEntry = llvm::BasicBlock::Create(
			*mContext, "entry", wrapperFn );
		mBuilder->SetInsertPoint( wrapEntry );

		// Track current function
		mCurrentFunction = func;
		mResultAlloca = nullptr;

		// Unpack parameters from a context struct if the function has params
		if ( !func->mParameters.empty() )
		{
			// Create a context struct for parameters
			std::vector<llvm::Type*> paramTypesForCtx;
			for ( auto &param : func->mParameters )
				paramTypesForCtx.push_back( getLLVMType( param->getVariableType() ) );

			llvm::StructType *ctxType = llvm::StructType::create(
				*mContext, paramTypesForCtx, func->getName() + ".async.ctx" );

			llvm::Value *ctxPtr = wrapperFn->getArg( 0 );
			unsigned pidx = 0;
			for ( auto &param : func->mParameters )
			{
				llvm::Type *pType = getLLVMType( param->getVariableType() );
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					pType, nullptr, param->getName() );
				llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
					ctxType, ctxPtr, pidx, "async.param" );
				llvm::Value *val = mBuilder->CreateLoad( pType, fieldPtr, param->getName() );
				mBuilder->CreateStore( val, alloca );
				mVariableMap[param] = alloca;
				pidx++;
			}
		}

		// Set up async wrapper context so return statements store + branch
		llvm::BasicBlock *asyncExitBB = llvm::BasicBlock::Create(
			*mContext, "async.exit", wrapperFn );

		llvm::AllocaInst *asyncResultAlloca = nullptr;
		if ( !retType->isVoidTy() )
		{
			asyncResultAlloca = mBuilder->CreateAlloca( retType, nullptr, "async.result" );
			mBuilder->CreateStore( llvm::Constant::getNullValue( retType ), asyncResultAlloca );
		}

		mAsyncResultAlloca = asyncResultAlloca;
		mAsyncExitBB = asyncExitBB;
		mAsyncReturnType = retType;

		// Generate the function body
		if ( func->mFuncBody != nullptr )
			genBlock( func->mFuncBody );

		// Fall through to the exit block if body doesn't terminate
		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( asyncExitBB );

		// Generate the exit block: box result and return void*
		mBuilder->SetInsertPoint( asyncExitBB );
		if ( asyncResultAlloca != nullptr )
		{
			// Box the return value: allocate heap memory, store value, return ptr
			llvm::DataLayout dl( mModule.get() );
			uint64_t typeSize = dl.getTypeAllocSize( retType );
			llvm::Function *mallocFn = getOrDeclareBlangAlloc();
			llvm::Value *boxPtr = mBuilder->CreateCall( mallocFn,
				{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), typeSize ) },
				"async.box" );
			llvm::Value *resultVal = mBuilder->CreateLoad( retType, asyncResultAlloca, "res" );
			mBuilder->CreateStore( resultVal, boxPtr );
			mBuilder->CreateRet( boxPtr );
		}
		else
		{
			llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
				llvm::PointerType::get( *mContext, 0 ) );
			mBuilder->CreateRet( nullPtr );
		}

		// Clear async context
		mAsyncResultAlloca = nullptr;
		mAsyncExitBB = nullptr;
		mAsyncReturnType = nullptr;

		// Now generate the public function that calls __blang_async_call(wrapper, args)
		llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
			*mContext, "entry", llvmFunc );
		mBuilder->SetInsertPoint( entryBB );

		mVariableMap.clear();
		mMovedVariables.clear();

		// Create parameter allocas for the public function
		unsigned idx = 0;
		for ( auto &arg : llvmFunc->args() )
		{
			VariableDefinition *paramDef = func->mParameters[idx];
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				arg.getType(), nullptr, paramDef->getName() );
			mBuilder->CreateStore( &arg, alloca );
			mVariableMap[paramDef] = alloca;
			idx++;
		}

		// Pack parameters into context struct and call __blang_async_call
		llvm::Value *ctxArg = llvm::ConstantPointerNull::get(
			llvm::PointerType::get( *mContext, 0 ) );

		if ( !func->mParameters.empty() )
		{
			std::vector<llvm::Type*> paramTypesForCtx;
			for ( auto &param : func->mParameters )
				paramTypesForCtx.push_back( getLLVMType( param->getVariableType() ) );

			llvm::StructType *ctxType = llvm::StructType::create(
				*mContext, paramTypesForCtx, func->getName() + ".call.ctx" );

			llvm::DataLayout dl( mModule.get() );
			uint64_t ctxSize = dl.getTypeAllocSize( ctxType );
			llvm::Function *mallocFn = getOrDeclareBlangAlloc();
			ctxArg = mBuilder->CreateCall( mallocFn,
				{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), ctxSize ) },
				"async.ctx" );

			unsigned pidx = 0;
			for ( auto &param : func->mParameters )
			{
				llvm::AllocaInst *alloca = mVariableMap[param];
				llvm::Type *pType = getLLVMType( param->getVariableType() );
				llvm::Value *val = mBuilder->CreateLoad( pType, alloca, "param.val" );
				llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
					ctxType, ctxArg, pidx, "ctx.store" );
				mBuilder->CreateStore( val, fieldPtr );
				pidx++;
			}
		}

		// Call __blang_async_call(wrapper, ctx) -> BlangTask*
		llvm::Value *taskPtr = mBuilder->CreateCall(
			getOrDeclareAsyncCall(), { wrapperFn, ctxArg }, "task" );

		// Return the task pointer (or void)
		if ( retType->isVoidTy() )
		{
			mBuilder->CreateRetVoid();
		}
		else
		{
			// For now return 0 — proper return value would need boxing
			mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
		}

		mCurrentFunction = nullptr;
		mResultAlloca = nullptr;
		mVariableMap.clear();
		mMovedVariables.clear();
		return llvmFunc;
	}

	// Name the LLVM arguments (fn-type params expand to two args)
	{
		unsigned llvmArgIdx = 0;
		for ( int pi = 0; pi < (int)func->mParameters.size(); pi++ )
		{
			if ( func->mParameters[pi]->getVariableType()->isFunctionType() )
			{
				llvmFunc->getArg( llvmArgIdx )->setName( func->mParameters[pi]->getName() + ".fn" );
				llvmFunc->getArg( llvmArgIdx + 1 )->setName( func->mParameters[pi]->getName() + ".ctx" );
				llvmArgIdx += 2;
			}
			else
			{
				llvmFunc->getArg( llvmArgIdx )->setName( func->mParameters[pi]->getName() );
				llvmArgIdx++;
			}
		}
	}

	// Create the entry basic block
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", llvmFunc );
	mBuilder->SetInsertPoint( entryBB );

	// Track current function for contract support
	mCurrentFunction = func;
	mResultAlloca = nullptr;

	// Create allocas for parameters and store the argument values
	// fn-type params: combine two LLVM args into a {ptr, ptr} alloca
	{
		unsigned llvmArgIdx = 0;
		for ( int pi = 0; pi < (int)func->mParameters.size(); pi++ )
		{
			VariableDefinition *paramDef = func->mParameters[pi];
			if ( paramDef->getVariableType()->isFunctionType() )
			{
				llvm::Type *pairType = getLLVMType( paramDef->getVariableType() );
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					pairType, nullptr, paramDef->getName() );
				llvm::Value *fnPtr = llvmFunc->getArg( llvmArgIdx );
				llvm::Value *ctxPtr = llvmFunc->getArg( llvmArgIdx + 1 );
				llvm::Value *pair = llvm::UndefValue::get( pairType );
				pair = mBuilder->CreateInsertValue( pair, fnPtr, 0 );
				pair = mBuilder->CreateInsertValue( pair, ctxPtr, 1 );
				mBuilder->CreateStore( pair, alloca );
				mVariableMap[paramDef] = alloca;
				llvmArgIdx += 2;
			}
			else
			{
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					llvmFunc->getArg( llvmArgIdx )->getType(), nullptr, paramDef->getName() );
				mBuilder->CreateStore( llvmFunc->getArg( llvmArgIdx ), alloca );
				mVariableMap[paramDef] = alloca;
				llvmArgIdx++;
			}
		}
	}

	// Push a function-level scope for parameter tracking. This scope is
	// released in genReturnStatement and popped after genBlock returns.
	mStringScopeStack.push_back( {} );
	mArrayScopeStack.push_back( {} );
	mBufferScopeStack.push_back( {} );

	// Track own-qualified refcounted parameters for release at function exit.
	// own parameters transfer ownership to the function, which must release them.
	for ( int pi = 0; pi < (int)func->mParameters.size(); pi++ )
	{
		VariableDefinition *paramDef = func->mParameters[pi];
		if ( paramDef->getOwnership() != OwnershipQualifier::kOwnership_Own )
			continue;
		Type *pType = paramDef->getVariableType();
		if ( pType == nullptr )
			continue;
		auto it = mVariableMap.find( paramDef );
		if ( it == mVariableMap.end() )
			continue;
		llvm::AllocaInst *alloca = it->second;
		string ptName = pType->getName();
		if ( ptName == "string" && !mStringScopeStack.empty() )
			mStringScopeStack.back().push_back( { alloca, paramDef } );
		else if ( ptName == "Array" && !mArrayScopeStack.empty() )
			mArrayScopeStack.back().push_back( { alloca, paramDef } );
		else if ( ptName == "Buffer" && !mBufferScopeStack.empty() )
			mBufferScopeStack.back().push_back( { alloca, paramDef } );
	}

	// If function has ensures clauses and a return value, create result alloca
	if ( func->hasEnsures() && !retType->isVoidTy() )
	{
		mResultAlloca = mBuilder->CreateAlloca( retType, nullptr, "result" );
		mBuilder->CreateStore( llvm::Constant::getNullValue( retType ), mResultAlloca );

		// Register the result variable so ensures expressions can reference it
		Symbol *resultSym = func->mFuncScope->findSymbol( "result" );
		if ( auto *resultVar = dynamic_cast<VariableDefinition*>( resultSym ) )
			mVariableMap[resultVar] = mResultAlloca;
	}

	// Track whether this is main() — we may need to inject runtime init/shutdown
	bool isMain = ( func->getName() == "main" );
	bool concurrencyBefore = mUsesConcurrency;

	// Reserve a basic block for runtime init (filled in after body generation
	// so we know whether concurrency features are actually used)
	llvm::BasicBlock *initBB = nullptr;
	llvm::BasicBlock *bodyBB = nullptr;
	if ( isMain )
	{
		initBB = mBuilder->GetInsertBlock(); // current entry block
		bodyBB = llvm::BasicBlock::Create( *mContext, "body", llvmFunc );
		mBuilder->CreateBr( bodyBB );
		mBuilder->SetInsertPoint( bodyBB );
	}

	// Generate requires (precondition) checks at function entry
	for ( auto &clause : func->mRequiresClauses )
	{
		genContractCheck( clause, "Precondition violated" );
	}

	// Generate the function body
	if ( func->mFuncBody != nullptr )
	{
		genBlock( func->mFuncBody );
	}

	// Inject init calls into main's entry block (before the branch to body)
	if ( isMain )
	{
		llvm::Instruction *brInst = initBB->getTerminator();
		mBuilder->SetInsertPoint( brInst );

		// Always capture argc/argv for sys.args
		mBuilder->CreateCall( getOrDeclareSysInit(),
			{ llvmFunc->getArg( llvmFunc->arg_size() - 2 ),
			  llvmFunc->getArg( llvmFunc->arg_size() - 1 ) } );

		// Runtime init only if concurrency features were discovered
		if ( mUsesConcurrency && !concurrencyBefore )
		{
			mBuilder->CreateCall( getOrDeclareRuntimeInit(),
				{ llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 4 ) } );
		}

		// Restore insert point to after the body
		llvm::BasicBlock *afterBody = &llvmFunc->back();
		mBuilder->SetInsertPoint( afterBody );
	}

	// If the function is void and the last block has no terminator, add ret void
	llvm::BasicBlock *currentBB = mBuilder->GetInsertBlock();
	if ( currentBB->getTerminator() == nullptr )
	{
		// Generate ensures (postcondition) checks before implicit return
		for ( auto &clause : func->mEnsuresClauses )
		{
			genContractCheck( clause, "Postcondition violated" );
		}

		// Insert runtime shutdown for main() if concurrency features are used
		if ( isMain && mUsesConcurrency )
			mBuilder->CreateCall( getOrDeclareRuntimeShutdown(), {} );

		if ( retType->isVoidTy() )
		{
			mBuilder->CreateRetVoid();
		}
		else if ( mResultAlloca != nullptr )
		{
			llvm::Value *resultVal = mBuilder->CreateLoad( retType, mResultAlloca, "result.val" );
			mBuilder->CreateRet( resultVal );
		}
		else
		{
			// Implicit return 0 for non-void functions without explicit return
			mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
		}
	}

	// Pop function-level parameter scope
	if ( !mStringScopeStack.empty() ) mStringScopeStack.pop_back();
	if ( !mArrayScopeStack.empty() ) mArrayScopeStack.pop_back();
	if ( !mBufferScopeStack.empty() ) mBufferScopeStack.pop_back();

	// Clear function context
	mCurrentFunction = nullptr;
	mResultAlloca = nullptr;
	mVariableMap.clear();
	mMovedVariables.clear();

	return llvmFunc;
}

void CodeGen::genBlock( Block *block )
{
	// Push a new ARC scope to track shared/sync variables declared in this block
	mArcScopeStack.push_back( {} );
	mStringScopeStack.push_back( {} );
	mArrayScopeStack.push_back( {} );
	mBufferScopeStack.push_back( {} );
	mLambdaScopeStack.push_back( {} );
	mStructScopeStack.push_back( {} );
	mEnumScopeStack.push_back( {} );

	for ( auto &stmt : block->mStatementList )
	{
		if ( stmt != nullptr )
		{
			// Don't generate code after a terminator (unreachable code)
			if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
				break;
			genStatement( stmt );
			// Release any temporary strings created during this statement
			releaseTempStrings();
			// Release any inline lambda contexts created during this statement
			releaseTempLambdaCtxs();
		}
	}

	// Emit release calls for all shared/sync variables declared in this scope
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
	{
		for ( auto *alloca : mArcScopeStack.back() )
		{
			llvm::Value *heapPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "rc.rel.ptr" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { heapPtr } );
		}

		// Release string variables declared in this scope (skip moved vars)
		for ( auto &entry : mStringScopeStack.back() )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *strPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "str.rel.ptr" );
			mBuilder->CreateCall( getOrDeclareStringRelease(), { strPtr } );
		}

		// Release array variables declared in this scope (skip moved vars)
		for ( auto &entry : mArrayScopeStack.back() )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *arrPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "arr.rel.ptr" );
			mBuilder->CreateCall( getOrDeclareArrayRelease(), { arrPtr } );
		}

		// Release buffer variables declared in this scope (skip moved vars)
		for ( auto &entry : mBufferScopeStack.back() )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *bufPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "buf.rel.ptr" );
			mBuilder->CreateCall( getOrDeclareBufferRelease(), { bufPtr } );
		}

		// Release lambda/fn-typed variable contexts declared in this scope
		for ( auto &entry : mLambdaScopeStack.back() )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			// Load the {fn_ptr, ctx_ptr} pair, extract ctx, release it
			llvm::Type *pairType = llvm::StructType::get( *mContext, {
				llvm::PointerType::get( *mContext, 0 ),
				llvm::PointerType::get( *mContext, 0 )
			} );
			llvm::Value *pairVal = mBuilder->CreateLoad(
				pairType, entry.first, "fn.rel.pair" );
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
				pairVal, 1, "fn.rel.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRelease(), { ctxPtr } );
		}

		// Release heap-allocated struct variables declared in this scope.
		// The destructor (set at allocation time) releases refcounted fields
		// when the refcount reaches zero.
		for ( auto *structAlloca : mStructScopeStack.back() )
		{
			llvm::Value *structPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), structAlloca, "struct.rel.ptr" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { structPtr } );
		}

		// Release refcounted payloads in enum variables declared in this scope
		for ( auto &entry : mEnumScopeStack.back() )
		{
			emitEnumPayloadRelease( entry.first, entry.second );
		}
	}

	mArcScopeStack.pop_back();
	mStringScopeStack.pop_back();
	mArrayScopeStack.pop_back();
	mBufferScopeStack.pop_back();
	mLambdaScopeStack.pop_back();
	mStructScopeStack.pop_back();
	mEnumScopeStack.pop_back();
}

bool CodeGen::isUserStructType( const std::string &typeName )
{
	if ( typeName == "int" || typeName == "float" || typeName == "double" ||
		 typeName == "char" || typeName == "short" || typeName == "long" ||
		 typeName == "bool" || typeName == "void" || typeName == "string" ||
		 typeName == "cstring" || typeName == "Array" || typeName == "Buffer" ||
		 typeName == "carray" || typeName == "Task" || typeName == "self" )
		return false;
	return mStructDefMap.find( typeName ) != mStructDefMap.end();
}

void CodeGen::emitEnumPayloadRelease( llvm::AllocaInst *alloca, EnumDefinition *enumDef )
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
			string atn = at->getName();
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
			string atn = at->getName();
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

void CodeGen::trackTempString( llvm::Value *val )
{
	if ( val != nullptr )
		mTempStrings.push_back( val );
}

void CodeGen::releaseTempStrings()
{
	if ( mTempStrings.empty() )
		return;
	if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
	{
		mTempStrings.clear();
		return;
	}
	for ( auto *val : mTempStrings )
		mBuilder->CreateCall( getOrDeclareStringRelease(), { val } );
	mTempStrings.clear();
}

void CodeGen::untrackTempString( llvm::Value *val )
{
	for ( auto it = mTempStrings.begin(); it != mTempStrings.end(); ++it )
	{
		if ( *it == val )
		{
			mTempStrings.erase( it );
			return;
		}
	}
}

void CodeGen::trackTempLambdaCtx( llvm::Value *ctxPtr )
{
	if ( ctxPtr != nullptr )
		mTempLambdaCtxs.push_back( ctxPtr );
}

void CodeGen::releaseTempLambdaCtxs()
{
	if ( mTempLambdaCtxs.empty() )
		return;
	if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
	{
		mTempLambdaCtxs.clear();
		return;
	}
	for ( auto *val : mTempLambdaCtxs )
		mBuilder->CreateCall( getOrDeclareLambdaCtxRelease(), { val } );
	mTempLambdaCtxs.clear();
}

void CodeGen::untrackTempLambdaCtx( llvm::Value *ctxPtr )
{
	for ( auto it = mTempLambdaCtxs.begin(); it != mTempLambdaCtxs.end(); ++it )
	{
		if ( *it == ctxPtr )
		{
			mTempLambdaCtxs.erase( it );
			return;
		}
	}
}

void CodeGen::genStatement( Statement *stmt )
{
	if ( stmt == nullptr )
		return;

	// Don't generate code after a terminator
	if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
		return;

	if ( auto *block = dynamic_cast<Block*>( stmt ) )
	{
		genBlock( block );
	}
	else if ( auto *ret = dynamic_cast<ReturnStatement*>( stmt ) )
	{
		genReturnStatement( ret );
	}
	else if ( auto *ifStmt = dynamic_cast<IfStatement*>( stmt ) )
	{
		genIfStatement( ifStmt );
	}
	else if ( auto *whileStmt = dynamic_cast<WhileStatement*>( stmt ) )
	{
		genWhileStatement( whileStmt );
	}
	else if ( auto *forStmt = dynamic_cast<ForStatement*>( stmt ) )
	{
		genForStatement( forStmt );
	}
	else if ( auto *forIn = dynamic_cast<ForInStatement*>( stmt ) )
	{
		genForInStatement( forIn );
	}
	else if ( auto *spawn = dynamic_cast<SpawnStatement*>( stmt ) )
	{
		genSpawnStatement( spawn );
	}
	else if ( auto *waitStmt = dynamic_cast<WaitStatement*>( stmt ) )
	{
		genWaitStatement( waitStmt );
	}
	else if ( auto *waitAllStmt = dynamic_cast<WaitAllStatement*>( stmt ) )
	{
		genWaitAllStatement( waitAllStmt );
	}
	else if ( auto *assertStmt = dynamic_cast<AssertStatement*>( stmt ) )
	{
		genAssertStatement( assertStmt );
	}
	else if ( auto *handler = dynamic_cast<EventHandler*>( stmt ) )
	{
		genEventHandler( handler );
	}
	else if ( dynamic_cast<BreakStatement*>( stmt ) )
	{
		genBreakStatement();
	}
	else if ( dynamic_cast<ContinueStatement*>( stmt ) )
	{
		genContinueStatement();
	}
	else if ( auto *varDecl = dynamic_cast<VariableDeclaration*>( stmt ) )
	{
		genVariableDeclaration( varDecl );
	}
	else if ( auto *expr = dynamic_cast<Expression*>( stmt ) )
	{
		genExpression( expr );
	}
}

void CodeGen::genVariableDeclaration( VariableDeclaration *decl )
{
	for ( auto &data : decl->mVariables )
	{
		VariableDefinition *varDef = data.mVaribale;
		llvm::Type *llvmType = getLLVMType( varDef->getVariableType() );
		OwnershipQualifier ownership = varDef->getOwnership();

		// Check for channel type: chan<T>
		Type *varType = varDef->getVariableType();
		if ( varType != nullptr && varType->getName() == "chan" )
		{
			// chan<T> -> __blang_chan_create(sizeof(T), capacity)
			llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				ptrType, nullptr, varDef->getName() );
			mVariableMap[varDef] = alloca;

			// Determine element size from type parameter
			uint64_t elemSize = 4; // default to int-sized
			if ( varType->getNumTypeParams() > 0 )
			{
				llvm::Type *elemType = getLLVMType( varType->getTypeParam( 0 ) );
				llvm::DataLayout dl( mModule.get() );
				elemSize = dl.getTypeAllocSize( elemType );
			}

			// Default capacity of 16
			llvm::Value *sizeVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), elemSize );
			llvm::Value *capVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), 16 );

			llvm::Value *chanPtr = mBuilder->CreateCall(
				getOrDeclareChanCreate(), { sizeVal, capVal }, "chan.ptr" );
			mBuilder->CreateStore( chanPtr, alloca );

			mUsesConcurrency = true;
			continue;
		}

		if ( ownership == OwnershipQualifier::kOwnership_Shared ||
			 ownership == OwnershipQualifier::kOwnership_Sync )
		{
			// Check if this is a user-defined struct type — structs are already
			// heap-allocated with ARC, so we just store the struct ptr directly.
			bool isStructOwnership = false;
			if ( varType != nullptr )
			{
				string stn = varType->getName();
				auto sub2 = mTypeSubstitution.find( stn );
				if ( sub2 != mTypeSubstitution.end() )
					stn = sub2->second->getName();
				isStructOwnership = isUserStructType( stn );
				if ( !isStructOwnership && varType->getNumTypeParams() > 0 )
				{
					std::vector<SmartPtr<Type>> ta;
					for ( int i = 0; i < varType->getNumTypeParams(); i++ )
						ta.push_back( varType->getTypeParam( i ) );
					string mangledStn = mangleGenericName( stn, ta );
					isStructOwnership = ( mStructDefMap.find( mangledStn ) != mStructDefMap.end() );
				}
			}

			if ( isStructOwnership )
			{
				// Struct types are already heap-allocated with their own ARC.
				// Just store the struct ptr in the alloca and track for release.
				llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					ptrType, nullptr, varDef->getName() );
				mVariableMap[varDef] = alloca;

				if ( !mStructScopeStack.empty() )
					mStructScopeStack.back().push_back( alloca );

				if ( data.mInitialValue != nullptr )
				{
					llvm::Value *initVal = genExpression( data.mInitialValue );
					if ( initVal != nullptr )
						mBuilder->CreateStore( initVal, alloca );
				}
			}
			else
			{
				// Non-struct types: heap-allocate via runtime ARC.
				// The alloca stores a pointer (opaque ptr) to the heap data.
				llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
				llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
					ptrType, nullptr, varDef->getName() );
				mVariableMap[varDef] = alloca;

				// Track for ARC release at scope exit
				if ( !mArcScopeStack.empty() )
					mArcScopeStack.back().push_back( alloca );

				// Determine data size
				llvm::DataLayout dl( mModule.get() );
				uint64_t dataSize = dl.getTypeAllocSize( llvmType );
				llvm::Value *sizeVal = llvm::ConstantInt::get(
					llvm::Type::getInt64Ty( *mContext ), dataSize );

				// Call __blang_rc_alloc or __blang_rc_alloc_sync
				llvm::Function *allocFn = ( ownership == OwnershipQualifier::kOwnership_Sync )
					? getOrDeclareRcAllocSync() : getOrDeclareRcAlloc();
				llvm::Value *heapPtr = mBuilder->CreateCall( allocFn, { sizeVal }, "rc.ptr" );
				mBuilder->CreateStore( heapPtr, alloca );

				// If there's an initializer, generate and store through the heap pointer
				if ( data.mInitialValue != nullptr )
				{
					llvm::Value *initVal = genExpression( data.mInitialValue );
					if ( initVal != nullptr )
					{
						if ( initVal->getType() != llvmType )
						{
							if ( llvmType->isIntegerTy() && initVal->getType()->isIntegerTy() )
								initVal = mBuilder->CreateIntCast( initVal, llvmType, true, "icast" );
						}
						if ( initVal != nullptr )
						{
							// Store through the heap pointer
							mBuilder->CreateStore( initVal, heapPtr );
						}
					}
				}
			}
		}
		else
		{
			// Value type or own: stack allocation (same as before)
			// For user-defined struct types, the alloca stores a heap pointer (ptr)
			// since getLLVMType returns ptr for structs.
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				llvmType, nullptr, varDef->getName() );
			mVariableMap[varDef] = alloca;

			// Check if this is a user-defined struct type (heap-allocated by reference)
			bool isStructVar = false;
			if ( varType != nullptr )
			{
				string sTypeName = varType->getName();
				auto subIt2 = mTypeSubstitution.find( sTypeName );
				if ( subIt2 != mTypeSubstitution.end() )
					sTypeName = subIt2->second->getName();
				isStructVar = isUserStructType( sTypeName );
				if ( !isStructVar && varType->getNumTypeParams() > 0 )
				{
					std::vector<SmartPtr<Type>> typeArgs;
					for ( int tpi = 0; tpi < varType->getNumTypeParams(); tpi++ )
						typeArgs.push_back( varType->getTypeParam( tpi ) );
					string mangledTypeName = mangleGenericName( sTypeName, typeArgs );
					isStructVar = ( mStructDefMap.find( mangledTypeName ) != mStructDefMap.end() );
				}
			}

			// Track struct variables for __blang_rc_release at scope exit
			if ( isStructVar && !mStructScopeStack.empty() )
			{
				mStructScopeStack.back().push_back( alloca );
			}

			// Track string variables for release at scope exit
			if ( varType != nullptr && varType->getName() == "string" &&
				 !mStringScopeStack.empty() )
			{
				mStringScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track array variables for release at scope exit
			if ( varType != nullptr && varType->getName() == "Array" &&
				 !mArrayScopeStack.empty() )
			{
				mArrayScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track buffer variables for release at scope exit
			if ( varType != nullptr && varType->getName() == "Buffer" &&
				 !mBufferScopeStack.empty() )
			{
				mBufferScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track fn-typed variables for lambda context release at scope exit
			if ( varType != nullptr && varType->isFunctionType() &&
				 !mLambdaScopeStack.empty() )
			{
				mLambdaScopeStack.back().push_back( { alloca, varDef } );
			}

			// Track enum variables with refcounted payloads for cleanup at scope exit
			if ( varType != nullptr && !mEnumScopeStack.empty() )
			{
				string enumTypeName = varType->getName();
				auto enumIt = mEnumDefMap.find( enumTypeName );
				if ( enumIt != mEnumDefMap.end() )
				{
					// Check if any variant has a refcounted payload
					EnumDefinition *ed = enumIt->second;
					bool hasRefPayload = false;
					for ( auto &variant : ed->mVariants )
					{
						for ( auto &assocType : variant.mAssociatedTypes )
						{
							string atn = assocType->getName();
							if ( atn == "string" || atn == "Array" || atn == "Buffer" ||
								 isUserStructType( atn ) )
							{
								hasRefPayload = true;
								break;
							}
						}
						if ( hasRefPayload ) break;
					}
					if ( hasRefPayload )
						mEnumScopeStack.back().push_back( { alloca, ed } );
				}
			}

			// If there's an initializer, generate it and store
			if ( data.mInitialValue != nullptr )
			{
				// For Array<T> declarations with empty literal initializer [],
				// set the element type hint so genArrayLiteral uses the correct
				// element size (e.g. 8 bytes for string/pointer types, not default 4)
				if ( varType != nullptr && varType->getName() == "Array" &&
					 varType->getNumTypeParams() > 0 &&
					 dynamic_cast<ArrayLiteralExpression*>( (Expression*)data.mInitialValue ) != nullptr )
				{
					Type *elemType = varType->getTypeParam( 0 );
					mArrayElemTypeHint = getLLVMType( elemType );
					string etn = elemType->getName();
					auto subEtn = mTypeSubstitution.find( etn );
					if ( subEtn != mTypeSubstitution.end() )
						etn = subEtn->second->getName();
					mArrayElemTypeNameHint = etn;
				}

				llvm::Value *initVal = genExpression( data.mInitialValue );
				if ( initVal != nullptr )
				{
					// For struct variables initialized from another variable or field
					// access, retain the reference (the source keeps its own reference).
					// For new allocations (struct literal, function return), the refcount
					// is already 1 (ownership transfer, no retain needed).
					if ( isStructVar )
					{
						auto *srcVarExpr = dynamic_cast<VariableExpression*>(
							(Expression*)data.mInitialValue );
						auto *srcFieldExpr = dynamic_cast<FieldAccessExpression*>(
							(Expression*)data.mInitialValue );
						if ( srcVarExpr != nullptr || srcFieldExpr != nullptr )
						{
							mBuilder->CreateCall( getOrDeclareRcRetain(), { initVal } );
						}
					}

					// Cast if types don't match (skip for struct ptrs which are already ptr)
					if ( !isStructVar && initVal->getType() != llvmType )
					{
						if ( llvmType->isIntegerTy() && initVal->getType()->isIntegerTy() )
							initVal = mBuilder->CreateIntCast( initVal, llvmType, true, "icast" );
						else if ( llvmType->isFloatTy() && initVal->getType()->isDoubleTy() )
							initVal = mBuilder->CreateFPTrunc( initVal, llvmType, "fptrunc" );
						else if ( llvmType->isDoubleTy() && initVal->getType()->isFloatTy() )
							initVal = mBuilder->CreateFPExt( initVal, llvmType, "fpext" );
						else
							initVal = nullptr;
					}
					if ( initVal != nullptr )
					{
						mBuilder->CreateStore( initVal, alloca );

						// If storing a string, untrack it from temps — the variable now owns it
						if ( varType != nullptr && varType->getName() == "string" )
							untrackTempString( initVal );

						// If storing a fn-typed value, untrack its lambda context from temps.
						// The variable now owns the context via mLambdaScopeStack.
						if ( varType != nullptr && varType->isFunctionType() &&
							 !mTempLambdaCtxs.empty() )
						{
							mTempLambdaCtxs.pop_back();
						}
					}
				}

				// Move semantics: if this is an own variable initialized from
				// another own variable, mark the source as moved
				if ( ownership == OwnershipQualifier::kOwnership_Own )
				{
					auto *srcVarExpr = dynamic_cast<VariableExpression*>( (Expression*)data.mInitialValue );
					if ( srcVarExpr != nullptr )
					{
						VariableDefinition *srcDef = srcVarExpr->getVariable();
						if ( srcDef->getOwnership() == OwnershipQualifier::kOwnership_Own )
						{
							if ( mInsideLoop )
							{
								cerr << "Error: cannot move own variable '" << srcDef->getName()
									 << "' inside a loop (would move on each iteration)" << endl;
								mHasError = true;
								return;
							}
							mMovedVariables.insert( srcDef );
						}
					}
				}
			}
		}
	}
}

void CodeGen::genReturnStatement( ReturnStatement *ret )
{
	// Generate the return value FIRST, before releasing scope variables.
	// This prevents use-after-free when returning a string/array variable
	// that would be released by the scope cleanup below.
	llvm::Value *retVal = nullptr;
	if ( ret->mExpression != nullptr )
	{
		retVal = genExpression( ret->mExpression );

		// If returning a string, retain it so scope release doesn't free it
		if ( retVal != nullptr && isStringType( ret->mExpression ) )
			mBuilder->CreateCall( getOrDeclareStringRetain(), { retVal } );

		// If returning an array, retain it so scope release doesn't free it
		if ( retVal != nullptr && isArrayType( ret->mExpression ) )
			mBuilder->CreateCall( getOrDeclareArrayRetain(), { retVal } );

		// If returning a fn-typed value (lambda/callback pair), retain the
		// context so scope cleanup doesn't free it before the caller gets it
		if ( retVal != nullptr && mCurrentFunction != nullptr &&
			 mCurrentFunction->getReturnType() != nullptr &&
			 mCurrentFunction->getReturnType()->isFunctionType() )
		{
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
				retVal, 1, "ret.fn.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRetain(), { ctxPtr } );
		}

		// If returning a heap-allocated struct, retain the pointer so it
		// survives the scope cleanup below (which will release the local reference).
		// Only retain when the source expression is a variable or field access —
		// those are tracked in scope stacks and will be released at scope exit.
		// New allocations (struct literals, function call results) already have
		// refcount=1 and transfer ownership directly to the caller.
		if ( retVal != nullptr && mCurrentFunction != nullptr &&
			 mCurrentFunction->getReturnType() != nullptr )
		{
			string retTypeName = mCurrentFunction->getReturnType()->getName();
			if ( isUserStructType( retTypeName ) )
			{
				bool needsRetain = false;
				Expression *retRawExpr = (Expression *)ret->mExpression;
				auto *retExpr = dynamic_cast<VariableExpression*>( retRawExpr );
				auto *retField = dynamic_cast<FieldAccessExpression*>( retRawExpr );
				if ( retExpr != nullptr || retField != nullptr )
					needsRetain = true;
				if ( needsRetain )
					mBuilder->CreateCall( getOrDeclareRcRetain(), { retVal } );
			}
		}
	}

	// Release temporary strings created during expression evaluation
	releaseTempStrings();

	// Insert runtime shutdown before ARC releases in main() — threads must
	// finish before we free shared/sync memory they may be using
	if ( mCurrentFunction != nullptr && mCurrentFunction->getName() == "main" && mUsesConcurrency )
	{
		mBuilder->CreateCall( getOrDeclareRuntimeShutdown(), {} );
	}

	// Emit ARC releases for all in-scope shared/sync variables before returning
	for ( auto it = mArcScopeStack.rbegin(); it != mArcScopeStack.rend(); ++it )
	{
		for ( auto *alloca : *it )
		{
			llvm::Value *heapPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "rc.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { heapPtr } );
		}
	}

	// Release string variables before returning (skip moved vars)
	for ( auto it = mStringScopeStack.rbegin(); it != mStringScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *strPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "str.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareStringRelease(), { strPtr } );
		}
	}

	// Release array variables before returning (skip moved vars)
	for ( auto it = mArrayScopeStack.rbegin(); it != mArrayScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *arrPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "arr.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareArrayRelease(), { arrPtr } );
		}
	}

	// Release buffer variables before returning (skip moved vars)
	for ( auto it = mBufferScopeStack.rbegin(); it != mBufferScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Value *bufPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), entry.first, "buf.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareBufferRelease(), { bufPtr } );
		}
	}

	// Release lambda/fn-typed variable contexts before returning (skip moved vars)
	for ( auto it = mLambdaScopeStack.rbegin(); it != mLambdaScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			if ( entry.second != nullptr && mMovedVariables.count( entry.second ) )
				continue;
			llvm::Type *pairType = llvm::StructType::get( *mContext, {
				llvm::PointerType::get( *mContext, 0 ),
				llvm::PointerType::get( *mContext, 0 )
			} );
			llvm::Value *pairVal = mBuilder->CreateLoad(
				pairType, entry.first, "fn.ret.pair" );
			llvm::Value *ctxPtr = mBuilder->CreateExtractValue(
				pairVal, 1, "fn.ret.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRelease(), { ctxPtr } );
		}
	}

	// Release heap-allocated struct variables before returning
	for ( auto it = mStructScopeStack.rbegin(); it != mStructScopeStack.rend(); ++it )
	{
		for ( auto *structAlloca : *it )
		{
			llvm::Value *structPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), structAlloca, "struct.ret.ptr" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { structPtr } );
		}
	}

	// Release enum variables with refcounted payloads before returning
	for ( auto it = mEnumScopeStack.rbegin(); it != mEnumScopeStack.rend(); ++it )
	{
		for ( auto &entry : *it )
		{
			emitEnumPayloadRelease( entry.first, entry.second );
		}
	}

	// In an async wrapper, return statements store the value and branch to exit
	if ( mAsyncExitBB != nullptr )
	{
		if ( retVal != nullptr && mAsyncResultAlloca != nullptr )
		{
			// Cast if needed
			if ( retVal->getType() != mAsyncReturnType )
			{
				if ( mAsyncReturnType->isIntegerTy() && retVal->getType()->isIntegerTy() )
					retVal = mBuilder->CreateIntCast( retVal, mAsyncReturnType, true, "icast" );
			}
			mBuilder->CreateStore( retVal, mAsyncResultAlloca );
		}
		mBuilder->CreateBr( mAsyncExitBB );
		return;
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::Type *expectedType = func->getReturnType();

	if ( retVal != nullptr )
	{
		// Cast if the value type doesn't match the function return type
		if ( retVal->getType() != expectedType )
		{
			if ( expectedType->isFloatTy() && retVal->getType()->isDoubleTy() )
				retVal = mBuilder->CreateFPTrunc( retVal, expectedType, "fptrunc" );
			else if ( expectedType->isDoubleTy() && retVal->getType()->isFloatTy() )
				retVal = mBuilder->CreateFPExt( retVal, expectedType, "fpext" );
			else if ( expectedType->isIntegerTy() && retVal->getType()->isIntegerTy() )
				retVal = mBuilder->CreateIntCast( retVal, expectedType, true, "icast" );
			else if ( expectedType->isStructTy() && retVal->getType()->isIntegerTy() )
			{
				retVal = llvm::Constant::getNullValue( expectedType );
			}
			else if ( expectedType->isIntegerTy() && retVal->getType()->isPointerTy() )
			{
				retVal = mBuilder->CreatePtrToInt( retVal, expectedType, "ptrtoint" );
			}
			else if ( expectedType->isPointerTy() && retVal->getType()->isIntegerTy() )
			{
				retVal = mBuilder->CreateIntToPtr( retVal, expectedType, "inttoptr" );
			}
		}

		// Store return value and check ensures (postcondition) clauses
		if ( mResultAlloca != nullptr && mCurrentFunction != nullptr )
		{
			mBuilder->CreateStore( retVal, mResultAlloca );
			for ( auto &clause : mCurrentFunction->mEnsuresClauses )
			{
				genContractCheck( clause, "Postcondition violated" );
			}
		}

		mBuilder->CreateRet( retVal );
	}
	else if ( ret->mExpression != nullptr )
	{
		// Expression was present but genExpression returned null
		if ( expectedType->isVoidTy() )
			mBuilder->CreateRetVoid();
		else
			mBuilder->CreateRet( llvm::Constant::getNullValue( expectedType ) );
	}
	else
	{
		// Check ensures for void return
		if ( mCurrentFunction != nullptr )
		{
			for ( auto &clause : mCurrentFunction->mEnsuresClauses )
			{
				genContractCheck( clause, "Postcondition violated" );
			}
		}
		mBuilder->CreateRetVoid();
	}
}

void CodeGen::genIfStatement( IfStatement *ifStmt )
{
	llvm::Value *condVal = genExpression( ifStmt->mIfExpression );
	if ( condVal == nullptr )
		return;

	// Convert condition to a bool (i1) by comparing != 0
	if ( !condVal->getType()->isIntegerTy( 1 ) )
	{
		condVal = mBuilder->CreateICmpNE(
			condVal,
			llvm::ConstantInt::get( condVal->getType(), 0 ),
			"ifcond" );
	}

	// Release any temps created during condition evaluation (e.g., string
	// comparison literals) before branching.  Both branches may diverge
	// (early return, etc.) so we must release before the split.
	releaseTempStrings();

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	llvm::BasicBlock *thenBB = llvm::BasicBlock::Create( *mContext, "then", func );
	llvm::BasicBlock *elseBB = llvm::BasicBlock::Create( *mContext, "else", func );
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "ifmerge", func );

	mBuilder->CreateCondBr( condVal, thenBB, elseBB );

	// Save moved set before branches for conservative union
	auto savedMoved = mMovedVariables;

	// Then block
	mBuilder->SetInsertPoint( thenBB );
	if ( ifStmt->mStatement != nullptr )
		genStatement( ifStmt->mStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( mergeBB );

	// Capture moves from then branch
	auto thenMoved = mMovedVariables;

	// Restore to pre-branch state for else
	mMovedVariables = savedMoved;

	// Else block
	mBuilder->SetInsertPoint( elseBB );
	if ( ifStmt->mElseStatement != nullptr )
		genStatement( ifStmt->mElseStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( mergeBB );

	// Conservative: union moves from both branches
	// If moved in either branch, consider moved after the if/else
	mMovedVariables.insert( thenMoved.begin(), thenMoved.end() );

	// Continue at merge
	mBuilder->SetInsertPoint( mergeBB );
}

void CodeGen::genWhileStatement( WhileStatement *whileStmt )
{
	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "whilecond", func );
	llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "whilebody", func );
	llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "whileend", func );

	mBuilder->CreateBr( condBB );

	// Condition block
	mBuilder->SetInsertPoint( condBB );
	llvm::Value *condVal = genExpression( whileStmt->mLoopExpression );
	if ( condVal != nullptr )
	{
		if ( !condVal->getType()->isIntegerTy( 1 ) )
		{
			condVal = mBuilder->CreateICmpNE(
				condVal,
				llvm::ConstantInt::get( condVal->getType(), 0 ),
				"whilecond" );
		}
		// Release condition temps before branching
		releaseTempStrings();
		mBuilder->CreateCondBr( condVal, bodyBB, afterBB );
	}

	// Push loop targets for break/continue
	mLoopStack.push_back( { condBB, afterBB } );

	// Body block
	mBuilder->SetInsertPoint( bodyBB );
	bool savedInsideLoop = mInsideLoop;
	mInsideLoop = true;
	if ( whileStmt->mLoopStatement != nullptr )
		genStatement( whileStmt->mLoopStatement );
	mInsideLoop = savedInsideLoop;
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( condBB );

	mLoopStack.pop_back();

	// Continue after loop
	mBuilder->SetInsertPoint( afterBB );
}

void CodeGen::genForStatement( ForStatement *forStmt )
{
	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	// Init expression (in current block)
	if ( forStmt->mInitialExpression != nullptr )
		genExpression( forStmt->mInitialExpression );

	llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "forcond", func );
	llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forbody", func );
	llvm::BasicBlock *iterBB = llvm::BasicBlock::Create( *mContext, "foriter", func );
	llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forend", func );

	mBuilder->CreateBr( condBB );

	// Condition
	mBuilder->SetInsertPoint( condBB );
	if ( forStmt->mTestExpression != nullptr )
	{
		llvm::Value *condVal = genExpression( forStmt->mTestExpression );
		if ( condVal != nullptr )
		{
			if ( !condVal->getType()->isIntegerTy( 1 ) )
			{
				condVal = mBuilder->CreateICmpNE(
					condVal,
					llvm::ConstantInt::get( condVal->getType(), 0 ),
					"forcond" );
			}
			mBuilder->CreateCondBr( condVal, bodyBB, afterBB );
		}
	}
	else
	{
		mBuilder->CreateBr( bodyBB );
	}

	// Push loop targets: continue goes to iter, break goes to after
	mLoopStack.push_back( { iterBB, afterBB } );

	// Body
	mBuilder->SetInsertPoint( bodyBB );
	if ( forStmt->mStatement != nullptr )
		genStatement( forStmt->mStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( iterBB );

	mLoopStack.pop_back();

	// Iteration
	mBuilder->SetInsertPoint( iterBB );
	if ( forStmt->mIterationExpression != nullptr )
		genExpression( forStmt->mIterationExpression );
	mBuilder->CreateBr( condBB );

	// Continue after loop
	mBuilder->SetInsertPoint( afterBB );
}

llvm::Value *CodeGen::genExpression( Expression *expr )
{
	if ( expr == nullptr )
		return nullptr;

	if ( auto *ci = dynamic_cast<ConstInteger*>( expr ) )
		return genConstInteger( ci );
	else if ( auto *cf = dynamic_cast<ConstFloat*>( expr ) )
		return genConstFloat( cf );
	else if ( auto *cs = dynamic_cast<ConstString*>( expr ) )
		return genConstString( cs );
	else if ( auto *cc = dynamic_cast<ConstChar*>( expr ) )
		return genConstChar( cc );
	else if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
		return genVariableExpression( ve );
	else if ( auto *ce = dynamic_cast<CallExpression*>( expr ) )
		return genCallExpression( ce );
	else if ( auto *ops = dynamic_cast<OperationsExpression*>( expr ) )
		return genOperationsExpression( ops );
	else if ( auto *assign = dynamic_cast<AssignmentExpression*>( expr ) )
		return genAssignmentExpression( assign );
	else if ( auto *fieldAssign = dynamic_cast<FieldAssignmentExpression*>( expr ) )
		return genFieldAssignment( fieldAssign );
	else if ( auto *indexAssign = dynamic_cast<IndexAssignmentExpression*>( expr ) )
		return genIndexAssignment( indexAssign );
	else if ( auto *unary = dynamic_cast<UnaryExpression*>( expr ) )
		return genUnaryExpression( unary );
	else if ( auto *field = dynamic_cast<FieldAccessExpression*>( expr ) )
		return genFieldAccess( field );
	else if ( auto *method = dynamic_cast<MethodCallExpression*>( expr ) )
		return genMethodCall( method );
	else if ( auto *slit = dynamic_cast<StructLiteralExpression*>( expr ) )
		return genStructLiteral( slit );
	else if ( auto *enumCons = dynamic_cast<EnumConstructExpression*>( expr ) )
		return genEnumConstruct( enumCons );
	else if ( auto *matchExpr = dynamic_cast<MatchExpression*>( expr ) )
		return genMatchExpression( matchExpr );
	else if ( auto *tryExpr = dynamic_cast<TryExpression*>( expr ) )
		return genTryExpression( tryExpr );
	else if ( auto *arrLit = dynamic_cast<ArrayLiteralExpression*>( expr ) )
		return genArrayLiteral( arrLit );
	else if ( auto *idxExpr = dynamic_cast<IndexExpression*>( expr ) )
		return genIndexExpression( idxExpr );
	else if ( auto *awaitExpr = dynamic_cast<AwaitExpression*>( expr ) )
		return genAwaitExpression( awaitExpr );
	else if ( auto *pipeline = dynamic_cast<PipelineExpression*>( expr ) )
		return genPipelineExpression( pipeline );
	else if ( auto *interp = dynamic_cast<StringInterpolation*>( expr ) )
		return genStringInterpolation( interp );
	else if ( auto *queryExpr = dynamic_cast<QueryExpression*>( expr ) )
		return genQueryExpression( queryExpr );
	else if ( auto *insertExpr = dynamic_cast<InsertExpression*>( expr ) )
		return genInsertExpression( insertExpr );
	else if ( auto *updateExpr = dynamic_cast<UpdateExpression*>( expr ) )
		return genUpdateExpression( updateExpr );
	else if ( auto *deleteExpr = dynamic_cast<DeleteExpression*>( expr ) )
		return genDeleteExpression( deleteExpr );
	else if ( auto *spawn = dynamic_cast<SpawnStatement*>( expr ) )
		return genSpawnStatement( spawn );
	else if ( auto *lambda = dynamic_cast<LambdaExpression*>( expr ) )
		return genLambdaExpression( lambda );
	else if ( auto *funcRef = dynamic_cast<FunctionRefExpression*>( expr ) )
		return genFunctionRefExpression( funcRef );
	else if ( auto *indCall = dynamic_cast<IndirectCallExpression*>( expr ) )
		return genIndirectCallExpression( indCall );

	return nullptr;
}

llvm::Value *CodeGen::genConstInteger( ConstInteger *ci )
{
	return llvm::ConstantInt::get(
		llvm::Type::getInt32Ty( *mContext ), ci->mValue, true );
}

llvm::Value *CodeGen::genConstFloat( ConstFloat *cf )
{
	return llvm::ConstantFP::get(
		llvm::Type::getDoubleTy( *mContext ), cf->mValue );
}

llvm::Value *CodeGen::genConstString( ConstString *cs )
{
	// Create the global string data (null-terminated for C compat)
	llvm::Constant *strData = mBuilder->CreateGlobalStringPtr( cs->mValue, "str.data" );

	// Call __blang_string_create_static(data, length)
	llvm::Function *createStatic = getOrDeclareStringCreateStatic();
	llvm::Value *lenVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), cs->mValue.size() );
	llvm::Value *result = mBuilder->CreateCall( createStatic, { strData, lenVal }, "str" );
	trackTempString( result );
	return result;
}

llvm::Value *CodeGen::genConstChar( ConstChar *cc )
{
	uint8_t charVal = 0;
	if ( !cc->mValue.empty() )
		charVal = static_cast<uint8_t>( cc->mValue[0] );
	return llvm::ConstantInt::get(
		llvm::Type::getInt8Ty( *mContext ), charVal );
}

llvm::Value *CodeGen::genVariableExpression( VariableExpression *var )
{
	VariableDefinition *varDef = var->mVariable;

	// Check for own variables crossing spawn boundaries (before variable lookup,
	// because own variables are excluded from spawn captures)
	if ( mSpawnOuterOwnVars.count( varDef ) )
	{
		cerr << "Error: own variable '" << varDef->getName()
			 << "' cannot be captured by spawn block (use shared or sync instead)" << endl;
		mHasError = true;
		return nullptr;
	}

	auto it = mVariableMap.find( varDef );
	if ( it == mVariableMap.end() )
	{
		cerr << "CodeGen: undefined variable '" << varDef->getName() << "'" << endl;
		return nullptr;
	}

	// Check for use-after-move on own variables
	if ( varDef->getOwnership() == OwnershipQualifier::kOwnership_Own &&
		 mMovedVariables.count( varDef ) )
	{
		cerr << "Error: use of moved variable '" << varDef->getName() << "'" << endl;
		mHasError = true;
		return nullptr;
	}

	llvm::AllocaInst *alloca = it->second;
	OwnershipQualifier ownership = varDef->getOwnership();

	if ( ownership == OwnershipQualifier::kOwnership_Shared )
	{
		// Shared: alloca holds a pointer to heap data (immutable, no lock needed).
		// Load the pointer, then load the actual value through it.
		llvm::Value *heapPtr = mBuilder->CreateLoad(
			llvm::PointerType::get( *mContext, 0 ), alloca, varDef->getName() + ".ptr" );
		llvm::Type *dataType = getLLVMType( varDef->getVariableType() );
		return mBuilder->CreateLoad( dataType, heapPtr, varDef->getName() );
	}

	if ( ownership == OwnershipQualifier::kOwnership_Sync )
	{
		// Sync: alloca holds a pointer to heap data (mutex-protected).
		// Lock before read, unlock after read to prevent data races.
		llvm::Value *heapPtr = mBuilder->CreateLoad(
			llvm::PointerType::get( *mContext, 0 ), alloca, varDef->getName() + ".ptr" );
		llvm::Type *dataType = getLLVMType( varDef->getVariableType() );
		mBuilder->CreateCall( getOrDeclareSyncLock(), { heapPtr } );
		llvm::Value *val = mBuilder->CreateLoad( dataType, heapPtr, varDef->getName() );
		mBuilder->CreateCall( getOrDeclareSyncUnlock(), { heapPtr } );
		return val;
	}

	return mBuilder->CreateLoad( alloca->getAllocatedType(), alloca, varDef->getName() );
}

llvm::Value *CodeGen::genCallExpression( CallExpression *call )
{
	FunctionDefinition *funcDef = call->mFunction;

	// Handle builtin functions (print/println)
	if ( funcDef->isBuiltin() )
	{
		if ( funcDef->getName() == "print" )
		{
			genPrintCall( call, false );
			return nullptr;
		}
		if ( funcDef->getName() == "println" )
		{
			genPrintCall( call, true );
			return nullptr;
		}
	}

	// Handle @format annotation: validate format string at call site
	for ( const auto &ann : funcDef->getAnnotations() )
	{
		if ( ann.mName == "format" && !call->mParams.empty() )
		{
			auto *fmtConst = dynamic_cast<ConstString*>( (Expression*)call->mParams[0] );
			if ( fmtConst != nullptr )
			{
				// Parse format string and validate arg count
				int phCount = 0;
				const std::string &fmt = fmtConst->mValue;
				for ( size_t fi = 0; fi < fmt.size(); fi++ )
				{
					if ( fmt[fi] == '{' && fi + 1 < fmt.size() && fmt[fi + 1] == '{' )
					{ fi++; continue; }
					if ( fmt[fi] == '{' )
						phCount++;
				}
				int extraArgs = (int)call->mParams.size() - funcDef->getNumberParams();
				if ( phCount != extraArgs )
				{
					cerr << "CodeGen error: @format function '" << funcDef->getName()
						 << "': format string has " << phCount
						 << " placeholder(s) but " << extraArgs << " extra argument(s) provided" << endl;
					mHasError = true;
					return nullptr;
				}
			}
			break;
		}
	}

	// Handle generic function instantiation
	if ( !call->mTypeArgs.empty() && funcDef->isGeneric() )
	{
		llvm::Function *genFunc = instantiateGenericFunction( funcDef, call->mTypeArgs );
		if ( genFunc == nullptr )
		{
			cerr << "CodeGen: failed to instantiate generic function '"
				<< funcDef->getName() << "'" << endl;
			return nullptr;
		}

		// Generate argument values
		std::vector<llvm::Value*> args;
		for ( auto &paramExpr : call->mParams )
		{
			llvm::Value *argVal = genExpression( paramExpr );
			if ( argVal == nullptr )
				return nullptr;
			args.push_back( argVal );
		}

		if ( genFunc->getReturnType()->isVoidTy() )
		{
			mBuilder->CreateCall( genFunc, args );
			return nullptr;
		}

		llvm::Value *callResult = mBuilder->CreateCall( genFunc, args, "calltmp" );
		if ( funcDef->getReturnType() != nullptr &&
			 funcDef->getReturnType()->getName() == "string" )
			trackTempString( callResult );
		return callResult;
	}

	// Look up the LLVM function
	llvm::Function *llvmFunc = nullptr;
	auto it = mFunctionMap.find( funcDef );
	if ( it != mFunctionMap.end() )
	{
		llvmFunc = it->second;
	}
	else
	{
		// Try by name in the module — check mangled name first, then original
		if ( !call->mMangledName.empty() )
			llvmFunc = mModule->getFunction( call->mMangledName );
		if ( llvmFunc == nullptr )
			llvmFunc = mModule->getFunction( funcDef->getName() );
	}

	// If still not found, auto-declare extern functions (e.g. from .bmod imports)
	if ( llvmFunc == nullptr && funcDef->isExtern() )
	{
		llvmFunc = genFunction( funcDef );
	}

	if ( llvmFunc == nullptr )
	{
		cerr << "CodeGen: undefined function '" << funcDef->getName() << "'"
			<< ( !call->mMangledName.empty() ? " (mangled: " + call->mMangledName + ")" : "" )
			<< endl;
		return nullptr;
	}

	// Generate argument values with FFI conversion for extern functions
	std::vector<llvm::Value*> args;
	for ( size_t argIdx = 0; argIdx < call->mParams.size(); argIdx++ )
	{
		llvm::Value *argVal = genExpression( call->mParams[argIdx] );
		if ( argVal == nullptr )
			return nullptr;

		// FFI: if calling extern fn and param is cstring but arg is string,
		// extract the .data field from BlangString*
		if ( funcDef->isExtern() && argIdx < (size_t)funcDef->getNumberParams() )
		{
			VariableDefinition *paramDef = funcDef->getParam( argIdx );
			if ( paramDef != nullptr &&
				 paramDef->getVariableType() != nullptr &&
				 paramDef->getVariableType()->getName() == "cstring" &&
				 isStringType( call->mParams[argIdx] ) )
			{
				// GEP into BlangString struct field 0 (data pointer) and load
				// BlangString: { char*, i64, i64, i32 }
				llvm::StructType *bsType = llvm::StructType::get( *mContext,
					{ llvm::PointerType::get( *mContext, 0 ),
					  llvm::Type::getInt64Ty( *mContext ),
					  llvm::Type::getInt64Ty( *mContext ),
					  llvm::Type::getInt32Ty( *mContext ) } );
				llvm::Value *dataPtr = mBuilder->CreateStructGEP(
					bsType, argVal, 0, "str.data.ptr" );
				argVal = mBuilder->CreateLoad(
					llvm::PointerType::get( *mContext, 0 ), dataPtr, "str.data" );
			}

			// FFI: if param is carray and arg is Array, extract .data field
			if ( paramDef->getVariableType()->getName() == "carray" &&
				 isArrayType( call->mParams[argIdx] ) )
			{
				// GEP into BlangArray struct field 0 (data pointer) and load
				// BlangArray: { void*, i64, i64, i32, i32 }
				llvm::StructType *baType = llvm::StructType::get( *mContext,
					{ llvm::PointerType::get( *mContext, 0 ),
					  llvm::Type::getInt64Ty( *mContext ),
					  llvm::Type::getInt64Ty( *mContext ),
					  llvm::Type::getInt32Ty( *mContext ),
					  llvm::Type::getInt32Ty( *mContext ) } );
				llvm::Value *dataPtr = mBuilder->CreateStructGEP(
					baType, argVal, 0, "arr.data.ptr" );
				argVal = mBuilder->CreateLoad(
					llvm::PointerType::get( *mContext, 0 ), dataPtr, "arr.data" );
			}
		}

		// Fn-type argument: split {ptr, ptr} into two separate LLVM args
		if ( argIdx < (size_t)funcDef->getNumberParams() )
		{
			VariableDefinition *paramDef = funcDef->getParam( argIdx );
			if ( paramDef != nullptr && paramDef->getVariableType()->isFunctionType() )
			{
				// Check if argVal is a named function reference (needs thunk wrapping)
				auto *argVarExpr = dynamic_cast<VariableExpression*>( (Expression*)call->mParams[argIdx] );
				if ( argVarExpr == nullptr )
				{
					// It's a lambda or other expression that already produces {ptr, ptr}
					// Check if it's a named function being passed by name
					auto *argCallExpr = dynamic_cast<CallExpression*>( (Expression*)call->mParams[argIdx] );
					(void)argCallExpr;
				}
				llvm::Value *fnPtr = mBuilder->CreateExtractValue( argVal, 0, "cb.fn" );
				llvm::Value *ctxPtr = mBuilder->CreateExtractValue( argVal, 1, "cb.ctx" );
				args.push_back( fnPtr );
				args.push_back( ctxPtr );
				continue;
			}
		}

		// Integer type promotion: widen i32 to i64 if the function parameter expects it
		if ( llvmFunc != nullptr && argIdx < llvmFunc->arg_size() )
		{
			llvm::Type *paramType = llvmFunc->getFunctionType()->getParamType( argIdx );
			if ( paramType->isIntegerTy() && argVal->getType()->isIntegerTy() &&
				 paramType->getIntegerBitWidth() > argVal->getType()->getIntegerBitWidth() )
			{
				argVal = mBuilder->CreateSExt( argVal, paramType, "arg.ext" );
			}
		}

		args.push_back( argVal );
	}

	// Move semantics: mark own variables as moved when passed to own parameters
	for ( size_t i = 0; i < call->mParams.size() && i < (size_t)funcDef->getNumberParams(); i++ )
	{
		VariableDefinition *paramDef = funcDef->getParam( i );
		if ( paramDef != nullptr && paramDef->getOwnership() == OwnershipQualifier::kOwnership_Own )
		{
			auto *argVarExpr = dynamic_cast<VariableExpression*>( (Expression*)call->mParams[i] );
			if ( argVarExpr != nullptr )
			{
				VariableDefinition *srcDef = argVarExpr->getVariable();
				if ( srcDef->getOwnership() == OwnershipQualifier::kOwnership_Own )
				{
					if ( mInsideLoop )
					{
						cerr << "Error: cannot move own variable '" << srcDef->getName()
							 << "' inside a loop (would move on each iteration)" << endl;
						mHasError = true;
						return nullptr;
					}
					mMovedVariables.insert( srcDef );
				}
			}
		}
	}

	if ( llvmFunc->getReturnType()->isVoidTy() )
	{
		mBuilder->CreateCall( llvmFunc, args );
		return nullptr;
	}

	llvm::Value *callResult = mBuilder->CreateCall( llvmFunc, args, "calltmp" );

	// Track string-returning function calls as temps
	if ( funcDef->getReturnType() != nullptr &&
		 funcDef->getReturnType()->getName() == "string" )
	{
		trackTempString( callResult );
	}

	return callResult;
}

llvm::Value *CodeGen::genOperationsExpression( OperationsExpression *ops )
{
	llvm::Value *left = genExpression( ops->mOp1 );
	llvm::Value *right = genExpression( ops->mOp2 );

	if ( left == nullptr || right == nullptr )
		return nullptr;

	// Type promotion for mixed-width operands
	if ( left->getType() != right->getType() )
	{
		if ( left->getType()->isIntegerTy() && right->getType()->isIntegerTy() )
		{
			unsigned leftBits = left->getType()->getIntegerBitWidth();
			unsigned rightBits = right->getType()->getIntegerBitWidth();
			if ( leftBits < rightBits )
			{
				if ( leftBits == 1 )
					left = mBuilder->CreateZExt( left, right->getType(), "bpromote" );
				else
					left = mBuilder->CreateSExt( left, right->getType(), "promote" );
			}
			else
			{
				if ( rightBits == 1 )
					right = mBuilder->CreateZExt( right, left->getType(), "bpromote" );
				else
					right = mBuilder->CreateSExt( right, left->getType(), "promote" );
			}
		}
		else if ( left->getType()->isFloatingPointTy() && right->getType()->isFloatingPointTy() )
		{
			if ( left->getType()->isFloatTy() && right->getType()->isDoubleTy() )
				left = mBuilder->CreateFPExt( left, right->getType(), "fpromote" );
			else if ( left->getType()->isDoubleTy() && right->getType()->isFloatTy() )
				right = mBuilder->CreateFPExt( right, left->getType(), "fpromote" );
		}
		else if ( left->getType()->isIntegerTy() && right->getType()->isFloatingPointTy() )
		{
			left = mBuilder->CreateSIToFP( left, right->getType(), "itofp" );
		}
		else if ( left->getType()->isFloatingPointTy() && right->getType()->isIntegerTy() )
		{
			right = mBuilder->CreateSIToFP( right, left->getType(), "itofp" );
		}
	}

	bool isFloat = left->getType()->isFloatingPointTy();

	// String operations: check if operands are string type
	bool isString = isStringType( ops->mOp1 ) && isStringType( ops->mOp2 );

	// Array operations: check if operands are array type
	bool isArray = isArrayType( ops->mOp1 ) && isArrayType( ops->mOp2 );

	const string &op = ops->mOperation;

	// Array concatenation
	if ( isArray && op == "+" )
		return mBuilder->CreateCall( getOrDeclareArrayConcat(), { left, right }, "arrcat" );

	// String concatenation
	if ( isString && op == "+" )
	{
		llvm::Value *result = mBuilder->CreateCall( getOrDeclareStringConcat(), { left, right }, "strcat" );
		trackTempString( result );
		return result;
	}

	// String comparison
	if ( isString && op == "==" )
		return mBuilder->CreateCall( getOrDeclareStringEquals(), { left, right }, "streq" );
	if ( isString && op == "!=" )
	{
		llvm::Value *eq = mBuilder->CreateCall( getOrDeclareStringEquals(), { left, right }, "streq" );
		return mBuilder->CreateNot( eq, "strne" );
	}

	// Arithmetic
	if ( op == "+" )  return isFloat ? mBuilder->CreateFAdd( left, right, "addtmp" ) : mBuilder->CreateAdd( left, right, "addtmp" );
	if ( op == "-" )  return isFloat ? mBuilder->CreateFSub( left, right, "subtmp" ) : mBuilder->CreateSub( left, right, "subtmp" );
	if ( op == "*" )  return isFloat ? mBuilder->CreateFMul( left, right, "multmp" ) : mBuilder->CreateMul( left, right, "multmp" );
	if ( op == "/" )  return isFloat ? mBuilder->CreateFDiv( left, right, "divtmp" ) : mBuilder->CreateSDiv( left, right, "divtmp" );
	if ( op == "%" )  return isFloat ? mBuilder->CreateFRem( left, right, "modtmp" ) : mBuilder->CreateSRem( left, right, "modtmp" );

	// Bitwise (integer only)
	if ( op == "&" )  return mBuilder->CreateAnd( left, right, "andtmp" );
	if ( op == "|" )  return mBuilder->CreateOr( left, right, "ortmp" );
	if ( op == "^" )  return mBuilder->CreateXor( left, right, "xortmp" );
	if ( op == "<<" ) return mBuilder->CreateShl( left, right, "shltmp" );
	if ( op == ">>" ) return mBuilder->CreateAShr( left, right, "shrtmp" );

	// Comparisons (produce i1)
	if ( op == "==" ) return isFloat ? mBuilder->CreateFCmpOEQ( left, right, "eqtmp" ) : mBuilder->CreateICmpEQ( left, right, "eqtmp" );
	if ( op == "!=" ) return isFloat ? mBuilder->CreateFCmpONE( left, right, "netmp" ) : mBuilder->CreateICmpNE( left, right, "netmp" );
	if ( op == "<" )  return isFloat ? mBuilder->CreateFCmpOLT( left, right, "lttmp" ) : mBuilder->CreateICmpSLT( left, right, "lttmp" );
	if ( op == ">" )  return isFloat ? mBuilder->CreateFCmpOGT( left, right, "gttmp" ) : mBuilder->CreateICmpSGT( left, right, "gttmp" );
	if ( op == "<=" ) return isFloat ? mBuilder->CreateFCmpOLE( left, right, "letmp" ) : mBuilder->CreateICmpSLE( left, right, "letmp" );
	if ( op == ">=" ) return isFloat ? mBuilder->CreateFCmpOGE( left, right, "getmp" ) : mBuilder->CreateICmpSGE( left, right, "getmp" );

	// Logical (treat operands as booleans via != 0)
	if ( op == "&&" )
	{
		llvm::Value *lBool = isFloat
			? mBuilder->CreateFCmpONE( left, llvm::ConstantFP::get( left->getType(), 0.0 ), "lbool" )
			: mBuilder->CreateICmpNE( left, llvm::ConstantInt::get( left->getType(), 0 ), "lbool" );
		llvm::Value *rBool = isFloat
			? mBuilder->CreateFCmpONE( right, llvm::ConstantFP::get( right->getType(), 0.0 ), "rbool" )
			: mBuilder->CreateICmpNE( right, llvm::ConstantInt::get( right->getType(), 0 ), "rbool" );
		return mBuilder->CreateAnd( lBool, rBool, "landtmp" );
	}
	if ( op == "||" )
	{
		llvm::Value *lBool = isFloat
			? mBuilder->CreateFCmpONE( left, llvm::ConstantFP::get( left->getType(), 0.0 ), "lbool" )
			: mBuilder->CreateICmpNE( left, llvm::ConstantInt::get( left->getType(), 0 ), "lbool" );
		llvm::Value *rBool = isFloat
			? mBuilder->CreateFCmpONE( right, llvm::ConstantFP::get( right->getType(), 0.0 ), "rbool" )
			: mBuilder->CreateICmpNE( right, llvm::ConstantInt::get( right->getType(), 0 ), "rbool" );
		return mBuilder->CreateOr( lBool, rBool, "lortmp" );
	}

	cerr << "CodeGen: unknown binary operator '" << op << "'" << endl;
	return nullptr;
}

llvm::Value *CodeGen::genAssignmentExpression( AssignmentExpression *assign )
{
	VariableDefinition *varDef = assign->mVariable;
	auto it = mVariableMap.find( varDef );
	if ( it == mVariableMap.end() )
	{
		cerr << "CodeGen: undefined variable '" << varDef->getName() << "'" << endl;
		return nullptr;
	}

	llvm::AllocaInst *alloca = it->second;
	const string &op = assign->mOperation;
	OwnershipQualifier ownership = varDef->getOwnership();

	// Reject assignment to shared variables — shared values are immutable
	if ( ownership == OwnershipQualifier::kOwnership_Shared )
	{
		cerr << "CodeGen error: cannot assign to shared variable '"
			 << varDef->getName() << "' — shared values are immutable" << endl;
		return nullptr;
	}

	// Sync `=` assignment: evaluate RHS inside the lock to prevent TOCTOU races.
	// This ensures expressions like `counter = counter + 1` are atomic.
	if ( ownership == OwnershipQualifier::kOwnership_Sync && op == "=" )
	{
		llvm::Value *heapPtr = mBuilder->CreateLoad(
			llvm::PointerType::get( *mContext, 0 ), alloca, varDef->getName() + ".ptr" );
		mBuilder->CreateCall( getOrDeclareSyncLock(), { heapPtr } );
		llvm::Value *rhs = genExpression( assign->mValue );
		if ( rhs == nullptr )
		{
			mBuilder->CreateCall( getOrDeclareSyncUnlock(), { heapPtr } );
			return nullptr;
		}
		mBuilder->CreateStore( rhs, heapPtr );
		mBuilder->CreateCall( getOrDeclareSyncUnlock(), { heapPtr } );
		return rhs;
	}

	llvm::Value *rhs = genExpression( assign->mValue );
	if ( rhs == nullptr )
		return nullptr;

	// Sync compound assignment: lock, read-modify-write, unlock
	if ( ownership == OwnershipQualifier::kOwnership_Sync )
	{
		llvm::Value *heapPtr = mBuilder->CreateLoad(
			llvm::PointerType::get( *mContext, 0 ), alloca, varDef->getName() + ".ptr" );

		llvm::Type *dataType = getLLVMType( varDef->getVariableType() );
		mBuilder->CreateCall( getOrDeclareSyncLock(), { heapPtr } );
		llvm::Value *current = mBuilder->CreateLoad( dataType, heapPtr, "cur" );
		llvm::Value *result = nullptr;
		if ( op == "+=" )      result = mBuilder->CreateAdd( current, rhs, "addassign" );
		else if ( op == "-=" ) result = mBuilder->CreateSub( current, rhs, "subassign" );
		else if ( op == "*=" ) result = mBuilder->CreateMul( current, rhs, "mulassign" );
		else if ( op == "/=" ) result = mBuilder->CreateSDiv( current, rhs, "divassign" );
		else result = current;
		if ( result != nullptr )
			mBuilder->CreateStore( result, heapPtr );
		mBuilder->CreateCall( getOrDeclareSyncUnlock(), { heapPtr } );
		return result;
	}

	if ( op == "=" )
	{
		// If reassigning a string variable, release the old value first
		if ( varDef->getVariableType() != nullptr &&
			 varDef->getVariableType()->getName() == "string" )
		{
			llvm::Value *oldVal = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "str.old" );
			mBuilder->CreateCall( getOrDeclareStringRelease(), { oldVal } );
			// The RHS is now owned by this variable — untrack from temps
			untrackTempString( rhs );
		}

		mBuilder->CreateStore( rhs, alloca );

		// Move semantics: if assigning an own variable from another own variable,
		// mark the source as moved
		if ( ownership == OwnershipQualifier::kOwnership_Own )
		{
			auto *srcVarExpr = dynamic_cast<VariableExpression*>( (Expression*)assign->mValue );
			if ( srcVarExpr != nullptr )
			{
				VariableDefinition *srcDef = srcVarExpr->getVariable();
				if ( srcDef->getOwnership() == OwnershipQualifier::kOwnership_Own )
				{
					if ( mInsideLoop )
					{
						cerr << "Error: cannot move own variable '" << srcDef->getName()
							 << "' inside a loop (would move on each iteration)" << endl;
						mHasError = true;
						return nullptr;
					}
					mMovedVariables.insert( srcDef );
				}
			}
		}

		return rhs;
	}

	// Compound assignment: load current value, apply operation, store result
	llvm::Value *current = mBuilder->CreateLoad(
		alloca->getAllocatedType(), alloca, "cur" );
	llvm::Value *result = nullptr;

	if ( op == "+=" )      result = mBuilder->CreateAdd( current, rhs, "addassign" );
	else if ( op == "-=" ) result = mBuilder->CreateSub( current, rhs, "subassign" );
	else if ( op == "*=" ) result = mBuilder->CreateMul( current, rhs, "mulassign" );
	else if ( op == "/=" ) result = mBuilder->CreateSDiv( current, rhs, "divassign" );
	else if ( op == "%=" ) result = mBuilder->CreateSRem( current, rhs, "modassign" );
	else if ( op == "^=" ) result = mBuilder->CreateXor( current, rhs, "xorassign" );
	else
	{
		cerr << "CodeGen: unknown assignment operator '" << op << "'" << endl;
		return nullptr;
	}

	mBuilder->CreateStore( result, alloca );
	return result;
}

llvm::Value *CodeGen::genUnaryExpression( UnaryExpression *unary )
{
	llvm::Value *operand = genExpression( unary->mOperand );
	if ( operand == nullptr )
		return nullptr;

	const string &op = unary->mOperation;

	if ( op == "-" )
		return mBuilder->CreateNeg( operand, "negtmp" );

	if ( op == "!" )
	{
		llvm::Value *boolVal = mBuilder->CreateICmpNE(
			operand,
			llvm::ConstantInt::get( operand->getType(), 0 ),
			"tobool" );
		llvm::Value *notVal = mBuilder->CreateXor(
			boolVal,
			llvm::ConstantInt::getTrue( *mContext ),
			"nottmp" );
		return mBuilder->CreateZExt( notVal, operand->getType(), "lnot" );
	}

	if ( op == "~" )
		return mBuilder->CreateNot( operand, "bnottmp" );

	cerr << "CodeGen: unknown unary operator '" << op << "'" << endl;
	return nullptr;
}

// ---- Struct codegen (Tasks 26-27) ----

llvm::AllocaInst *CodeGen::getExpressionAddress( Expression *expr )
{
	if ( auto *ve = dynamic_cast<VariableExpression*>( expr ) )
	{
		auto it = mVariableMap.find( ve->mVariable );
		if ( it != mVariableMap.end() )
			return it->second;
	}
	return nullptr;
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

		// Coerce value type to match field type (e.g., double literal → float field)
		llvm::Type *fieldType = structType->getElementType( fieldIdx );
		if ( fieldVal->getType() != fieldType )
		{
			if ( fieldType->isFloatTy() && fieldVal->getType()->isDoubleTy() )
				fieldVal = mBuilder->CreateFPTrunc( fieldVal, fieldType, "fptrunc" );
			else if ( fieldType->isDoubleTy() && fieldVal->getType()->isFloatTy() )
				fieldVal = mBuilder->CreateFPExt( fieldVal, fieldType, "fpext" );
			else if ( fieldType->isIntegerTy() && fieldVal->getType()->isIntegerTy() )
				fieldVal = mBuilder->CreateIntCast( fieldVal, fieldType,
					true, "icast" );
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
				else if ( fTypeName == "Buffer" && srcIsExistingOwner )
					mBuilder->CreateCall( getOrDeclareBufferRetain(), { fieldVal } );
				else if ( isUserStructType( fTypeName ) && srcIsExistingOwner )
					mBuilder->CreateCall( getOrDeclareRcRetain(), { fieldVal } );
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

	// Return the heap pointer (struct is by-reference)
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

		// Retain strings pushed into arrays so they survive scope cleanup.
		// The array takes ownership; the scope would otherwise release the
		// original variable, leaving a dangling pointer in the array.
		if ( isStringType( expr->mArgs[0] ) )
			mBuilder->CreateCall( getOrDeclareStringRetain(), { elemVal } );

		llvm::AllocaInst *tmpAlloca = mBuilder->CreateAlloca(
			elemVal->getType(), nullptr, "push.tmp" );
		mBuilder->CreateStore( elemVal, tmpAlloca );
		mBuilder->CreateCall( getOrDeclareArrayPush(), { arrVal, tmpAlloca } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// pop(): get element type, create out alloca, call pop, return value
	if ( method == "pop" && expr->mArgs.empty() )
	{
		// Determine element type from the Array<T> type annotation
		llvm::Type *elemType = llvm::Type::getInt32Ty( *mContext ); // default
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
			{
				Type *ep = varType->getTypeParam( 0 );
				string en = ep->getName();
				auto subIt = mTypeSubstitution.find( en );
				if ( subIt != mTypeSubstitution.end() )
					elemType = getLLVMType( subIt->second );
				else
					elemType = getLLVMType( ep );
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
					elemType = getLLVMType( subIt->second );
				else
					elemType = getLLVMType( ep );
			}
		}

		llvm::AllocaInst *outAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, "pop.out" );
		mBuilder->CreateCall( getOrDeclareArrayPop(), { arrVal, outAlloca } );
		return mBuilder->CreateLoad( elemType, outAlloca, "pop.val" );
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

	llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, gepBase, fieldIdx, expr->mFieldName );
	llvm::Value *fieldVal = mBuilder->CreateLoad(
		structType->getElementType( fieldIdx ), fieldPtr, expr->mFieldName + ".val" );

	// Retain refcounted fields so the loaded value survives independently of the struct.
	// The struct will release its reference at scope exit (emitStructFieldRelease),
	// and this retain ensures the loaded value stays valid until it is released
	// (either as a temp string or when the variable storing it goes out of scope).
	if ( structDef != nullptr && fieldIdx < (int)structDef->mFields.size() )
	{
		Type *fType = structDef->mFields[fieldIdx]->getVariableType();
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
		return nullptr;

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
	mBuilder->CreateStore( val, fieldPtr );
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

		return val;
	}

	return nullptr;
}

llvm::Value *CodeGen::genMethodCall( MethodCallExpression *expr )
{
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

	// Built-in channel method calls: chan<T> .send()/.recv()/.close()
	if ( isChanType( expr->mObject ) )
	{
		llvm::Value *result = genChanMethodCall( expr );
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
		return nullptr;

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

	return mBuilder->CreateCall( llvmFunc, args, "methodcall" );
}

// ---- Enum construction codegen ----

llvm::Value *CodeGen::genEnumConstruct( EnumConstructExpression *expr )
{
	EnumDefinition *enumDef = expr->mEnumDef;
	int variantIdx = expr->mVariantIndex;

	if ( !enumHasPayload( enumDef ) )
	{
		// Plain enum without payload — just return the tag as i32
		return llvm::ConstantInt::get(
			llvm::Type::getInt32Ty( *mContext ), variantIdx );
	}

	// Tagged union enum: { i32 tag, [N x i8] payload }
	llvm::StructType *enumType = getOrCreateEnumType( enumDef );

	// Alloca the enum struct
	llvm::AllocaInst *alloca = mBuilder->CreateAlloca( enumType, nullptr, "enum.tmp" );

	// Store the tag
	llvm::Value *tagPtr = mBuilder->CreateStructGEP( enumType, alloca, 0, "enum.tag.ptr" );
	mBuilder->CreateStore(
		llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), variantIdx ),
		tagPtr );

	// Store the payload (if this variant has associated types)
	auto &variant = enumDef->mVariants[variantIdx];
	if ( !variant.mAssociatedTypes.empty() && !expr->mArgs.empty() )
	{
		llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
			enumType, alloca, 1, "enum.payload.ptr" );

		// For each associated type, store the argument into the payload area
		uint64_t offset = 0;
		llvm::DataLayout dl( mModule.get() );

		for ( size_t i = 0; i < variant.mAssociatedTypes.size() && i < expr->mArgs.size(); i++ )
		{
			llvm::Value *argVal = genExpression( expr->mArgs[i] );
			if ( argVal == nullptr )
				continue;

			llvm::Type *argType = argVal->getType();

			// GEP into the payload byte array at the current offset
			llvm::Value *offsetVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), offset );
			llvm::Type *payloadArrType = enumType->getElementType( 1 );
			llvm::Value *bytePtr = mBuilder->CreateGEP(
				payloadArrType, payloadPtr,
				{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ), offsetVal },
				"enum.payload.byte" );

			// Store the value through the byte pointer
			mBuilder->CreateStore( argVal, bytePtr );

			// If storing a string into the enum payload, untrack the temp —
			// ownership transfers to the enum value
			if ( argType->isPointerTy() && i < variant.mAssociatedTypes.size() )
			{
				string assocTypeName = variant.mAssociatedTypes[i]->getName();
				if ( assocTypeName == "string" )
					untrackTempString( argVal );
			}

			uint64_t typeSize = dl.getTypeAllocSize( argType );
			if ( typeSize == 0 ) typeSize = 4; // fallback
			offset += typeSize;
		}
	}

	// Load and return the enum value
	return mBuilder->CreateLoad( enumType, alloca, "enum.val" );
}

// ---- Match codegen (Task 53) ----

llvm::Value *CodeGen::genMatchExpression( MatchExpression *expr )
{
	llvm::Value *subject = genExpression( expr->mSubject );
	if ( subject == nullptr )
		return nullptr;

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "matchend", func );

	// Determine if this is a tagged union enum match
	bool isEnumStruct = false;
	EnumDefinition *matchedEnum = nullptr;
	llvm::StructType *enumStructType = nullptr;
	llvm::Value *tagVal = nullptr;
	llvm::AllocaInst *subjectAlloca = nullptr;

	if ( auto *st = llvm::dyn_cast<llvm::StructType>( subject->getType() ) )
	{
		// Check if this struct type corresponds to a known enum
		string stName = st->hasName() ? st->getName().str() : "";
		// Enum types are named "enum.EnumName"
		if ( stName.substr( 0, 5 ) == "enum." )
		{
			string enumName = stName.substr( 5 );
			auto enumIt = mEnumDefMap.find( enumName );
			if ( enumIt != mEnumDefMap.end() )
			{
				isEnumStruct = true;
				matchedEnum = enumIt->second;
				enumStructType = st;

				// Store the subject in an alloca so we can GEP into it
				subjectAlloca = mBuilder->CreateAlloca( st, nullptr, "match.enum" );
				mBuilder->CreateStore( subject, subjectAlloca );

				// Extract the tag: GEP to field 0, load i32
				llvm::Value *tagPtr = mBuilder->CreateStructGEP(
					st, subjectAlloca, 0, "match.tag.ptr" );
				tagVal = mBuilder->CreateLoad(
					llvm::Type::getInt32Ty( *mContext ), tagPtr, "match.tag" );
			}
		}
	}

	// Find the default (wildcard) arm, or create a default that falls through
	int wildcardIdx = -1;
	for ( size_t i = 0; i < expr->mArms.size(); i++ )
	{
		if ( expr->mArms[i].mIsWildcard )
		{
			wildcardIdx = static_cast<int>( i );
			break;
		}
	}

	// Create basic blocks for each arm
	std::vector<llvm::BasicBlock*> armBBs;
	for ( size_t i = 0; i < expr->mArms.size(); i++ )
	{
		string name = "match.arm." + to_string( i );
		armBBs.push_back( llvm::BasicBlock::Create( *mContext, name, func ) );
	}

	// Default block: either the wildcard arm or the merge block
	llvm::BasicBlock *defaultBB = ( wildcardIdx >= 0 ) ? armBBs[wildcardIdx] : mergeBB;

	// Determine the switch value: either the subject itself (i32) or the extracted tag
	llvm::Value *switchVal = isEnumStruct ? tagVal : subject;

	// Build switch for integer/tag subjects
	if ( switchVal != nullptr && switchVal->getType()->isIntegerTy() )
	{
		// Count actual cases for the switch
		int numCases = 0;
		for ( size_t i = 0; i < expr->mArms.size(); i++ )
		{
			if ( !expr->mArms[i].mIsWildcard )
				numCases++;
		}

		llvm::SwitchInst *switchInst = mBuilder->CreateSwitch(
			switchVal, defaultBB, numCases );

		for ( size_t i = 0; i < expr->mArms.size(); i++ )
		{
			if ( expr->mArms[i].mIsWildcard )
				continue;

			const string &pattern = expr->mArms[i].mPattern;
			int64_t patternVal = 0;

			// Try to parse as integer
			bool isNumeric = !pattern.empty() &&
				( isdigit( pattern[0] ) || pattern[0] == '-' );
			if ( isNumeric )
			{
				patternVal = stoll( pattern );
			}
			else
			{
				// Named pattern (e.g., "ok", "err", "none") — use variant index
				bool found = false;

				// If we know the specific enum, search only in it
				if ( matchedEnum != nullptr )
				{
					for ( size_t v = 0; v < matchedEnum->mVariants.size(); v++ )
					{
						if ( matchedEnum->mVariants[v].mName == pattern )
						{
							patternVal = static_cast<int64_t>( v );
							found = true;
							break;
						}
					}
				}

				if ( !found )
				{
					// Search all registered enums
					for ( auto &ep : mEnumDefMap )
					{
						for ( size_t v = 0; v < ep.second->mVariants.size(); v++ )
						{
							if ( ep.second->mVariants[v].mName == pattern )
							{
								patternVal = static_cast<int64_t>( v );
								found = true;
								break;
							}
						}
						if ( found )
							break;
					}
				}
				if ( !found )
				{
					// Use arm index as fallback
					patternVal = static_cast<int64_t>( i );
				}
			}

			switchInst->addCase(
				llvm::ConstantInt::get(
					llvm::cast<llvm::IntegerType>( switchVal->getType() ),
					patternVal ),
				armBBs[i] );
		}
	}
	else
	{
		// Non-integer subject — just branch to default
		mBuilder->CreateBr( defaultBB );
	}

	// Generate code for each arm body
	for ( size_t i = 0; i < expr->mArms.size(); i++ )
	{
		mBuilder->SetInsertPoint( armBBs[i] );

		// If the arm has a binding, extract the payload from the enum struct
		if ( !expr->mArms[i].mBindingName.empty() )
		{
			if ( isEnumStruct && matchedEnum != nullptr && subjectAlloca != nullptr )
			{
				// Find the variant for this arm's pattern
				int variantIdx = -1;
				for ( size_t v = 0; v < matchedEnum->mVariants.size(); v++ )
				{
					if ( matchedEnum->mVariants[v].mName == expr->mArms[i].mPattern )
					{
						variantIdx = static_cast<int>( v );
						break;
					}
				}

				llvm::Type *bindType = llvm::Type::getInt32Ty( *mContext ); // default
				if ( variantIdx >= 0 &&
					 !matchedEnum->mVariants[variantIdx].mAssociatedTypes.empty() )
				{
					bindType = getLLVMType( matchedEnum->mVariants[variantIdx].mAssociatedTypes[0] );
				}

				// GEP to the payload area (field 1) and load the value
				llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
					enumStructType, subjectAlloca, 1, "match.payload.ptr" );

				// The payload is [N x i8]; GEP to byte 0 and load as the expected type
				llvm::Type *payloadArrType = enumStructType->getElementType( 1 );
				llvm::Value *bytePtr = mBuilder->CreateGEP(
					payloadArrType, payloadPtr,
					{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
					  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
					"match.payload.byte" );

				llvm::Value *payloadVal = mBuilder->CreateLoad(
					bindType, bytePtr, "match.payload.val" );

				// Create alloca for the binding variable
				llvm::AllocaInst *bindAlloca = mBuilder->CreateAlloca(
					bindType, nullptr, expr->mArms[i].mBindingName );
				mBuilder->CreateStore( payloadVal, bindAlloca );

				// Register in variable map
				if ( expr->mArms[i].mBody != nullptr &&
					 expr->mArms[i].mBody->mScope != nullptr )
				{
					Symbol *bindSym = expr->mArms[i].mBody->mScope->findSymbol(
						expr->mArms[i].mBindingName );
					if ( auto *bindVar = dynamic_cast<VariableDefinition*>( bindSym ) )
						mVariableMap[bindVar] = bindAlloca;
				}
			}
			else
			{
				// Non-enum binding: store the subject value directly
				llvm::Type *bindType = subject->getType();
				llvm::AllocaInst *bindAlloca = mBuilder->CreateAlloca(
					bindType, nullptr, expr->mArms[i].mBindingName );
				mBuilder->CreateStore( subject, bindAlloca );

				// Register in variable map
				if ( expr->mArms[i].mBody != nullptr &&
					 expr->mArms[i].mBody->mScope != nullptr )
				{
					Symbol *bindSym = expr->mArms[i].mBody->mScope->findSymbol(
						expr->mArms[i].mBindingName );
					if ( auto *bindVar = dynamic_cast<VariableDefinition*>( bindSym ) )
						mVariableMap[bindVar] = bindAlloca;
				}
			}
		}

		if ( expr->mArms[i].mBody != nullptr )
			genBlock( expr->mArms[i].mBody );

		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( mergeBB );
	}

	// Continue at merge
	mBuilder->SetInsertPoint( mergeBB );
	return nullptr;
}

// ---- Try operator codegen (Task 54) ----

EnumDefinition *CodeGen::resolveExpressionEnumDef( Expression *expr )
{
	// Resolve the QLang-level type name of the expression to find its EnumDefinition.
	std::string typeName;

	if ( auto *call = dynamic_cast<CallExpression*>( expr ) )
	{
		if ( call->mFunction != nullptr && call->mFunction->getReturnType() != nullptr )
			typeName = call->mFunction->getReturnType()->getName();
	}
	else if ( auto *varExpr = dynamic_cast<VariableExpression*>( expr ) )
	{
		if ( varExpr->mVariable != nullptr && varExpr->mVariable->getVariableType() != nullptr )
			typeName = varExpr->mVariable->getVariableType()->getName();
	}
	else if ( auto *methodCall = dynamic_cast<MethodCallExpression*>( expr ) )
	{
		(void)methodCall;
		// Method return types are harder to resolve statically; fall through
	}

	if ( !typeName.empty() )
	{
		auto it = mEnumDefMap.find( typeName );
		if ( it != mEnumDefMap.end() )
			return it->second;
	}

	return nullptr;
}

llvm::Value *CodeGen::genTryExpression( TryExpression *expr )
{
	// Generate the operand expression (e.g., might_fail())
	llvm::Value *result = genExpression( expr->mOperand );
	if ( result == nullptr )
		return nullptr;

	// Try to resolve the operand's enum definition
	EnumDefinition *enumDef = resolveExpressionEnumDef( expr->mOperand );

	// If we can't determine the enum type, fall back to pass-through
	if ( enumDef == nullptr || !enumHasPayload( enumDef ) )
		return result;

	// Find success variant (ok/some) and error variant (err/none)
	int successIdx = -1;
	int errorIdx = -1;
	for ( size_t i = 0; i < enumDef->mVariants.size(); i++ )
	{
		const std::string &vname = enumDef->mVariants[i].mName;
		if ( vname == "ok" || vname == "some" )
			successIdx = static_cast<int>( i );
		else if ( vname == "err" || vname == "none" )
			errorIdx = static_cast<int>( i );
	}

	// If we can't identify the variants, pass through
	if ( successIdx < 0 || errorIdx < 0 )
		return result;

	llvm::StructType *enumType = getOrCreateEnumType( enumDef );

	// Store the result in an alloca so we can GEP into it
	llvm::AllocaInst *enumAlloca = mBuilder->CreateAlloca( enumType, nullptr, "try.enum" );
	mBuilder->CreateStore( result, enumAlloca );

	// Extract the tag
	llvm::Value *tagPtr = mBuilder->CreateStructGEP( enumType, enumAlloca, 0, "try.tag.ptr" );
	llvm::Value *tagVal = mBuilder->CreateLoad(
		llvm::Type::getInt32Ty( *mContext ), tagPtr, "try.tag" );

	// Compare tag to the success index
	llvm::Value *isSuccess = mBuilder->CreateICmpEQ(
		tagVal,
		llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), successIdx ),
		"try.is_ok" );

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *okBB = llvm::BasicBlock::Create( *mContext, "try.ok", func );
	llvm::BasicBlock *errBB = llvm::BasicBlock::Create( *mContext, "try.err", func );
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "try.merge", func );

	mBuilder->CreateCondBr( isSuccess, okBB, errBB );

	// ---- Error branch: propagate the error by returning early ----
	mBuilder->SetInsertPoint( errBB );

	// Determine the current function's return type at the QLang level
	// to construct a matching error enum value for early return.
	llvm::Type *funcRetType = func->getReturnType();

	if ( funcRetType->isStructTy() )
	{
		// The current function returns an enum struct — forward the whole value.
		// The operand's enum value is the error, so just return it as-is
		// (compatible if same enum, or at least same layout).
		llvm::Value *errVal = mBuilder->CreateLoad( enumType, enumAlloca, "try.err.val" );

		// If the function's return struct type differs, we need to reconstruct.
		// For now, if they're the same type, return directly.
		if ( funcRetType == enumType )
		{
			mBuilder->CreateRet( errVal );
		}
		else
		{
			// Different return enum: extract error payload and re-wrap.
			// Construct a new enum of the function's return type with the error variant.
			EnumDefinition *retEnumDef = nullptr;
			if ( mCurrentFunction != nullptr && mCurrentFunction->getReturnType() != nullptr )
			{
				auto retIt = mEnumDefMap.find( mCurrentFunction->getReturnType()->getName() );
				if ( retIt != mEnumDefMap.end() )
					retEnumDef = retIt->second;
			}

			if ( retEnumDef != nullptr )
			{
				// Find the error variant in the return enum
				int retErrIdx = -1;
				for ( size_t i = 0; i < retEnumDef->mVariants.size(); i++ )
				{
					if ( retEnumDef->mVariants[i].mName == "err" ||
						 retEnumDef->mVariants[i].mName == "none" )
					{
						retErrIdx = static_cast<int>( i );
						break;
					}
				}

				if ( retErrIdx >= 0 )
				{
					llvm::StructType *retEnumType = getOrCreateEnumType( retEnumDef );
					llvm::AllocaInst *retAlloca = mBuilder->CreateAlloca(
						retEnumType, nullptr, "try.ret.enum" );

					// Set tag to error variant
					llvm::Value *retTagPtr = mBuilder->CreateStructGEP(
						retEnumType, retAlloca, 0, "try.ret.tag" );
					mBuilder->CreateStore(
						llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), retErrIdx ),
						retTagPtr );

					// Copy payload from source error to return error
					auto &srcErrVariant = enumDef->mVariants[errorIdx];
					if ( !srcErrVariant.mAssociatedTypes.empty() )
					{
						llvm::Type *payloadArrType = enumType->getElementType( 1 );
						llvm::Value *srcPayloadPtr = mBuilder->CreateStructGEP(
							enumType, enumAlloca, 1, "try.src.payload" );
						llvm::Value *srcByte = mBuilder->CreateGEP(
							payloadArrType, srcPayloadPtr,
							{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
							  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
							"try.src.byte" );
						llvm::Type *errPayloadType = getLLVMType( srcErrVariant.mAssociatedTypes[0] );
						llvm::Value *errPayload = mBuilder->CreateLoad(
							errPayloadType, srcByte, "try.err.payload" );

						llvm::Type *retPayloadArrType = retEnumType->getElementType( 1 );
						llvm::Value *retPayloadPtr = mBuilder->CreateStructGEP(
							retEnumType, retAlloca, 1, "try.ret.payload" );
						llvm::Value *retByte = mBuilder->CreateGEP(
							retPayloadArrType, retPayloadPtr,
							{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
							  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
							"try.ret.byte" );
						mBuilder->CreateStore( errPayload, retByte );
					}

					llvm::Value *retVal = mBuilder->CreateLoad(
						retEnumType, retAlloca, "try.ret.val" );
					mBuilder->CreateRet( retVal );
				}
				else
				{
					// Can't find error variant in return type — return null
					mBuilder->CreateRet( llvm::Constant::getNullValue( funcRetType ) );
				}
			}
			else
			{
				// Return type is not a known enum — return null
				mBuilder->CreateRet( llvm::Constant::getNullValue( funcRetType ) );
			}
		}
	}
	else
	{
		// Current function returns a non-enum type — shouldn't use ? but handle gracefully
		if ( funcRetType->isVoidTy() )
			mBuilder->CreateRetVoid();
		else
			mBuilder->CreateRet( llvm::Constant::getNullValue( funcRetType ) );
	}

	// ---- Success branch: extract the unwrapped payload value ----
	mBuilder->SetInsertPoint( okBB );

	llvm::Type *successPayloadType = llvm::Type::getInt32Ty( *mContext ); // default
	if ( !enumDef->mVariants[successIdx].mAssociatedTypes.empty() )
		successPayloadType = getLLVMType( enumDef->mVariants[successIdx].mAssociatedTypes[0] );

	// GEP to the payload area and load the success value
	llvm::Type *payloadArrType = enumType->getElementType( 1 );
	llvm::Value *payloadPtr = mBuilder->CreateStructGEP(
		enumType, enumAlloca, 1, "try.ok.payload.ptr" );
	llvm::Value *bytePtr = mBuilder->CreateGEP(
		payloadArrType, payloadPtr,
		{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ),
		  llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 0 ) },
		"try.ok.byte" );
	llvm::Value *unwrapped = mBuilder->CreateLoad(
		successPayloadType, bytePtr, "try.ok.val" );

	mBuilder->CreateBr( mergeBB );

	// ---- Merge block: the unwrapped value flows through ----
	mBuilder->SetInsertPoint( mergeBB );

	// Create a phi node for the unwrapped value (only from okBB)
	llvm::PHINode *phi = mBuilder->CreatePHI( successPayloadType, 1, "try.unwrapped" );
	phi->addIncoming( unwrapped, okBB );

	return phi;
}

// ---- Array codegen ----

llvm::Value *CodeGen::genArrayLiteral( ArrayLiteralExpression *expr )
{
	int numElements = static_cast<int>( expr->mElements.size() );

	// Generate first element to determine type
	llvm::Value *firstElem = nullptr;
	llvm::Type *elemLLVMType = llvm::Type::getInt32Ty( *mContext ); // default
	int elemSize = 4;

	if ( numElements > 0 )
	{
		firstElem = genExpression( expr->mElements[0] );
		if ( firstElem != nullptr )
		{
			elemLLVMType = firstElem->getType();
			llvm::DataLayout dl( mModule.get() );
			elemSize = dl.getTypeAllocSize( elemLLVMType );
		}
	}
	else if ( mArrayElemTypeHint != nullptr )
	{
		// Empty array literal with type hint from variable declaration
		// (e.g., Array<string> keys = []; needs elem_size = 8 for pointers)
		elemLLVMType = mArrayElemTypeHint;
		llvm::DataLayout dl( mModule.get() );
		elemSize = dl.getTypeAllocSize( elemLLVMType );
		mArrayElemTypeHint = nullptr;
	}

	// Create BlangArray: __blang_array_create(elem_size, capacity)
	llvm::Function *createFn = getOrDeclareArrayCreate();
	llvm::Value *elemSizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt32Ty( *mContext ), elemSize );
	llvm::Value *capVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), numElements > 0 ? numElements : 8 );
	llvm::Value *arr = mBuilder->CreateCall( createFn, { elemSizeVal, capVal }, "arr" );

	// Push elements
	if ( numElements > 0 )
	{
		llvm::Function *pushFn = getOrDeclareArrayPush();

		// Push first element
		llvm::AllocaInst *tmpAlloca = mBuilder->CreateAlloca( elemLLVMType, nullptr, "arr.tmp" );
		mBuilder->CreateStore( firstElem, tmpAlloca );
		mBuilder->CreateCall( pushFn, { arr, tmpAlloca } );

		// Push remaining elements
		for ( int i = 1; i < numElements; i++ )
		{
			llvm::Value *elemVal = genExpression( expr->mElements[i] );
			if ( elemVal == nullptr )
				continue;
			mBuilder->CreateStore( elemVal, tmpAlloca );
			mBuilder->CreateCall( pushFn, { arr, tmpAlloca } );
		}
	}

	// Set element destructor for arrays with refcounted element types
	if ( !mArrayElemTypeNameHint.empty() )
	{
		emitArrayElemDtor( arr, mArrayElemTypeNameHint );
		mArrayElemTypeNameHint.clear();
	}

	return arr;
}

llvm::Value *CodeGen::genIndexExpression( IndexExpression *expr )
{
	llvm::Value *objVal = genExpression( expr->mObject );
	if ( objVal == nullptr )
		return nullptr;

	llvm::Value *idxVal = genExpression( expr->mIndex );
	if ( idxVal == nullptr )
		return nullptr;

	// Check if this is a string index (string[i] -> char)
	if ( isStringType( expr->mObject ) )
	{
		// Extend index to i64 if needed
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );
		llvm::Function *charAtFn = getOrDeclareStringCharAt();
		return mBuilder->CreateCall( charAtFn, { objVal, idxVal }, "char.at" );
	}

	// Check if this is an Array index
	if ( isArrayType( expr->mObject ) )
	{
		// Extend index to i64 if needed
		if ( !idxVal->getType()->isIntegerTy( 64 ) )
			idxVal = mBuilder->CreateSExt( idxVal,
				llvm::Type::getInt64Ty( *mContext ), "idx.ext" );

		// Determine element type from the Array<T> type annotation
		llvm::Type *elemType = llvm::Type::getInt32Ty( *mContext ); // default
		if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
			{
				Type *elemTypeParam = varType->getTypeParam( 0 );
				// Check substitution map for generic type params
				string elemTypeName = elemTypeParam->getName();
				auto subIt = mTypeSubstitution.find( elemTypeName );
				if ( subIt != mTypeSubstitution.end() )
					elemType = getLLVMType( subIt->second );
				else
					elemType = getLLVMType( elemTypeParam );
			}
		}
		else if ( auto *fa = dynamic_cast<FieldAccessExpression*>( (Expression*)expr->mObject ) )
		{
			// Field access on struct (e.g., self.keys[i])
			Type *fieldType = getFieldType( fa );
			if ( fieldType != nullptr && fieldType->getNumTypeParams() > 0 )
			{
				Type *elemTypeParam = fieldType->getTypeParam( 0 );
				string elemTypeName = elemTypeParam->getName();
				auto subIt = mTypeSubstitution.find( elemTypeName );
				if ( subIt != mTypeSubstitution.end() )
					elemType = getLLVMType( subIt->second );
				else
					elemType = getLLVMType( elemTypeParam );
			}
		}

		llvm::AllocaInst *outAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, "arr.out" );
		llvm::Function *getFn = getOrDeclareArrayGet();
		mBuilder->CreateCall( getFn, { objVal, idxVal, outAlloca } );
		return mBuilder->CreateLoad( elemType, outAlloca, "arr.val" );
	}

	// Fallback: old-style stack-allocated array (for backward compat)
	if ( auto *alloca = llvm::dyn_cast<llvm::AllocaInst>( objVal ) )
	{
		llvm::Type *allocatedType = alloca->getAllocatedType();
		if ( auto *arrType = llvm::dyn_cast<llvm::ArrayType>( allocatedType ) )
		{
			llvm::Value *zero = llvm::ConstantInt::get(
				llvm::Type::getInt32Ty( *mContext ), 0 );
			llvm::Value *elemPtr = mBuilder->CreateGEP(
				arrType, alloca, { zero, idxVal }, "arr.idx" );
			return mBuilder->CreateLoad( arrType->getElementType(), elemPtr, "arr.val" );
		}
	}

	return nullptr;
}

// ---- Phase 2: Runtime helper declarations ----

llvm::Function *CodeGen::getOrDeclarePuts()
{
	llvm::Function *f = mModule->getFunction( "puts" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "puts", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareExit()
{
	llvm::Function *f = mModule->getFunction( "exit" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "exit", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclarePrintf()
{
	llvm::Function *f = mModule->getFunction( "printf" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		true /* variadic */ );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "printf", mModule.get() );
}

// ---- BLang runtime library declarations ----

llvm::Function *CodeGen::getOrDeclareRcAlloc()
{
	llvm::Function *f = mModule->getFunction( "__blang_rc_alloc" );
	if ( f != nullptr )
		return f;

	// void *__blang_rc_alloc( size_t data_size )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_rc_alloc", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareRcAllocSync()
{
	llvm::Function *f = mModule->getFunction( "__blang_rc_alloc_sync" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_rc_alloc_sync", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareRcRetain()
{
	llvm::Function *f = mModule->getFunction( "__blang_rc_retain" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_rc_retain", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareRcRelease()
{
	llvm::Function *f = mModule->getFunction( "__blang_rc_release" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_rc_release", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSyncLock()
{
	llvm::Function *f = mModule->getFunction( "__blang_sync_lock" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_sync_lock", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSyncUnlock()
{
	llvm::Function *f = mModule->getFunction( "__blang_sync_unlock" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_sync_unlock", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSysInit()
{
	llvm::Function *f = mModule->getFunction( "__blang_sys_init" );
	if ( f != nullptr )
		return f;

	// void __blang_sys_init( int argc, char **argv )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_sys_init", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareRuntimeInit()
{
	llvm::Function *f = mModule->getFunction( "__blang_runtime_init" );
	if ( f != nullptr )
		return f;

	// void __blang_runtime_init( int num_threads )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_runtime_init", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSpawn()
{
	llvm::Function *f = mModule->getFunction( "__blang_spawn" );
	if ( f != nullptr )
		return f;

	// BlangSpawnTask *__blang_spawn( void(*fn)(void*), void *ctx )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_spawn", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSpawnWait()
{
	llvm::Function *f = mModule->getFunction( "__blang_spawn_wait" );
	if ( f != nullptr )
		return f;

	// void __blang_spawn_wait( BlangSpawnTask *task )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_spawn_wait", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSpawnTaskDestroy()
{
	llvm::Function *f = mModule->getFunction( "__blang_spawn_task_destroy" );
	if ( f != nullptr )
		return f;

	// void __blang_spawn_task_destroy( BlangSpawnTask *task )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_spawn_task_destroy", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareWaitAll()
{
	llvm::Function *f = mModule->getFunction( "__blang_wait_all" );
	if ( f != nullptr )
		return f;

	// void __blang_wait_all( void )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), {}, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_wait_all", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareRuntimeShutdown()
{
	llvm::Function *f = mModule->getFunction( "__blang_runtime_shutdown" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), {}, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_runtime_shutdown", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareChanCreate()
{
	llvm::Function *f = mModule->getFunction( "__blang_chan_create" );
	if ( f != nullptr )
		return f;

	// BlangChan *__blang_chan_create( size_t elem_size, size_t capacity )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_chan_create", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareChanSend()
{
	llvm::Function *f = mModule->getFunction( "__blang_chan_send" );
	if ( f != nullptr )
		return f;

	// void __blang_chan_send( BlangChan *ch, const void *data )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_chan_send", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareChanRecv()
{
	llvm::Function *f = mModule->getFunction( "__blang_chan_recv" );
	if ( f != nullptr )
		return f;

	// int __blang_chan_recv( BlangChan *ch, void *data_out )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_chan_recv", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareChanClose()
{
	llvm::Function *f = mModule->getFunction( "__blang_chan_close" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_chan_close", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareChanDestroy()
{
	llvm::Function *f = mModule->getFunction( "__blang_chan_destroy" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_chan_destroy", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareAsyncCall()
{
	llvm::Function *f = mModule->getFunction( "__blang_async_call" );
	if ( f != nullptr )
		return f;

	// BlangTask *__blang_async_call( void*(*fn)(void*), void *arg )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_async_call", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareAwait()
{
	llvm::Function *f = mModule->getFunction( "__blang_await" );
	if ( f != nullptr )
		return f;

	// void *__blang_await( BlangTask *task )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_await", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareTaskDestroy()
{
	llvm::Function *f = mModule->getFunction( "__blang_task_destroy" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_task_destroy", mModule.get() );
}

// ---- Phase 2: Assert statement codegen ----

void CodeGen::genAssertStatement( AssertStatement *assertStmt )
{
	llvm::Value *condVal = genExpression( assertStmt->mExpression );
	if ( condVal == nullptr )
		return;

	// Convert condition to i1 if needed
	if ( !condVal->getType()->isIntegerTy( 1 ) )
	{
		condVal = mBuilder->CreateICmpNE(
			condVal,
			llvm::ConstantInt::get( condVal->getType(), 0 ),
			"assertcond" );
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *failBB = llvm::BasicBlock::Create( *mContext, "assert.fail", func );
	llvm::BasicBlock *passBB = llvm::BasicBlock::Create( *mContext, "assert.pass", func );

	mBuilder->CreateCondBr( condVal, passBB, failBB );

	// Fail block: print message and exit(1)
	mBuilder->SetInsertPoint( failBB );

	llvm::Function *putsFunc = getOrDeclarePuts();
	llvm::Function *exitFunc = getOrDeclareExit();

	std::string msg = assertStmt->mMessage.empty()
		? "Assertion failed" : assertStmt->mMessage;
	llvm::Value *msgVal = mBuilder->CreateGlobalStringPtr( msg, "assert.msg" );
	mBuilder->CreateCall( putsFunc, { msgVal } );
	mBuilder->CreateCall( exitFunc,
		{ llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 1 ) } );
	mBuilder->CreateUnreachable();

	// Continue in pass block
	mBuilder->SetInsertPoint( passBB );
}

// ---- Phase 2: Contract check codegen ----

void CodeGen::genContractCheck( Expression *condition, const std::string &message )
{
	llvm::Value *condVal = genExpression( condition );
	if ( condVal == nullptr )
		return;

	// Convert to i1 if needed
	if ( !condVal->getType()->isIntegerTy( 1 ) )
	{
		condVal = mBuilder->CreateICmpNE(
			condVal,
			llvm::ConstantInt::get( condVal->getType(), 0 ),
			"contractcond" );
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *failBB = llvm::BasicBlock::Create( *mContext, "contract.fail", func );
	llvm::BasicBlock *passBB = llvm::BasicBlock::Create( *mContext, "contract.pass", func );

	mBuilder->CreateCondBr( condVal, passBB, failBB );

	// Fail block: print message and exit(1)
	mBuilder->SetInsertPoint( failBB );

	llvm::Function *putsFunc = getOrDeclarePuts();
	llvm::Function *exitFunc = getOrDeclareExit();

	llvm::Value *msgVal = mBuilder->CreateGlobalStringPtr( message, "contract.msg" );
	mBuilder->CreateCall( putsFunc, { msgVal } );
	mBuilder->CreateCall( exitFunc,
		{ llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 1 ) } );
	mBuilder->CreateUnreachable();

	// Continue in pass block
	mBuilder->SetInsertPoint( passBB );
}

// ---- Phase 2: Spawn statement codegen ----

llvm::Value *CodeGen::genSpawnStatement( SpawnStatement *spawn )
{
	if ( spawn->mBody == nullptr )
		return nullptr;

	mUsesConcurrency = true;

	// Identify captured variables: variables referenced in the spawn body
	// that are defined in an outer scope. We scan the block's scope for
	// all variables and check which ones have allocas in mVariableMap
	// (meaning they were defined before the spawn block).
	Block *bodyBlock = spawn->mBody;
	std::vector<std::pair<VariableDefinition*, llvm::AllocaInst*>> captures;

	// Collect outer variables used in this scope, enforcing ownership rules.
	// Skip own variables — they cannot cross spawn boundaries. If the spawn
	// body actually references an own variable, genVariableExpression will
	// catch it via mSpawnOuterOwnVars.
	if ( bodyBlock->mScope != nullptr )
	{
		for ( auto &entry : mVariableMap )
		{
			OwnershipQualifier capOwnership = entry.first->getOwnership();

			// Skip own variables — they will be checked at use-site
			if ( capOwnership == OwnershipQualifier::kOwnership_Own )
				continue;

			captures.push_back( { entry.first, entry.second } );
		}
	}

	// Track which captures are shared/sync for RC retain/release
	std::vector<size_t> rcCaptureIndices;
	for ( size_t i = 0; i < captures.size(); i++ )
	{
		OwnershipQualifier capOwnership = captures[i].first->getOwnership();
		if ( capOwnership == OwnershipQualifier::kOwnership_Shared ||
			 capOwnership == OwnershipQualifier::kOwnership_Sync )
		{
			rcCaptureIndices.push_back( i );
		}
	}

	// Create the context struct type
	std::vector<llvm::Type*> captureTypes;
	for ( auto &cap : captures )
	{
		captureTypes.push_back( cap.second->getAllocatedType() );
	}

	llvm::StructType *ctxType = llvm::StructType::create(
		*mContext, captureTypes, "spawn.ctx" );

	// Generate a unique spawn function name
	static int spawnCounter = 0;
	string spawnFnName = "__blang_spawn_body_" + to_string( spawnCounter++ );

	// Create the spawn body function: void spawn_body(void* ctx)
	llvm::FunctionType *spawnFnType = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	llvm::Function *spawnFn = llvm::Function::Create(
		spawnFnType, llvm::Function::InternalLinkage, spawnFnName, mModule.get() );
	spawnFn->getArg( 0 )->setName( "ctx" );

	// Save current state
	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	auto savedVarMap = mVariableMap;
	auto savedLoopStack = mLoopStack;
	auto savedArcStack = mArcScopeStack;
	auto savedStringStack = mStringScopeStack;
	auto savedArrayStack = mArrayScopeStack;
	auto savedBufferStack = mBufferScopeStack;
	auto savedLambdaStack = mLambdaScopeStack;
	auto savedStructStack = mStructScopeStack;
	auto savedEnumStack = mEnumScopeStack;
	auto savedTempStrings = mTempStrings;
	auto savedTempLambdaCtxs = mTempLambdaCtxs;
	auto savedSpawnOwnVars = mSpawnOuterOwnVars;

	// Generate the spawn function body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", spawnFn );
	mBuilder->SetInsertPoint( entryBB );

	mVariableMap.clear();
	mMovedVariables.clear();
	mLoopStack.clear();
	mArcScopeStack.clear();
	mStringScopeStack.clear();
	mArrayScopeStack.clear();
	mBufferScopeStack.clear();
	mLambdaScopeStack.clear();
	mStructScopeStack.clear();
	mEnumScopeStack.clear();
	mTempStrings.clear();
	mTempLambdaCtxs.clear();

	// Track own variables from the outer scope for spawn boundary checks.
	// These were excluded from captures but need to be tracked so that
	// genVariableExpression can reject references to them inside the spawn body.
	mSpawnOuterOwnVars.clear();
	for ( auto &entry : savedVarMap )
	{
		if ( entry.first->getOwnership() == OwnershipQualifier::kOwnership_Own )
			mSpawnOuterOwnVars.insert( entry.first );
	}

	// Unpack the context struct into local allocas
	llvm::Value *ctxPtr = spawnFn->getArg( 0 );
	for ( size_t i = 0; i < captures.size(); i++ )
	{
		llvm::Type *fieldType = captureTypes[i];
		llvm::AllocaInst *localAlloca = mBuilder->CreateAlloca(
			fieldType, nullptr, captures[i].first->getName() );

		llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
			ctxType, ctxPtr, static_cast<unsigned>( i ), "ctx.field" );
		llvm::Value *fieldVal = mBuilder->CreateLoad( fieldType, fieldPtr, "ctx.val" );
		mBuilder->CreateStore( fieldVal, localAlloca );

		mVariableMap[captures[i].first] = localAlloca;
	}

	// Generate the spawn body
	genBlock( bodyBlock );

	// Release RC for shared/sync captured variables before returning from spawn
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
	{
		for ( size_t idx : rcCaptureIndices )
		{
			llvm::AllocaInst *localAlloca = mVariableMap[captures[idx].first];
			llvm::Value *heapPtr = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), localAlloca, "rc.spawn.rel" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { heapPtr } );
		}
	}

	// Add implicit return
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateRetVoid();

	// Restore state
	mBuilder->SetInsertPoint( savedBB );
	mVariableMap = savedVarMap;
	mLoopStack = savedLoopStack;
	mArcScopeStack = savedArcStack;
	mStringScopeStack = savedStringStack;
	mArrayScopeStack = savedArrayStack;
	mBufferScopeStack = savedBufferStack;
	mLambdaScopeStack = savedLambdaStack;
	mStructScopeStack = savedStructStack;
	mEnumScopeStack = savedEnumStack;
	mTempStrings = savedTempStrings;
	mTempLambdaCtxs = savedTempLambdaCtxs;
	mSpawnOuterOwnVars = savedSpawnOwnVars;

	// Back in the caller: allocate context, populate, and call __blang_spawn
	llvm::DataLayout dl( mModule.get() );
	uint64_t ctxSize = dl.getTypeAllocSize( ctxType );

	llvm::Function *mallocFn = getOrDeclareBlangAlloc();
	llvm::Value *ctxAlloc = mBuilder->CreateCall( mallocFn,
		{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), ctxSize ) },
		"spawn.ctx" );

	// Store captured variable values into the context
	for ( size_t i = 0; i < captures.size(); i++ )
	{
		llvm::AllocaInst *alloca = captures[i].second;
		llvm::Type *fieldType = captureTypes[i];
		llvm::Value *val = mBuilder->CreateLoad( fieldType, alloca, "cap.val" );

		llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
			ctxType, ctxAlloc, static_cast<unsigned>( i ), "ctx.store" );
		mBuilder->CreateStore( val, fieldPtr );
	}

	// Retain RC for shared/sync captures to prevent use-after-free
	// (the main scope might release before the spawn completes)
	for ( size_t idx : rcCaptureIndices )
	{
		llvm::AllocaInst *alloca = captures[idx].second;
		llvm::Value *val = mBuilder->CreateLoad(
			alloca->getAllocatedType(), alloca, "rc.spawn.ret" );
		mBuilder->CreateCall( getOrDeclareRcRetain(), { val } );
	}

	// Call __blang_spawn(spawn_body, ctx) — returns BlangSpawnTask*
	llvm::Value *taskHandle = mBuilder->CreateCall(
		getOrDeclareSpawn(), { spawnFn, ctxAlloc }, "spawn.task" );
	return taskHandle;
}

// ---- Lambda capture analysis ----

// Walk an AST subtree and collect all VariableDefinition* references.
// Used to determine which outer-scope variables a lambda actually uses,
// so we only capture those (instead of blindly capturing everything).
void CodeGen::collectReferencedVars( Statement *stmt, std::set<VariableDefinition*> &vars )
{
	if ( stmt == nullptr )
		return;

	// --- Leaf nodes that reference variables ---
	if ( auto *ve = dynamic_cast<VariableExpression*>( stmt ) )
	{
		vars.insert( ve->mVariable );
		return;
	}
	if ( auto *ae = dynamic_cast<AssignmentExpression*>( stmt ) )
	{
		vars.insert( ae->mVariable );
		collectReferencedVars( ae->mValue, vars );
		return;
	}
	if ( auto *ic = dynamic_cast<IndirectCallExpression*>( stmt ) )
	{
		vars.insert( ic->mFnVariable );
		for ( auto &p : ic->mParams )
			collectReferencedVars( p, vars );
		return;
	}

	// --- Compound statements ---
	if ( auto *block = dynamic_cast<Block*>( stmt ) )
	{
		for ( auto &s : block->mStatementList )
			collectReferencedVars( s, vars );
		return;
	}
	if ( auto *ifStmt = dynamic_cast<IfStatement*>( stmt ) )
	{
		collectReferencedVars( ifStmt->mIfExpression, vars );
		collectReferencedVars( ifStmt->mStatement, vars );
		collectReferencedVars( ifStmt->mElseStatement, vars );
		return;
	}
	if ( auto *whileStmt = dynamic_cast<WhileStatement*>( stmt ) )
	{
		collectReferencedVars( whileStmt->mLoopExpression, vars );
		collectReferencedVars( whileStmt->mLoopStatement, vars );
		return;
	}
	if ( auto *forIn = dynamic_cast<ForInStatement*>( stmt ) )
	{
		collectReferencedVars( forIn->mIterableExpression, vars );
		collectReferencedVars( forIn->mBody, vars );
		return;
	}
	if ( auto *ret = dynamic_cast<ReturnStatement*>( stmt ) )
	{
		collectReferencedVars( ret->mExpression, vars );
		return;
	}
	if ( auto *call = dynamic_cast<CallExpression*>( stmt ) )
	{
		for ( auto &p : call->mParams )
			collectReferencedVars( p, vars );
		return;
	}

	// --- Binary/unary expressions ---
	if ( auto *ops = dynamic_cast<OperationsExpression*>( stmt ) )
	{
		collectReferencedVars( ops->mOp1, vars );
		collectReferencedVars( ops->mOp2, vars );
		return;
	}
	if ( auto *unary = dynamic_cast<UnaryExpression*>( stmt ) )
	{
		collectReferencedVars( unary->mOperand, vars );
		return;
	}

	// --- Field/method/index access ---
	if ( auto *fa = dynamic_cast<FieldAccessExpression*>( stmt ) )
	{
		collectReferencedVars( fa->mObject, vars );
		return;
	}
	if ( auto *mc = dynamic_cast<MethodCallExpression*>( stmt ) )
	{
		collectReferencedVars( mc->mObject, vars );
		for ( auto &a : mc->mArgs )
			collectReferencedVars( a, vars );
		return;
	}
	if ( auto *idx = dynamic_cast<IndexExpression*>( stmt ) )
	{
		collectReferencedVars( idx->mObject, vars );
		collectReferencedVars( idx->mIndex, vars );
		return;
	}

	// --- Struct, array, enum literals ---
	if ( auto *sl = dynamic_cast<StructLiteralExpression*>( stmt ) )
	{
		for ( auto &v : sl->mFieldValues )
			collectReferencedVars( v, vars );
		return;
	}
	if ( auto *al = dynamic_cast<ArrayLiteralExpression*>( stmt ) )
	{
		for ( auto &e : al->mElements )
			collectReferencedVars( e, vars );
		return;
	}
	if ( auto *ec = dynamic_cast<EnumConstructExpression*>( stmt ) )
	{
		for ( auto &a : ec->mArgs )
			collectReferencedVars( a, vars );
		return;
	}

	// --- String interpolation ---
	if ( auto *si = dynamic_cast<StringInterpolation*>( stmt ) )
	{
		for ( auto &p : si->mParts )
			collectReferencedVars( p, vars );
		return;
	}

	// --- Variable declarations ---
	if ( auto *vd = dynamic_cast<VariableDeclaration*>( stmt ) )
	{
		for ( auto &d : vd->mVariables )
			collectReferencedVars( d.mInitialValue, vars );
		return;
	}

	// --- Assignment variants ---
	if ( auto *fae = dynamic_cast<FieldAssignmentExpression*>( stmt ) )
	{
		collectReferencedVars( fae->mObject, vars );
		collectReferencedVars( fae->mValue, vars );
		return;
	}
	if ( auto *iae = dynamic_cast<IndexAssignmentExpression*>( stmt ) )
	{
		collectReferencedVars( iae->mObject, vars );
		collectReferencedVars( iae->mIndex, vars );
		collectReferencedVars( iae->mValue, vars );
		return;
	}

	// --- Match/try/await ---
	if ( auto *match = dynamic_cast<MatchExpression*>( stmt ) )
	{
		collectReferencedVars( match->mSubject, vars );
		for ( auto &arm : match->mArms )
			collectReferencedVars( arm.mBody, vars );
		return;
	}
	if ( auto *tryExpr = dynamic_cast<TryExpression*>( stmt ) )
	{
		collectReferencedVars( tryExpr->mOperand, vars );
		return;
	}
	if ( auto *awaitExpr = dynamic_cast<AwaitExpression*>( stmt ) )
	{
		collectReferencedVars( awaitExpr->mOperand, vars );
		return;
	}

	// --- Pipeline ---
	if ( auto *pipe = dynamic_cast<PipelineExpression*>( stmt ) )
	{
		collectReferencedVars( pipe->mInput, vars );
		collectReferencedVars( pipe->mTransform, vars );
		return;
	}

	// --- Range ---
	if ( auto *range = dynamic_cast<RangeExpression*>( stmt ) )
	{
		collectReferencedVars( range->mStart, vars );
		collectReferencedVars( range->mEnd, vars );
		return;
	}

	// --- Spawn/event/assert ---
	if ( auto *spawn = dynamic_cast<SpawnStatement*>( stmt ) )
	{
		collectReferencedVars( spawn->mBody, vars );
		return;
	}
	if ( auto *eh = dynamic_cast<EventHandler*>( stmt ) )
	{
		collectReferencedVars( eh->mEventExpression, vars );
		collectReferencedVars( eh->mBody, vars );
		return;
	}
	if ( auto *assertStmt = dynamic_cast<AssertStatement*>( stmt ) )
	{
		collectReferencedVars( assertStmt->mExpression, vars );
		return;
	}
	if ( auto *wait = dynamic_cast<WaitStatement*>( stmt ) )
	{
		collectReferencedVars( wait->mExpr, vars );
		return;
	}

	// --- Nested lambdas: recurse to find transitive captures ---
	if ( auto *innerLambda = dynamic_cast<LambdaExpression*>( stmt ) )
	{
		collectReferencedVars( innerLambda->mBody, vars );
		return;
	}

	// ConstExpression, BreakStatement, ContinueStatement, WaitAllStatement,
	// FunctionRefExpression — no variable references to collect
}

// ---- Lambda context destructor generation ----

// Generate a destructor function for a lambda context struct.
// The destructor releases any captured refcounted types (string, array,
// buffer, fn-typed) when the context refcount reaches zero.
// Context layout: { i64 refcount, ptr destructor, ...captured_fields... }
// Captured fields start at index 2.
llvm::Function *CodeGen::genLambdaDestructor(
	const std::string &name,
	llvm::StructType *ctxType,
	const std::vector<std::pair<VariableDefinition*, llvm::AllocaInst*>> &captures,
	const std::vector<llvm::Type*> &captureTypes )
{
	// Check if any captures need release
	bool needsDestructor = false;
	for ( size_t i = 0; i < captures.size(); i++ )
	{
		VariableDefinition *varDef = captures[i].first;
		Type *varType = varDef->getVariableType();
		if ( varType == nullptr )
			continue;
		string typeName = varType->getName();
		if ( typeName == "string" || typeName == "Array" ||
			 typeName == "Buffer" || varType->isFunctionType() )
		{
			needsDestructor = true;
			break;
		}
		OwnershipQualifier ownership = varDef->getOwnership();
		if ( ownership == OwnershipQualifier::kOwnership_Shared ||
			 ownership == OwnershipQualifier::kOwnership_Sync )
		{
			needsDestructor = true;
			break;
		}
	}

	if ( !needsDestructor )
		return nullptr;

	// Create destructor function: void dtor(void* ctx)
	llvm::FunctionType *dtorType = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	llvm::Function *dtorFn = llvm::Function::Create(
		dtorType, llvm::Function::InternalLinkage, name, mModule.get() );

	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", dtorFn );
	mBuilder->SetInsertPoint( entryBB );

	llvm::Value *ctxPtr = dtorFn->getArg( 0 );

	for ( size_t i = 0; i < captures.size(); i++ )
	{
		VariableDefinition *varDef = captures[i].first;
		Type *varType = varDef->getVariableType();
		if ( varType == nullptr )
			continue;

		string typeName = varType->getName();
		unsigned fieldIdx = static_cast<unsigned>( i + 2 ); // +2 for refcount + destructor header

		OwnershipQualifier ownership = varDef->getOwnership();
		if ( ownership == OwnershipQualifier::kOwnership_Shared ||
			 ownership == OwnershipQualifier::kOwnership_Sync )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxPtr, fieldIdx, "dtor.arc.ptr" );
			llvm::Value *val = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), fieldPtr, "dtor.arc.val" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { val } );
		}
		else if ( typeName == "string" )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxPtr, fieldIdx, "dtor.str.ptr" );
			llvm::Value *val = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), fieldPtr, "dtor.str.val" );
			mBuilder->CreateCall( getOrDeclareStringRelease(), { val } );
		}
		else if ( typeName == "Array" )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxPtr, fieldIdx, "dtor.arr.ptr" );
			llvm::Value *val = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), fieldPtr, "dtor.arr.val" );
			mBuilder->CreateCall( getOrDeclareArrayRelease(), { val } );
		}
		else if ( typeName == "Buffer" )
		{
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxPtr, fieldIdx, "dtor.buf.ptr" );
			llvm::Value *val = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), fieldPtr, "dtor.buf.val" );
			mBuilder->CreateCall( getOrDeclareBufferRelease(), { val } );
		}
		else if ( varType->isFunctionType() )
		{
			// fn-typed capture: extract ctx_ptr (field 1 of {fn_ptr, ctx_ptr}) and release
			llvm::Type *pairType = captureTypes[i]; // {ptr, ptr}
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxPtr, fieldIdx, "dtor.fn.ptr" );
			llvm::Value *pairVal = mBuilder->CreateLoad( pairType, fieldPtr, "dtor.fn.val" );
			llvm::Value *innerCtx = mBuilder->CreateExtractValue( pairVal, 1, "dtor.fn.ctx" );
			mBuilder->CreateCall( getOrDeclareLambdaCtxRelease(), { innerCtx } );
		}
	}

	mBuilder->CreateRetVoid();
	mBuilder->SetInsertPoint( savedBB );
	return dtorFn;
}

// ---- Lambda expression codegen ----

llvm::Value *CodeGen::genLambdaExpression( LambdaExpression *lambda )
{
	if ( lambda->mBody == nullptr )
		return nullptr;

	// --- Capture analysis: only capture outer variables actually referenced ---
	std::set<VariableDefinition*> referencedVars;
	collectReferencedVars( lambda->mBody, referencedVars );

	std::vector<std::pair<VariableDefinition*, llvm::AllocaInst*>> captures;
	for ( auto &entry : mVariableMap )
	{
		// Only capture variables that are actually referenced in the body
		if ( referencedVars.count( entry.first ) == 0 )
			continue;

		// Skip lambda's own parameters (not in mVariableMap yet, but just in case)
		bool isOwnParam = false;
		for ( auto &param : lambda->mParameters )
		{
			if ( (VariableDefinition*)param == entry.first )
			{
				isOwnParam = true;
				break;
			}
		}
		if ( isOwnParam )
			continue;

		// Ownership check: own refcounted types cannot be captured
		OwnershipQualifier ownership = entry.first->getOwnership();
		if ( ownership == OwnershipQualifier::kOwnership_Own )
		{
			Type *varType = entry.first->getVariableType();
			if ( varType != nullptr )
			{
				string typeName = varType->getName();
				if ( typeName == "string" || typeName == "Array" ||
					 typeName == "Buffer" || varType->isFunctionType() )
				{
					cerr << "error: own variable '" << entry.first->getName()
						 << "' cannot be captured by lambda (use shared instead)" << endl;
					mHasError = true;
					continue;
				}
			}
		}

		captures.push_back( { entry.first, entry.second } );
	}

	// Build context struct type with refcount header:
	//   { i64 refcount, ptr destructor, ...captured_fields... }
	std::vector<llvm::Type*> captureTypes;
	for ( auto &cap : captures )
		captureTypes.push_back( cap.second->getAllocatedType() );

	// Full struct includes header (refcount + destructor ptr) + captured fields
	std::vector<llvm::Type*> ctxFields;
	ctxFields.push_back( llvm::Type::getInt64Ty( *mContext ) );    // refcount
	ctxFields.push_back( llvm::PointerType::get( *mContext, 0 ) ); // destructor
	for ( auto &ct : captureTypes )
		ctxFields.push_back( ct );

	llvm::StructType *ctxType = nullptr;
	if ( !captures.empty() )
		ctxType = llvm::StructType::create( *mContext, ctxFields, "lambda.ctx" );

	// Determine the lambda function signature:
	// RetType __blang_lambda_N(void* ctx, ParamType1, ParamType2, ...)
	llvm::Type *retType = lambda->mReturnType != nullptr
		? getLLVMType( lambda->mReturnType )
		: llvm::Type::getVoidTy( *mContext );

	std::vector<llvm::Type*> fnParamTypes;
	fnParamTypes.push_back( llvm::PointerType::get( *mContext, 0 ) ); // ctx ptr
	for ( auto &param : lambda->mParameters )
		fnParamTypes.push_back( getLLVMType( param->getVariableType() ) );

	llvm::FunctionType *fnType = llvm::FunctionType::get( retType, fnParamTypes, false );

	string lambdaName = "__blang_lambda_" + to_string( mLambdaCounter++ );
	llvm::Function *lambdaFn = llvm::Function::Create(
		fnType, llvm::Function::InternalLinkage, lambdaName, mModule.get() );
	lambdaFn->getArg( 0 )->setName( "ctx" );
	for ( int i = 0; i < (int)lambda->mParameters.size(); i++ )
		lambdaFn->getArg( i + 1 )->setName( lambda->mParameters[i]->getName() );

	// Generate destructor for this lambda's context (releases captured refcounted types)
	llvm::Function *dtorFn = nullptr;
	if ( ctxType != nullptr )
	{
		dtorFn = genLambdaDestructor(
			lambdaName + "_dtor", ctxType, captures, captureTypes );
	}

	// Save codegen state
	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	auto savedVarMap = mVariableMap;
	auto savedLoopStack = mLoopStack;
	auto savedArcStack = mArcScopeStack;
	auto savedStringStack = mStringScopeStack;
	auto savedArrayStack = mArrayScopeStack;
	auto savedBufferStack = mBufferScopeStack;
	auto savedLambdaStack = mLambdaScopeStack;
	auto savedStructStack = mStructScopeStack;
	auto savedEnumStack = mEnumScopeStack;
	auto savedTempStrings = mTempStrings;
	auto savedTempLambdaCtxs = mTempLambdaCtxs;
	auto savedCurrentFunc = mCurrentFunction;
	auto savedResultAlloca = mResultAlloca;

	// Generate the lambda function body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", lambdaFn );
	mBuilder->SetInsertPoint( entryBB );

	mVariableMap.clear();
	mMovedVariables.clear();
	mLoopStack.clear();
	mArcScopeStack.clear();
	mStringScopeStack.clear();
	mArrayScopeStack.clear();
	mBufferScopeStack.clear();
	mLambdaScopeStack.clear();
	mStructScopeStack.clear();
	mEnumScopeStack.clear();
	mTempStrings.clear();
	mTempLambdaCtxs.clear();
	mCurrentFunction = nullptr;
	mResultAlloca = nullptr;

	// Unpack captures from context struct (fields start at index 2, after header)
	if ( ctxType != nullptr )
	{
		llvm::Value *ctxPtr = lambdaFn->getArg( 0 );
		for ( size_t i = 0; i < captures.size(); i++ )
		{
			llvm::Type *fieldType = captureTypes[i];
			llvm::AllocaInst *localAlloca = mBuilder->CreateAlloca(
				fieldType, nullptr, captures[i].first->getName() );

			unsigned fieldIdx = static_cast<unsigned>( i + 2 ); // +2 for header
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxPtr, fieldIdx, "ctx.field" );
			llvm::Value *fieldVal = mBuilder->CreateLoad( fieldType, fieldPtr, "ctx.val" );
			mBuilder->CreateStore( fieldVal, localAlloca );

			mVariableMap[captures[i].first] = localAlloca;
		}
	}

	// Create allocas for lambda parameters
	for ( int i = 0; i < (int)lambda->mParameters.size(); i++ )
	{
		VariableDefinition *paramDef = lambda->mParameters[i];
		llvm::Value *arg = lambdaFn->getArg( i + 1 );
		llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
			arg->getType(), nullptr, paramDef->getName() );
		mBuilder->CreateStore( arg, alloca );
		mVariableMap[paramDef] = alloca;
	}

	// Generate body
	genBlock( lambda->mBody );

	// Add implicit return void if no terminator
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
	{
		if ( retType->isVoidTy() )
			mBuilder->CreateRetVoid();
		else
			mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
	}

	// Restore codegen state
	mBuilder->SetInsertPoint( savedBB );
	mVariableMap = savedVarMap;
	mLoopStack = savedLoopStack;
	mArcScopeStack = savedArcStack;
	mStringScopeStack = savedStringStack;
	mArrayScopeStack = savedArrayStack;
	mBufferScopeStack = savedBufferStack;
	mLambdaScopeStack = savedLambdaStack;
	mStructScopeStack = savedStructStack;
	mEnumScopeStack = savedEnumStack;
	mTempStrings = savedTempStrings;
	mTempLambdaCtxs = savedTempLambdaCtxs;
	mCurrentFunction = savedCurrentFunc;
	mResultAlloca = savedResultAlloca;

	// Back in caller: allocate context, populate, build {fn_ptr, ctx_ptr}
	llvm::Value *ctxAlloc = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );

	if ( ctxType != nullptr && !captures.empty() )
	{
		llvm::DataLayout dl( mModule.get() );
		uint64_t ctxSize = dl.getTypeAllocSize( ctxType );

		llvm::Function *mallocFn = getOrDeclareBlangAlloc();
		ctxAlloc = mBuilder->CreateCall( mallocFn,
			{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), ctxSize ) },
			"lambda.ctx" );

		// Initialize refcount = 1
		llvm::Value *rcPtr = mBuilder->CreateStructGEP(
			ctxType, ctxAlloc, 0, "ctx.rc" );
		mBuilder->CreateStore(
			llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 1 ), rcPtr );

		// Store destructor pointer (or null if no destructor needed)
		llvm::Value *dtorPtr = mBuilder->CreateStructGEP(
			ctxType, ctxAlloc, 1, "ctx.dtor" );
		if ( dtorFn != nullptr )
			mBuilder->CreateStore( dtorFn, dtorPtr );
		else
			mBuilder->CreateStore(
				llvm::ConstantPointerNull::get( llvm::PointerType::get( *mContext, 0 ) ),
				dtorPtr );

		// Populate captured fields (at index i+2)
		for ( size_t i = 0; i < captures.size(); i++ )
		{
			llvm::AllocaInst *alloca = captures[i].second;
			llvm::Type *fieldType = captureTypes[i];
			llvm::Value *val = mBuilder->CreateLoad( fieldType, alloca, "cap.val" );

			unsigned fieldIdx = static_cast<unsigned>( i + 2 );
			llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
				ctxType, ctxAlloc, fieldIdx, "ctx.store" );
			mBuilder->CreateStore( val, fieldPtr );

			// Retain captured refcounted types
			VariableDefinition *varDef = captures[i].first;
			Type *varType = varDef->getVariableType();
			if ( varType != nullptr )
			{
				OwnershipQualifier ownership = varDef->getOwnership();
				if ( ownership == OwnershipQualifier::kOwnership_Shared ||
					 ownership == OwnershipQualifier::kOwnership_Sync )
				{
					mBuilder->CreateCall( getOrDeclareRcRetain(), { val } );
				}
				else
				{
					string typeName = varType->getName();
					if ( typeName == "string" )
						mBuilder->CreateCall( getOrDeclareStringRetain(), { val } );
					else if ( typeName == "Array" )
						mBuilder->CreateCall( getOrDeclareArrayRetain(), { val } );
					else if ( typeName == "Buffer" )
						mBuilder->CreateCall( getOrDeclareBufferRetain(), { val } );
					else if ( varType->isFunctionType() )
					{
						// Retain the inner lambda's context pointer
						llvm::Value *innerCtx = mBuilder->CreateExtractValue(
							val, 1, "cap.fn.ctx" );
						mBuilder->CreateCall( getOrDeclareLambdaCtxRetain(),
							{ innerCtx } );
					}
				}
			}
		}
	}

	// Track inline lambda context for deferred release.
	// If this lambda is stored to a variable, genVariableDeclaration will untrack it.
	if ( ctxType != nullptr && !captures.empty() )
		trackTempLambdaCtx( ctxAlloc );

	// Return {fn_ptr, ctx_ptr} struct
	llvm::Type *pairType = llvm::StructType::get( *mContext, {
		llvm::PointerType::get( *mContext, 0 ),
		llvm::PointerType::get( *mContext, 0 )
	} );
	llvm::Value *pair = llvm::UndefValue::get( pairType );
	pair = mBuilder->CreateInsertValue( pair, lambdaFn, 0, "lambda.pair.fn" );
	pair = mBuilder->CreateInsertValue( pair, ctxAlloc, 1, "lambda.pair.ctx" );
	return pair;
}

// ---- Function reference codegen ----

llvm::Value *CodeGen::genFunctionRefExpression( FunctionRefExpression *funcRef )
{
	FunctionDefinition *funcDef = funcRef->mFunction;

	// Generate or look up a thunk: RetType __blang_thunk_FuncName(void* ctx, params...)
	string thunkName = "__blang_thunk_" + funcDef->getName();
	llvm::Function *thunkFn = nullptr;

	auto it = mThunkMap.find( thunkName );
	if ( it != mThunkMap.end() )
	{
		thunkFn = it->second;
	}
	else
	{
		// Build thunk type: same as target function but with void* ctx as first param
		llvm::Type *retType = getLLVMType( funcDef->getReturnType() );

		std::vector<llvm::Type*> thunkParams;
		thunkParams.push_back( llvm::PointerType::get( *mContext, 0 ) ); // ctx (unused)
		for ( int i = 0; i < funcDef->getNumberParams(); i++ )
			thunkParams.push_back( getLLVMType( funcDef->getParamType( i ) ) );

		llvm::FunctionType *thunkType = llvm::FunctionType::get(
			retType, thunkParams, funcDef->isVariadic() );
		thunkFn = llvm::Function::Create(
			thunkType, llvm::Function::InternalLinkage, thunkName, mModule.get() );
		thunkFn->getArg( 0 )->setName( "ctx" );
		for ( int i = 0; i < funcDef->getNumberParams(); i++ )
			thunkFn->getArg( i + 1 )->setName( funcDef->getParam( i )->getName() );

		// Generate thunk body: call the real function, forwarding all args
		llvm::BasicBlock *entry = llvm::BasicBlock::Create(
			*mContext, "entry", thunkFn );
		llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
		mBuilder->SetInsertPoint( entry );

		// Look up or declare the target function
		llvm::Function *targetFn = nullptr;
		auto fIt = mFunctionMap.find( funcDef );
		if ( fIt != mFunctionMap.end() )
			targetFn = fIt->second;
		else
			targetFn = mModule->getFunction( funcDef->getName() );
		if ( targetFn == nullptr && funcDef->isExtern() )
			targetFn = genFunction( funcDef );

		std::vector<llvm::Value*> args;
		for ( int i = 0; i < funcDef->getNumberParams(); i++ )
			args.push_back( thunkFn->getArg( i + 1 ) );

		if ( retType->isVoidTy() )
		{
			mBuilder->CreateCall( targetFn, args );
			mBuilder->CreateRetVoid();
		}
		else
		{
			llvm::Value *result = mBuilder->CreateCall( targetFn, args, "thunk.call" );
			mBuilder->CreateRet( result );
		}

		mBuilder->SetInsertPoint( savedBB );
		mThunkMap[thunkName] = thunkFn;
	}

	// Return {thunk_ptr, null} pair
	llvm::Type *pairType = llvm::StructType::get( *mContext, {
		llvm::PointerType::get( *mContext, 0 ),
		llvm::PointerType::get( *mContext, 0 )
	} );
	llvm::Value *pair = llvm::UndefValue::get( pairType );
	pair = mBuilder->CreateInsertValue( pair, thunkFn, 0, "fref.pair.fn" );
	pair = mBuilder->CreateInsertValue( pair, llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) ), 1, "fref.pair.ctx" );
	return pair;
}

// ---- Indirect call codegen ----

llvm::Value *CodeGen::genIndirectCallExpression( IndirectCallExpression *indCall )
{
	VariableDefinition *fnVar = indCall->mFnVariable;
	FunctionType *fnType = dynamic_cast<FunctionType*>( fnVar->getVariableType() );
	if ( fnType == nullptr )
		return nullptr;

	// Load the {fn_ptr, ctx_ptr} pair from the variable
	auto it = mVariableMap.find( fnVar );
	if ( it == mVariableMap.end() )
	{
		cerr << "CodeGen: undefined function variable '" << fnVar->getName() << "'" << endl;
		return nullptr;
	}

	llvm::Type *pairType = it->second->getAllocatedType();
	llvm::Value *pair = mBuilder->CreateLoad( pairType, it->second, fnVar->getName() + ".pair" );
	llvm::Value *fnPtr = mBuilder->CreateExtractValue( pair, 0, "call.fn" );
	llvm::Value *ctxPtr = mBuilder->CreateExtractValue( pair, 1, "call.ctx" );

	// Build the LLVM function type: RetType(void* ctx, ParamTypes...)
	llvm::Type *retType = fnType->getReturnType() != nullptr
		? getLLVMType( fnType->getReturnType() )
		: llvm::Type::getVoidTy( *mContext );

	std::vector<llvm::Type*> callParamTypes;
	callParamTypes.push_back( llvm::PointerType::get( *mContext, 0 ) ); // ctx
	for ( int i = 0; i < fnType->getNumParamTypes(); i++ )
		callParamTypes.push_back( getLLVMType( fnType->getParamType( i ) ) );

	llvm::FunctionType *callType = llvm::FunctionType::get(
		retType, callParamTypes, false );

	// Build argument list: ctx_ptr, then user args
	std::vector<llvm::Value*> args;
	args.push_back( ctxPtr );
	for ( auto &paramExpr : indCall->mParams )
	{
		llvm::Value *argVal = genExpression( paramExpr );
		if ( argVal == nullptr )
			return nullptr;
		args.push_back( argVal );
	}

	if ( retType->isVoidTy() )
	{
		mBuilder->CreateCall( callType, fnPtr, args );
		return nullptr;
	}

	return mBuilder->CreateCall( callType, fnPtr, args, "indcall" );
}

// ---- Wait statement codegen ----

void CodeGen::genWaitStatement( WaitStatement *wait )
{
	if ( wait->mExpr == nullptr )
		return;

	mUsesConcurrency = true;

	// Generate the expression (should produce a BlangSpawnTask* pointer)
	llvm::Value *taskPtr = genExpression( wait->mExpr );
	if ( taskPtr == nullptr )
		return;

	// Call __blang_spawn_wait(task)
	mBuilder->CreateCall( getOrDeclareSpawnWait(), { taskPtr } );

	// Call __blang_spawn_task_destroy(task) to clean up the handle
	mBuilder->CreateCall( getOrDeclareSpawnTaskDestroy(), { taskPtr } );
}

void CodeGen::genWaitAllStatement( WaitAllStatement *waitAll )
{
	mUsesConcurrency = true;

	// Call __blang_wait_all()
	mBuilder->CreateCall( getOrDeclareWaitAll(), {} );
}

// ---- Phase 2: Event handler codegen ----

void CodeGen::genEventHandler( EventHandler *handler )
{
	if ( handler->mBody == nullptr )
		return;

	// Extract the handler body into a callback function: void(void*)
	static int handlerCounter = 0;
	string handlerName = "__blang_event_handler_" + to_string( handlerCounter++ );

	// Capture outer variables (same pattern as spawn)
	Block *bodyBlock = handler->mBody;
	std::vector<std::pair<VariableDefinition*, llvm::AllocaInst*>> captures;
	for ( auto &entry : mVariableMap )
		captures.push_back( { entry.first, entry.second } );

	std::vector<llvm::Type*> captureTypes;
	for ( auto &cap : captures )
		captureTypes.push_back( cap.second->getAllocatedType() );

	llvm::StructType *ctxType = llvm::StructType::create(
		*mContext, captureTypes, handlerName + ".ctx" );

	// Create callback function
	llvm::FunctionType *cbType = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	llvm::Function *cbFn = llvm::Function::Create(
		cbType, llvm::Function::InternalLinkage, handlerName, mModule.get() );
	cbFn->getArg( 0 )->setName( "ctx" );

	// Save state
	llvm::BasicBlock *savedBB = mBuilder->GetInsertBlock();
	auto savedVarMap = mVariableMap;
	auto savedLoopStack = mLoopStack;
	auto savedArcStack = mArcScopeStack;
	auto savedStringStack = mStringScopeStack;
	auto savedArrayStack = mArrayScopeStack;
	auto savedBufferStack = mBufferScopeStack;
	auto savedLambdaStack = mLambdaScopeStack;
	auto savedStructStack = mStructScopeStack;
	auto savedEnumStack = mEnumScopeStack;
	auto savedTempStrings = mTempStrings;
	auto savedTempLambdaCtxs = mTempLambdaCtxs;

	// Generate callback body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", cbFn );
	mBuilder->SetInsertPoint( entryBB );

	mVariableMap.clear();
	mMovedVariables.clear();
	mLoopStack.clear();
	mArcScopeStack.clear();
	mStringScopeStack.clear();
	mArrayScopeStack.clear();
	mBufferScopeStack.clear();
	mLambdaScopeStack.clear();
	mStructScopeStack.clear();
	mEnumScopeStack.clear();
	mTempStrings.clear();
	mTempLambdaCtxs.clear();

	// Unpack captures
	llvm::Value *ctxPtr = cbFn->getArg( 0 );
	for ( size_t i = 0; i < captures.size(); i++ )
	{
		llvm::Type *fieldType = captureTypes[i];
		llvm::AllocaInst *localAlloca = mBuilder->CreateAlloca(
			fieldType, nullptr, captures[i].first->getName() );
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
			ctxType, ctxPtr, static_cast<unsigned>( i ), "ctx.field" );
		llvm::Value *fieldVal = mBuilder->CreateLoad( fieldType, fieldPtr, "ctx.val" );
		mBuilder->CreateStore( fieldVal, localAlloca );
		mVariableMap[captures[i].first] = localAlloca;
	}

	genBlock( bodyBlock );

	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateRetVoid();

	// Restore state
	mBuilder->SetInsertPoint( savedBB );
	mVariableMap = savedVarMap;
	mLoopStack = savedLoopStack;
	mArcScopeStack = savedArcStack;
	mStringScopeStack = savedStringStack;
	mArrayScopeStack = savedArrayStack;
	mBufferScopeStack = savedBufferStack;
	mLambdaScopeStack = savedLambdaStack;
	mStructScopeStack = savedStructStack;
	mEnumScopeStack = savedEnumStack;
	mTempStrings = savedTempStrings;
	mTempLambdaCtxs = savedTempLambdaCtxs;

	// In the caller: evaluate the event expression (for side effects)
	if ( handler->mEventExpression != nullptr )
		genExpression( handler->mEventExpression );

	// Allocate context and populate captures
	llvm::DataLayout dl( mModule.get() );
	uint64_t ctxSize = dl.getTypeAllocSize( ctxType );
	llvm::Function *mallocFn = getOrDeclareBlangAlloc();
	llvm::Value *ctxAlloc = mBuilder->CreateCall( mallocFn,
		{ llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), ctxSize ) },
		"event.ctx" );

	for ( size_t i = 0; i < captures.size(); i++ )
	{
		llvm::AllocaInst *alloca = captures[i].second;
		llvm::Type *fieldType = captureTypes[i];
		llvm::Value *val = mBuilder->CreateLoad( fieldType, alloca, "cap.val" );
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP(
			ctxType, ctxAlloc, static_cast<unsigned>( i ), "ctx.store" );
		mBuilder->CreateStore( val, fieldPtr );
	}

	// For now, call the handler inline (synchronous execution)
	// When the event loop runtime is available, this would register
	// the callback instead: __blang_event_register(event, cbFn, ctxAlloc)
	mBuilder->CreateCall( cbFn, { ctxAlloc } );
}

// ---- Phase 2: Await expression codegen ----

llvm::Value *CodeGen::genAwaitExpression( AwaitExpression *awaitExpr )
{
	// Generate the operand — this should produce a task handle from an async call
	llvm::Value *operand = genExpression( awaitExpr->mOperand );
	if ( operand == nullptr )
		return nullptr;

	// If the operand is a pointer (task handle), call __blang_await
	if ( operand->getType()->isPointerTy() )
	{
		// void* result = __blang_await(task)
		llvm::Value *result = mBuilder->CreateCall(
			getOrDeclareAwait(), { operand }, "await.result" );

		// Destroy the task
		mBuilder->CreateCall( getOrDeclareTaskDestroy(), { operand } );

		return result;
	}

	// Non-pointer: return as-is (synchronous evaluation fallback)
	return operand;
}

// ---- Phase 2: ForIn statement codegen ----

void CodeGen::genForInStatement( ForInStatement *forInStmt )
{
	// Check for infinite loop: for { ... }
	if ( forInStmt->mIsInfinite )
	{
		llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
		llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forinf.body", func );
		llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forinf.end", func );

		mBuilder->CreateBr( bodyBB );
		mBuilder->SetInsertPoint( bodyBB );

		// Push loop targets: continue goes back to body, break goes to after
		mLoopStack.push_back( { bodyBB, afterBB } );

		bool savedInsideLoop = mInsideLoop;
		mInsideLoop = true;
		if ( forInStmt->mBody != nullptr )
			genStatement( forInStmt->mBody );
		mInsideLoop = savedInsideLoop;

		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( bodyBB );

		mLoopStack.pop_back();

		mBuilder->SetInsertPoint( afterBB );
		return;
	}

	// Range-based for-in: for x in start..end { ... }
	if ( forInStmt->mIterableExpression != nullptr )
	{
		auto *rangeExpr = dynamic_cast<RangeExpression*>( (Expression*)forInStmt->mIterableExpression );
		if ( rangeExpr != nullptr )
		{
			llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

			// Generate range bounds
			llvm::Value *startVal = genExpression( rangeExpr->mStart );
			llvm::Value *endVal = genExpression( rangeExpr->mEnd );
			if ( startVal == nullptr || endVal == nullptr )
				return;

			// Promote start/end to matching types if widths differ
			if ( startVal->getType() != endVal->getType() &&
				 startVal->getType()->isIntegerTy() && endVal->getType()->isIntegerTy() )
			{
				unsigned startBits = startVal->getType()->getIntegerBitWidth();
				unsigned endBits = endVal->getType()->getIntegerBitWidth();
				if ( startBits < endBits )
					startVal = mBuilder->CreateSExt( startVal, endVal->getType(), "range.promote" );
				else
					endVal = mBuilder->CreateSExt( endVal, startVal->getType(), "range.promote" );
			}

			// Create the loop variable alloca
			llvm::Type *iterType = startVal->getType();
			llvm::AllocaInst *iterAlloca = mBuilder->CreateAlloca(
				iterType, nullptr, forInStmt->mVariableName );
			mBuilder->CreateStore( startVal, iterAlloca );

			// Look up the VariableDefinition from the body's scope to register the alloca
			if ( forInStmt->mBody != nullptr )
			{
				Block *bodyBlock = dynamic_cast<Block*>( (Statement*)forInStmt->mBody );
				if ( bodyBlock != nullptr && bodyBlock->mScope != nullptr )
				{
					Symbol *iterSym = bodyBlock->mScope->findSymbol( forInStmt->mVariableName );
					if ( auto *iterVar = dynamic_cast<VariableDefinition*>( iterSym ) )
						mVariableMap[iterVar] = iterAlloca;
				}
			}

			llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "forin.cond", func );
			llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forin.body", func );
			llvm::BasicBlock *iterBB = llvm::BasicBlock::Create( *mContext, "forin.iter", func );
			llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forin.end", func );

			mBuilder->CreateBr( condBB );

			// Condition: i < end
			mBuilder->SetInsertPoint( condBB );
			llvm::Value *curVal = mBuilder->CreateLoad( iterType, iterAlloca, "cur" );
			llvm::Value *cond = mBuilder->CreateICmpSLT( curVal, endVal, "forin.cmp" );
			mBuilder->CreateCondBr( cond, bodyBB, afterBB );

			// Push loop targets: continue goes to iter, break goes to after
			mLoopStack.push_back( { iterBB, afterBB } );

			// Body
			mBuilder->SetInsertPoint( bodyBB );
			bool savedInsideLoop = mInsideLoop;
			mInsideLoop = true;
			if ( forInStmt->mBody != nullptr )
				genStatement( forInStmt->mBody );
			mInsideLoop = savedInsideLoop;
			if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
				mBuilder->CreateBr( iterBB );

			mLoopStack.pop_back();

			// Iteration: i = i + 1
			mBuilder->SetInsertPoint( iterBB );
			llvm::Value *nextVal = mBuilder->CreateAdd(
				mBuilder->CreateLoad( iterType, iterAlloca, "i" ),
				llvm::ConstantInt::get( iterType, 1 ), "inc" );
			mBuilder->CreateStore( nextVal, iterAlloca );
			mBuilder->CreateBr( condBB );

			// After
			mBuilder->SetInsertPoint( afterBB );
			return;
		}
	}

	// Array-based for-in: for x in arrayExpr { ... }
	if ( forInStmt->mIterableExpression != nullptr &&
		 isArrayType( forInStmt->mIterableExpression ) )
	{
		llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

		// Generate the array expression
		llvm::Value *arrVal = genExpression( forInStmt->mIterableExpression );
		if ( arrVal == nullptr )
			return;

		// Get the array length
		llvm::Value *lenVal = mBuilder->CreateCall(
			getOrDeclareArrayLength(), { arrVal }, "arr.len" );

		// Determine element type from the array's type annotation
		llvm::Type *elemType = llvm::Type::getInt32Ty( *mContext ); // default
		if ( auto *ve = dynamic_cast<VariableExpression*>(
				 (Expression*)forInStmt->mIterableExpression ) )
		{
			Type *varType = ve->mVariable->getVariableType();
			if ( varType != nullptr && varType->getNumTypeParams() > 0 )
				elemType = getLLVMType( varType->getTypeParam( 0 ) );
		}

		// Create the loop counter and element variable
		llvm::Type *i64Type = llvm::Type::getInt64Ty( *mContext );
		llvm::AllocaInst *counterAlloca = mBuilder->CreateAlloca(
			i64Type, nullptr, "forin.counter" );
		mBuilder->CreateStore( llvm::ConstantInt::get( i64Type, 0 ), counterAlloca );

		llvm::AllocaInst *elemAlloca = mBuilder->CreateAlloca(
			elemType, nullptr, forInStmt->mVariableName );

		// Register the loop variable in the variable map
		if ( forInStmt->mBody != nullptr )
		{
			Block *bodyBlock = dynamic_cast<Block*>( (Statement*)forInStmt->mBody );
			if ( bodyBlock != nullptr && bodyBlock->mScope != nullptr )
			{
				Symbol *iterSym = bodyBlock->mScope->findSymbol( forInStmt->mVariableName );
				if ( auto *iterVar = dynamic_cast<VariableDefinition*>( iterSym ) )
					mVariableMap[iterVar] = elemAlloca;
			}
		}

		llvm::BasicBlock *condBB = llvm::BasicBlock::Create( *mContext, "forin.cond", func );
		llvm::BasicBlock *bodyBB = llvm::BasicBlock::Create( *mContext, "forin.body", func );
		llvm::BasicBlock *iterBB = llvm::BasicBlock::Create( *mContext, "forin.iter", func );
		llvm::BasicBlock *afterBB = llvm::BasicBlock::Create( *mContext, "forin.end", func );

		mBuilder->CreateBr( condBB );

		// Condition: counter < length
		mBuilder->SetInsertPoint( condBB );
		llvm::Value *curCounter = mBuilder->CreateLoad( i64Type, counterAlloca, "cur" );
		llvm::Value *cond = mBuilder->CreateICmpSLT( curCounter, lenVal, "forin.cmp" );
		mBuilder->CreateCondBr( cond, bodyBB, afterBB );

		// Body: get element from array
		mBuilder->SetInsertPoint( bodyBB );

		// Push loop targets
		mLoopStack.push_back( { iterBB, afterBB } );

		// Load current element: __blang_array_get(arr, counter, &elem)
		llvm::Value *curIdx = mBuilder->CreateLoad( i64Type, counterAlloca, "idx" );
		llvm::Function *getFn = getOrDeclareArrayGet();
		mBuilder->CreateCall( getFn, { arrVal, curIdx, elemAlloca } );

		bool savedInsideLoop = mInsideLoop;
		mInsideLoop = true;
		if ( forInStmt->mBody != nullptr )
			genStatement( forInStmt->mBody );
		mInsideLoop = savedInsideLoop;

		if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
			mBuilder->CreateBr( iterBB );

		mLoopStack.pop_back();

		// Iteration: counter = counter + 1
		mBuilder->SetInsertPoint( iterBB );
		llvm::Value *nextVal = mBuilder->CreateAdd(
			mBuilder->CreateLoad( i64Type, counterAlloca, "i" ),
			llvm::ConstantInt::get( i64Type, 1 ), "inc" );
		mBuilder->CreateStore( nextVal, counterAlloca );
		mBuilder->CreateBr( condBB );

		// After
		mBuilder->SetInsertPoint( afterBB );
		return;
	}

	// Fallback: generate collection expression and execute body once
	if ( forInStmt->mIterableExpression != nullptr )
		genExpression( forInStmt->mIterableExpression );
	if ( forInStmt->mBody != nullptr )
		genStatement( forInStmt->mBody );
}

// ---- Memory allocation helpers ----

llvm::Function *CodeGen::getOrDeclareMalloc()
{
	llvm::Function *f = mModule->getFunction( "malloc" );
	if ( f != nullptr )
		return f;

	// void *malloc(size_t size)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "malloc", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBlangAlloc()
{
	llvm::Function *f = mModule->getFunction( "__blang_alloc" );
	if ( f != nullptr )
		return f;

	// void *__blang_alloc(size_t size) — checked malloc, aborts on OOM
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_alloc", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareFree()
{
	llvm::Function *f = mModule->getFunction( "free" );
	if ( f != nullptr )
		return f;

	// void free(void *ptr)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "free", mModule.get() );
}

// ---- Lambda context lifetime runtime declarations ----

llvm::Function *CodeGen::getOrDeclareLambdaCtxRetain()
{
	llvm::Function *f = mModule->getFunction( "__blang_lambda_ctx_retain" );
	if ( f != nullptr )
		return f;

	// void __blang_lambda_ctx_retain(void* ctx)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_lambda_ctx_retain", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareLambdaCtxRelease()
{
	llvm::Function *f = mModule->getFunction( "__blang_lambda_ctx_release" );
	if ( f != nullptr )
		return f;

	// void __blang_lambda_ctx_release(void* ctx)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_lambda_ctx_release", mModule.get() );
}

// ---- String interpolation codegen ----

llvm::Value *CodeGen::genStringInterpolation( StringInterpolation *interp )
{
	if ( interp->mParts.empty() )
	{
		llvm::Constant *emptyData = mBuilder->CreateGlobalStringPtr( "", "empty.data" );
		llvm::Value *zeroLen = llvm::ConstantInt::get(
			llvm::Type::getInt64Ty( *mContext ), 0 );
		return mBuilder->CreateCall(
			getOrDeclareStringCreateStatic(), { emptyData, zeroLen }, "str" );
	}

	// Convert each part to a BlangString and collect them
	std::vector<llvm::Value*> parts;

	for ( auto &part : interp->mParts )
	{
		if ( auto *cs = dynamic_cast<ConstString*>( (Expression*)part ) )
		{
			// Literal string segment → create static BlangString
			llvm::Constant *strData = mBuilder->CreateGlobalStringPtr(
				cs->mValue, "interp.data" );
			llvm::Value *lenVal = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), cs->mValue.size() );
			llvm::Value *strVal = mBuilder->CreateCall(
				getOrDeclareStringCreateStatic(), { strData, lenVal }, "interp.str" );
			trackTempString( strVal );
			parts.push_back( strVal );
		}
		else
		{
			// Expression segment — generate value and convert to BlangString
			llvm::Value *val = genExpression( part );
			if ( val == nullptr )
				continue;

			if ( val->getType()->isIntegerTy() )
			{
				llvm::Value *strPart;
				if ( val->getType()->isIntegerTy( 1 ) )
				{
					strPart = mBuilder->CreateCall(
						getOrDeclareBoolToString(), { val }, "boolstr" );
				}
				else
				{
					llvm::Value *ext = mBuilder->CreateSExt( val,
						llvm::Type::getInt64Ty( *mContext ), "ext64" );
					strPart = mBuilder->CreateCall(
						getOrDeclareIntToString(), { ext }, "intstr" );
				}
				trackTempString( strPart );
				parts.push_back( strPart );
			}
			else if ( val->getType()->isFloatTy() || val->getType()->isDoubleTy() )
			{
				if ( val->getType()->isFloatTy() )
					val = mBuilder->CreateFPExt( val,
						llvm::Type::getDoubleTy( *mContext ), "fpext" );
				llvm::Value *strPart = mBuilder->CreateCall(
					getOrDeclareFloatToString(), { val }, "fltstr" );
				trackTempString( strPart );
				parts.push_back( strPart );
			}
			else if ( val->getType()->isPointerTy() )
			{
				// Already a BlangString pointer — use directly (borrowed)
				parts.push_back( val );
			}
		}
	}

	if ( parts.empty() )
	{
		llvm::Constant *emptyData = mBuilder->CreateGlobalStringPtr( "", "empty.data" );
		llvm::Value *zeroLen = llvm::ConstantInt::get(
			llvm::Type::getInt64Ty( *mContext ), 0 );
		llvm::Value *r = mBuilder->CreateCall(
			getOrDeclareStringCreateStatic(), { emptyData, zeroLen }, "str" );
		trackTempString( r );
		return r;
	}

	if ( parts.size() == 1 )
		return parts[0];

	// Build an array of BlangString pointers and call __blang_string_concat_many
	llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i64Ty = llvm::Type::getInt64Ty( *mContext );
	llvm::ArrayType *arrType = llvm::ArrayType::get( ptrType, parts.size() );
	llvm::AllocaInst *arr = mBuilder->CreateAlloca( arrType, nullptr, "interp.arr" );

	llvm::Value *idx0 = llvm::ConstantInt::get(
		llvm::Type::getInt32Ty( *mContext ), 0 );
	for ( size_t i = 0; i < parts.size(); i++ )
	{
		llvm::Value *idx = llvm::ConstantInt::get(
			llvm::Type::getInt32Ty( *mContext ), i );
		llvm::Value *elemPtr = mBuilder->CreateGEP(
			arrType, arr, { idx0, idx }, "interp.elem" );
		mBuilder->CreateStore( parts[i], elemPtr );
	}

	llvm::Value *arrPtr = mBuilder->CreateGEP(
		arrType, arr, { idx0, idx0 }, "interp.ptr" );
	llvm::Value *countVal = llvm::ConstantInt::get( i64Ty, parts.size() );
	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareStringConcatMany(), { arrPtr, countVal }, "interp.result" );
	trackTempString( result );

	return result;
}

// ---- Builtin print/println codegen ----

void CodeGen::genPrintCall( CallExpression *call, bool appendNewline )
{
	// println() with no args → just emit newline
	if ( call->mParams.empty() )
	{
		if ( appendNewline )
			mBuilder->CreateCall( getOrDeclarePrintNewline(), {} );
		return;
	}

	// First arg must be a string literal (format string).
	// String literals containing '{' get parsed as StringInterpolation by the
	// parser. When all parts are ConstString (no variable references resolved),
	// reconstruct the original format string text.
	std::string fmtStr;
	auto *fmtConst = dynamic_cast<ConstString*>( (Expression*)call->mParams[0] );
	if ( fmtConst != nullptr )
	{
		fmtStr = fmtConst->mValue;
	}
	else if ( auto *interp = dynamic_cast<StringInterpolation*>( (Expression*)call->mParams[0] ) )
	{
		// Reconstruct the format string from StringInterpolation parts.
		// The parser preserves unresolved {}, {:spec} as literal text.
		bool allLiteral = true;
		for ( auto &part : interp->mParts )
		{
			if ( dynamic_cast<ConstString*>( (Expression*)part ) == nullptr )
			{
				allLiteral = false;
				break;
			}
		}
		if ( allLiteral )
		{
			for ( auto &part : interp->mParts )
			{
				auto *cs = dynamic_cast<ConstString*>( (Expression*)part );
				fmtStr += cs->mValue;
			}
		}
		else
		{
			cerr << "CodeGen error: print/println format string must be a string literal, not a string interpolation with variables" << endl;
			mHasError = true;
			return;
		}
	}
	else
	{
		cerr << "CodeGen error: print/println format string must be a string literal" << endl;
		mHasError = true;
		return;
	}

	// Fake lexer for error reporting — we use a reference to the module scope's parent
	// Since we're in codegen, errors are reported via cerr + mHasError

	// Parse format string
	// We need a Lexer for error reporting in ParsedFormatString::parse.
	// Instead, we'll do inline parsing since we can't easily get a Lexer here.
	// Re-implement the parse inline with cerr error reporting.

	ParsedFormatString parsed;
	{
		std::string current;
		int argIndex = 0;
		size_t i = 0;
		while ( i < fmtStr.size() )
		{
			char c = fmtStr[i];

			if ( c == '{' && i + 1 < fmtStr.size() && fmtStr[i + 1] == '{' )
			{
				current += '{';
				i += 2;
				continue;
			}
			if ( c == '}' && i + 1 < fmtStr.size() && fmtStr[i + 1] == '}' )
			{
				current += '}';
				i += 2;
				continue;
			}

			if ( c == '{' )
			{
				parsed.literals.push_back( current );
				current.clear();

				FormatPlaceholder ph;
				ph.argIndex = argIndex++;
				i++;

				if ( i < fmtStr.size() && fmtStr[i] == ':' )
				{
					i++;
					std::string spec;
					while ( i < fmtStr.size() && fmtStr[i] != '}' )
					{
						spec += fmtStr[i];
						i++;
					}
					if ( spec.empty() )
					{
						cerr << "CodeGen error: empty format specifier after ':'" << endl;
						mHasError = true;
						return;
					}
					ph.specifier = spec;
					char last = spec.back();
					if ( last == 'x' || last == 'X' || last == 'o' || last == 'b' )
						ph.type = last;
					else if ( last == 'f' )
					{
						ph.type = 'f';
						if ( spec.size() >= 2 && spec[0] == '.' )
						{
							std::string digits = spec.substr( 1, spec.size() - 2 );
							ph.precision = 0;
							for ( char d : digits )
							{
								if ( d < '0' || d > '9' )
								{
									cerr << "CodeGen error: invalid precision in format specifier: '" << spec << "'" << endl;
									mHasError = true;
									return;
								}
								ph.precision = ph.precision * 10 + ( d - '0' );
							}
						}
					}
					else if ( last == 'e' )
						ph.type = 'e';
					else
					{
						cerr << "CodeGen error: unknown format specifier: '" << spec << "'" << endl;
						mHasError = true;
						return;
					}
				}

				if ( i >= fmtStr.size() || fmtStr[i] != '}' )
				{
					cerr << "CodeGen error: unterminated format placeholder" << endl;
					mHasError = true;
					return;
				}
				i++;
				parsed.placeholders.push_back( ph );
			}
			else if ( c == '}' )
			{
				cerr << "CodeGen error: unexpected '}' in format string (use '}}' for literal '}')" << endl;
				mHasError = true;
				return;
			}
			else
			{
				current += c;
				i++;
			}
		}
		parsed.literals.push_back( current );
	}

	int numPlaceholders = (int)parsed.placeholders.size();
	int numArgs = (int)call->mParams.size() - 1; // subtract format string

	if ( numPlaceholders != numArgs )
	{
		cerr << "CodeGen error: format string has " << numPlaceholders
			 << " placeholder(s) but " << numArgs << " argument(s) provided" << endl;
		mHasError = true;
		return;
	}

	// Validate type compatibility for specifiers
	for ( int pi = 0; pi < numPlaceholders; pi++ )
	{
		const FormatPlaceholder &ph = parsed.placeholders[pi];
		if ( ph.type == '\0' )
			continue; // default {}, accept anything

		Expression *argExpr = call->mParams[pi + 1];

		// For specifier validation, we need the expression's type
		// We'll do best-effort type checking via AST inspection
		bool isIntArg = false, isFloatArg = false;
		if ( dynamic_cast<ConstInteger*>( argExpr ) )
			isIntArg = true;
		else if ( dynamic_cast<ConstFloat*>( argExpr ) )
			isFloatArg = true;
		else if ( auto *ve = dynamic_cast<VariableExpression*>( argExpr ) )
		{
			if ( ve->mVariable && ve->mVariable->getVariableType() )
			{
				const std::string &tn = ve->mVariable->getVariableType()->getName();
				if ( tn == "int" || tn == "long" || tn == "short" || tn == "char" )
					isIntArg = true;
				else if ( tn == "float" || tn == "double" )
					isFloatArg = true;
			}
		}

		if ( ph.type == 'x' || ph.type == 'X' || ph.type == 'o' || ph.type == 'b' )
		{
			if ( isFloatArg )
			{
				cerr << "CodeGen error: format specifier ':" << ph.type
					 << "' requires integer type, got float/double" << endl;
				mHasError = true;
				return;
			}
		}
		else if ( ph.type == 'f' || ph.type == 'e' )
		{
			if ( isIntArg )
			{
				cerr << "CodeGen error: format specifier ':" << ph.type
					 << "' requires float/double type, got integer" << endl;
				mHasError = true;
				return;
			}
		}
	}

	// No placeholders: just print the literal string
	if ( numPlaceholders == 0 )
	{
		llvm::Constant *strData = mBuilder->CreateGlobalStringPtr( fmtStr, "print.data" );
		llvm::Value *lenVal = llvm::ConstantInt::get(
			llvm::Type::getInt64Ty( *mContext ), fmtStr.size() );
		llvm::Value *strVal = mBuilder->CreateCall(
			getOrDeclareStringCreateStatic(), { strData, lenVal }, "print.str" );
		trackTempString( strVal );
		mBuilder->CreateCall( getOrDeclarePrintBlang(), { strVal } );
		if ( appendNewline )
			mBuilder->CreateCall( getOrDeclarePrintNewline(), {} );
		return;
	}

	// Build parts by interleaving literals and formatted args
	std::vector<llvm::Value*> parts;

	for ( int i = 0; i <= numPlaceholders; i++ )
	{
		// Literal segment
		if ( !parsed.literals[i].empty() )
		{
			llvm::Constant *litData = mBuilder->CreateGlobalStringPtr(
				parsed.literals[i], "print.lit" );
			llvm::Value *litLen = llvm::ConstantInt::get(
				llvm::Type::getInt64Ty( *mContext ), parsed.literals[i].size() );
			llvm::Value *litStr = mBuilder->CreateCall(
				getOrDeclareStringCreateStatic(), { litData, litLen }, "print.litstr" );
			trackTempString( litStr );
			parts.push_back( litStr );
		}

		// Placeholder arg
		if ( i < numPlaceholders )
		{
			const FormatPlaceholder &ph = parsed.placeholders[i];
			Expression *argExpr = call->mParams[i + 1];
			llvm::Value *val = genExpression( argExpr );
			if ( val == nullptr )
				continue;

			// Check if this is a struct type variable first (before genExpression)
			bool isStructArg = false;
			std::string structTypeName;
			if ( auto *ve = dynamic_cast<VariableExpression*>( argExpr ) )
			{
				if ( ve->mVariable && ve->mVariable->getVariableType() )
				{
					structTypeName = ve->mVariable->getVariableType()->getName();
					if ( structTypeName != "string" && structTypeName != "cstring" &&
						 structTypeName != "int" && structTypeName != "long" &&
						 structTypeName != "short" && structTypeName != "char" &&
						 structTypeName != "float" && structTypeName != "double" &&
						 structTypeName != "bool" )
					{
						auto sIt = mStructDefMap.find( structTypeName );
						if ( sIt != mStructDefMap.end() )
							isStructArg = true;
					}
				}
			}

			if ( isStructArg )
			{
				// Struct type → call StructName_to_string
				StructDefinition *sd = mStructDefMap[structTypeName];
				bool hasPrintable = false;
				for ( auto &m : sd->getMethods() )
				{
					if ( m->getName() == "to_string" )
					{
						hasPrintable = true;
						break;
					}
				}
				if ( !hasPrintable )
				{
					cerr << "CodeGen error: type '" << structTypeName
						 << "' is not printable — implement the Printable protocol" << endl;
					mHasError = true;
					return;
				}
				// val already holds the loaded struct value from genExpression above
				// to_string expects a pointer to the struct (self by pointer)
				std::string fnName = structTypeName + "_to_string";
				llvm::Function *toStrFn = mModule->getFunction( fnName );
				if ( toStrFn )
				{
					// Store the struct value in a temporary alloca and pass its address
					llvm::AllocaInst *tmpAlloca = mBuilder->CreateAlloca(
						val->getType(), nullptr, "print.tmp" );
					mBuilder->CreateStore( val, tmpAlloca );
					llvm::Value *strPart = mBuilder->CreateCall(
						toStrFn, { tmpAlloca }, "print.structstr" );
					trackTempString( strPart );
					parts.push_back( strPart );
				}
				else
				{
					cerr << "CodeGen error: '" << fnName << "' function not found" << endl;
					mHasError = true;
					return;
				}
			}
			else if ( val->getType()->isIntegerTy() )
			{
				llvm::Value *strPart;
				if ( val->getType()->isIntegerTy( 1 ) )
				{
					strPart = mBuilder->CreateCall(
						getOrDeclareBoolToString(), { val }, "print.boolstr" );
				}
				else if ( ph.type != '\0' )
				{
					llvm::Value *ext = mBuilder->CreateSExt( val,
						llvm::Type::getInt64Ty( *mContext ), "ext64" );
					llvm::Constant *specData = mBuilder->CreateGlobalStringPtr(
						ph.specifier, "print.spec" );
					llvm::Value *specLen = llvm::ConstantInt::get(
						llvm::Type::getInt32Ty( *mContext ), ph.specifier.size() );
					strPart = mBuilder->CreateCall(
						getOrDeclareIntToStringFmt(), { ext, specData, specLen }, "print.intfmt" );
				}
				else
				{
					llvm::Value *ext = mBuilder->CreateSExt( val,
						llvm::Type::getInt64Ty( *mContext ), "ext64" );
					strPart = mBuilder->CreateCall(
						getOrDeclareIntToString(), { ext }, "print.intstr" );
				}
				trackTempString( strPart );
				parts.push_back( strPart );
			}
			else if ( val->getType()->isFloatTy() || val->getType()->isDoubleTy() )
			{
				if ( val->getType()->isFloatTy() )
					val = mBuilder->CreateFPExt( val,
						llvm::Type::getDoubleTy( *mContext ), "fpext" );

				llvm::Value *strPart;
				if ( ph.type != '\0' )
				{
					llvm::Constant *specData = mBuilder->CreateGlobalStringPtr(
						ph.specifier, "print.spec" );
					llvm::Value *specLen = llvm::ConstantInt::get(
						llvm::Type::getInt32Ty( *mContext ), ph.specifier.size() );
					strPart = mBuilder->CreateCall(
						getOrDeclareFloatToStringFmt(), { val, specData, specLen }, "print.fltfmt" );
				}
				else
				{
					strPart = mBuilder->CreateCall(
						getOrDeclareFloatToString(), { val }, "print.fltstr" );
				}
				trackTempString( strPart );
				parts.push_back( strPart );
			}
			else if ( val->getType()->isPointerTy() )
			{
				// String pointer — use directly (borrowed, not owned)
				parts.push_back( val );
			}
		}
	}

	if ( parts.empty() )
	{
		if ( appendNewline )
			mBuilder->CreateCall( getOrDeclarePrintNewline(), {} );
		return;
	}

	// If single part, print directly
	if ( parts.size() == 1 )
	{
		mBuilder->CreateCall( getOrDeclarePrintBlang(), { parts[0] } );
	}
	else
	{
		// Concat all parts then print
		llvm::Type *ptrType = llvm::PointerType::get( *mContext, 0 );
		llvm::Type *i64Ty = llvm::Type::getInt64Ty( *mContext );
		llvm::ArrayType *arrType = llvm::ArrayType::get( ptrType, parts.size() );
		llvm::AllocaInst *arr = mBuilder->CreateAlloca( arrType, nullptr, "print.arr" );

		llvm::Value *idx0 = llvm::ConstantInt::get(
			llvm::Type::getInt32Ty( *mContext ), 0 );
		for ( size_t i = 0; i < parts.size(); i++ )
		{
			llvm::Value *idx = llvm::ConstantInt::get(
				llvm::Type::getInt32Ty( *mContext ), i );
			llvm::Value *elemPtr = mBuilder->CreateGEP(
				arrType, arr, { idx0, idx }, "print.elem" );
			mBuilder->CreateStore( parts[i], elemPtr );
		}

		llvm::Value *arrPtr = mBuilder->CreateGEP(
			arrType, arr, { idx0, idx0 }, "print.ptr" );
		llvm::Value *countVal = llvm::ConstantInt::get( i64Ty, parts.size() );
		llvm::Value *result = mBuilder->CreateCall(
			getOrDeclareStringConcatMany(), { arrPtr, countVal }, "print.result" );
		trackTempString( result );
		mBuilder->CreateCall( getOrDeclarePrintBlang(), { result } );
	}

	if ( appendNewline )
		mBuilder->CreateCall( getOrDeclarePrintNewline(), {} );
}

// ---- Print runtime declarations ----

llvm::Function *CodeGen::getOrDeclarePrintBlang()
{
	llvm::Function *f = mModule->getFunction( "__blang_print" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_print", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclarePrintNewline()
{
	llvm::Function *f = mModule->getFunction( "__blang_print_newline" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{},
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_print_newline", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclarePrintFlush()
{
	llvm::Function *f = mModule->getFunction( "__blang_print_flush" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{},
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_print_flush", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareIntToStringFmt()
{
	llvm::Function *f = mModule->getFunction( "__blang_int_to_string_fmt" );
	if ( f != nullptr )
		return f;

	// BlangString *__blang_int_to_string_fmt(int64_t value, const char *spec, int spec_len)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_int_to_string_fmt", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareFloatToStringFmt()
{
	llvm::Function *f = mModule->getFunction( "__blang_float_to_string_fmt" );
	if ( f != nullptr )
		return f;

	// BlangString *__blang_float_to_string_fmt(double value, const char *spec, int spec_len)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getDoubleTy( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_float_to_string_fmt", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareCharToString()
{
	llvm::Function *f = mModule->getFunction( "__blang_char_to_string" );
	if ( f != nullptr )
		return f;

	// BlangString *__blang_char_to_string(int32_t c)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_char_to_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareSnprintf()
{
	llvm::Function *f = mModule->getFunction( "snprintf" );
	if ( f != nullptr )
		return f;

	// int snprintf(char *buf, size_t size, const char *fmt, ...)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) },
		true /* variadic */ );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "snprintf", mModule.get() );
}

// ---- String runtime declarations ----

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

// ---- Array runtime declarations ----

llvm::Function *CodeGen::getOrDeclareArrayCreate()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_create" );
	if ( f != nullptr )
		return f;

	// BlangArray* __blang_array_create(int32_t elem_size, int64_t initial_capacity)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_create", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayCreateFromData()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_create_from_data" );
	if ( f != nullptr )
		return f;

	// BlangArray* __blang_array_create_from_data(int32_t elem_size, const void *data, int64_t count)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_create_from_data", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayRetain()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_retain" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_retain", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayRelease()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_release" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_release", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayGet()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_get" );
	if ( f != nullptr )
		return f;

	// void __blang_array_get(BlangArray *a, int64_t index, void *out)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_get", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArraySet()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_set" );
	if ( f != nullptr )
		return f;

	// void __blang_array_set(BlangArray *a, int64_t index, const void *value)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_set", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayPush()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_push" );
	if ( f != nullptr )
		return f;

	// void __blang_array_push(BlangArray *a, const void *value)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_push", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayLength()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_length" );
	if ( f != nullptr )
		return f;

	// int64_t __blang_array_length(BlangArray *a)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_length", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayConcat()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_concat" );
	if ( f != nullptr )
		return f;

	// BlangArray* __blang_array_concat(BlangArray *a, BlangArray *b)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_concat", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringCreate()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_create" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_create", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringCreateStatic()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_create_static" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_create_static", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringRetain()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_retain" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_retain", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringRelease()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_release" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_release", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringConcat()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_concat" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_concat", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringConcatMany()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_concat_many" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_concat_many", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringEquals()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_equals" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_equals", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringLength()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_length" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_length", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringCharAt()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_char_at" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt8Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_char_at", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareIntToString()
{
	llvm::Function *f = mModule->getFunction( "__blang_int_to_string" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_int_to_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareFloatToString()
{
	llvm::Function *f = mModule->getFunction( "__blang_float_to_string" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getDoubleTy( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_float_to_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBoolToString()
{
	llvm::Function *f = mModule->getFunction( "__blang_bool_to_string" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt1Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_bool_to_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStrlen()
{
	llvm::Function *f = mModule->getFunction( "strlen" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "strlen", mModule.get() );
}

// ---- Additional string runtime declarations ----

llvm::Function *CodeGen::getOrDeclareStringIsEmpty()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_is_empty" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_is_empty", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringContains()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_contains" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_contains", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringStartsWith()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_starts_with" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_starts_with", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringEndsWith()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_ends_with" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_ends_with", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringIndexOf()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_index_of" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_index_of", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringToUpper()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_to_upper" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_to_upper", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringToLower()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_to_lower" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_to_lower", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringTrim()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_trim" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_trim", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringSubstring()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_substring" );
	if ( f != nullptr )
		return f;

	// BlangString* __blang_string_substring(BlangString *s, int64_t start, int64_t end)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_substring", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringReplace()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_replace" );
	if ( f != nullptr )
		return f;

	// BlangString* __blang_string_replace(BlangString *s, BlangString *old, BlangString *new)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_replace", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringByteAt()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_byte_at" );
	if ( f != nullptr )
		return f;

	// int32_t __blang_string_byte_at(BlangString *s, int64_t index)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_byte_at", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareStringToCstring()
{
	llvm::Function *f = mModule->getFunction( "__blang_string_to_cstring" );
	if ( f != nullptr )
		return f;

	// const char* __blang_string_to_cstring(BlangString *s)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_string_to_cstring", mModule.get() );
}

// ---- Additional array runtime declarations ----

llvm::Function *CodeGen::getOrDeclareArrayIsEmpty()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_is_empty" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_is_empty", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayPop()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_pop" );
	if ( f != nullptr )
		return f;

	// bool __blang_array_pop(BlangArray *a, void *out)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt1Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_pop", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayCapacity()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_capacity" );
	if ( f != nullptr )
		return f;

	// int64_t __blang_array_capacity(BlangArray *a)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_capacity", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArrayClear()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_clear" );
	if ( f != nullptr )
		return f;

	// void __blang_array_clear(BlangArray *a)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_clear", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareArraySetElemDtor()
{
	llvm::Function *f = mModule->getFunction( "__blang_array_set_elem_dtor" );
	if ( f != nullptr )
		return f;

	// void __blang_array_set_elem_dtor(BlangArray *a, void(*dtor)(void*))
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ ptrTy, ptrTy },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_array_set_elem_dtor", mModule.get() );
}

void CodeGen::emitArrayElemDtor( llvm::Value *arrayPtr, const std::string &elemTypeName )
{
	llvm::Function *dtorFn = nullptr;

	if ( elemTypeName == "string" )
		dtorFn = getOrDeclareStringRelease();
	else if ( elemTypeName == "Array" )
		dtorFn = getOrDeclareArrayRelease();
	else if ( elemTypeName == "Buffer" )
		dtorFn = getOrDeclareBufferRelease();
	else if ( isUserStructType( elemTypeName ) )
		dtorFn = getOrDeclareRcRelease();

	if ( dtorFn != nullptr )
	{
		mBuilder->CreateCall( getOrDeclareArraySetElemDtor(), { arrayPtr, dtorFn } );
	}
}

// ---- Buffer type helper ----

bool CodeGen::isBufferType( Expression *expr )
{
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

// ---- Channel (chan<T>) type helpers and method codegen ----

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
//   chan<T> declarations store a BlangChan* (see genVariableDefinition).
//   send/recv marshal the element through a stack slot of T's LLVM type,
//   matching the byte-copy contract of __blang_chan_send/recv.
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
		if ( valVal->getType()->isIntegerTy() && elemType->isIntegerTy() &&
			 valVal->getType() != elemType )
			valVal = mBuilder->CreateIntCast( valVal, elemType, true, "chan.val" );

		llvm::Value *slot = mBuilder->CreateAlloca( elemType, nullptr, "chan.send.slot" );
		mBuilder->CreateStore( valVal, slot );
		mBuilder->CreateCall( getOrDeclareChanSend(), { chanVal, slot } );
		return llvm::Constant::getNullValue( llvm::Type::getInt32Ty( *mContext ) );
	}

	// recv() -> T  (int __blang_chan_recv(BlangChan*, void* out); returns
	// the received value, leaving the success flag to a future Option<T> form)
	if ( method == "recv" && expr->mArgs.empty() )
	{
		llvm::Value *slot = mBuilder->CreateAlloca( elemType, nullptr, "chan.recv.slot" );
		// Zero-initialize so a recv on a closed/empty channel yields a defined value.
		mBuilder->CreateStore( llvm::Constant::getNullValue( elemType ), slot );
		mBuilder->CreateCall( getOrDeclareChanRecv(), { chanVal, slot } );
		return mBuilder->CreateLoad( elemType, slot, "chan.recv" );
	}

	return nullptr;
}

// ---- Buffer field/method codegen ----

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

// ---- Buffer runtime declarations ----

llvm::Function *CodeGen::getOrDeclareBufferCreate()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_create" );
	if ( f != nullptr )
		return f;

	// BlangBuffer* __blang_buffer_create(int64_t capacity)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_create", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferCreateFromString()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_create_from_string" );
	if ( f != nullptr )
		return f;

	// BlangBuffer* __blang_buffer_create_from_string(BlangString *s)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_create_from_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferRetain()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_retain" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_retain", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferRelease()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_release" );
	if ( f != nullptr )
		return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_release", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferLength()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_length" );
	if ( f != nullptr )
		return f;

	// int64_t __blang_buffer_length(BlangBuffer *buf)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_length", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferCapacity()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_capacity" );
	if ( f != nullptr )
		return f;

	// int64_t __blang_buffer_capacity(BlangBuffer *buf)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_capacity", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferIsEmpty()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_is_empty" );
	if ( f != nullptr )
		return f;

	// int32_t __blang_buffer_is_empty(BlangBuffer *buf)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_is_empty", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferGet()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_get" );
	if ( f != nullptr )
		return f;

	// int32_t __blang_buffer_get(BlangBuffer *buf, int64_t index)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_get", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferSet()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_set" );
	if ( f != nullptr )
		return f;

	// void __blang_buffer_set(BlangBuffer *buf, int64_t index, int32_t value)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_set", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferAppendByte()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_append_byte" );
	if ( f != nullptr )
		return f;

	// void __blang_buffer_append_byte(BlangBuffer *buf, int32_t byte)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_append_byte", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferAppendBytes()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_append_bytes" );
	if ( f != nullptr )
		return f;

	// void __blang_buffer_append_bytes(BlangBuffer *buf, BlangBuffer *src, int64_t len)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_append_bytes", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferAppendString()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_append_string" );
	if ( f != nullptr )
		return f;

	// void __blang_buffer_append_string(BlangBuffer *buf, BlangString *s)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_append_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferIndexOf()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_index_of" );
	if ( f != nullptr )
		return f;

	// int64_t __blang_buffer_index_of(BlangBuffer *buf, BlangBuffer *pattern, int64_t offset)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_index_of", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferSlice()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_slice" );
	if ( f != nullptr )
		return f;

	// BlangBuffer* __blang_buffer_slice(BlangBuffer *buf, int64_t start, int64_t end)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_slice", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferToString()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_to_string" );
	if ( f != nullptr )
		return f;

	// BlangString* __blang_buffer_to_string(BlangBuffer *buf)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_to_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferToStringRange()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_to_string_range" );
	if ( f != nullptr )
		return f;

	// BlangString* __blang_buffer_to_string_range(BlangBuffer *buf, int64_t start, int64_t end)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_to_string_range", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferClear()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_clear" );
	if ( f != nullptr )
		return f;

	// void __blang_buffer_clear(BlangBuffer *buf)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_clear", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareBufferCompact()
{
	llvm::Function *f = mModule->getFunction( "__blang_buffer_compact" );
	if ( f != nullptr )
		return f;

	// void __blang_buffer_compact(BlangBuffer *buf, int64_t bytes)
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt64Ty( *mContext ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_buffer_compact", mModule.get() );
}

// ---- Pipeline expression codegen ----

llvm::Value *CodeGen::genPipelineExpression( PipelineExpression *pipeline )
{
	// Desugar: expr |> fn(args) becomes fn(expr, args)
	// The mTransform should be a CallExpression
	auto *call = dynamic_cast<CallExpression*>( (Expression*)pipeline->mTransform );
	if ( call == nullptr )
	{
		cerr << "CodeGen: pipeline RHS is not a function call" << endl;
		return nullptr;
	}

	FunctionDefinition *funcDef = call->mFunction;

	// Look up the LLVM function
	llvm::Function *llvmFunc = nullptr;
	auto it = mFunctionMap.find( funcDef );
	if ( it != mFunctionMap.end() )
		llvmFunc = it->second;
	else
		llvmFunc = mModule->getFunction( funcDef->getName() );

	if ( llvmFunc == nullptr )
	{
		cerr << "CodeGen: undefined function '" << funcDef->getName() << "' in pipeline" << endl;
		return nullptr;
	}

	// Generate the pipeline input as the first argument
	llvm::Value *inputVal = genExpression( pipeline->mInput );
	if ( inputVal == nullptr )
		return nullptr;

	std::vector<llvm::Value*> args;
	args.push_back( inputVal );

	// Generate the remaining explicit arguments from the call
	for ( auto &paramExpr : call->mParams )
	{
		llvm::Value *argVal = genExpression( paramExpr );
		if ( argVal == nullptr )
			return nullptr;
		args.push_back( argVal );
	}

	if ( llvmFunc->getReturnType()->isVoidTy() )
	{
		mBuilder->CreateCall( llvmFunc, args );
		return nullptr;
	}

	return mBuilder->CreateCall( llvmFunc, args, "pipe" );
}

// ---- Database query codegen ----

llvm::Function *CodeGen::getOrDeclareDbQuery()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_query" );
	if ( f != nullptr )
		return f;

	// BlangDBResult* __blang_db_query(BlangDBConn*, const char* sql,
	//     const char** params, int num_params, const char** error_msg)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		ptrTy, { ptrTy, ptrTy, ptrTy, i32Ty, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_query", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbExec()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_exec" );
	if ( f != nullptr )
		return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		i32Ty, { ptrTy, ptrTy, ptrTy, i32Ty, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_exec", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbResultCount()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_result_count" );
	if ( f != nullptr )
		return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get( i32Ty, { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_result_count", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbResultGet()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_result_get" );
	if ( f != nullptr )
		return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy, i32Ty, i32Ty }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_result_get", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbResultGetInt()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_result_get_int" );
	if ( f != nullptr )
		return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::Type *i64Ty = llvm::Type::getInt64Ty( *mContext );
	llvm::FunctionType *ft = llvm::FunctionType::get( i64Ty, { ptrTy, i32Ty, i32Ty }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_result_get_int", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareDbResultFree()
{
	llvm::Function *f = mModule->getFunction( "__blang_db_result_free" );
	if ( f != nullptr )
		return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_db_result_free", mModule.get() );
}

llvm::Value *CodeGen::genQueryExpression( QueryExpression *query )
{
	// Generate SQL at compile time
	SQLStatement sqlStmt = SQLGen::generateSelect( query, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	// Create the SQL string constant
	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "query.sql" );

	// Create params array (NULL for now — parameter binding requires runtime values)
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );
	llvm::Value *numParams = llvm::ConstantInt::get( i32Ty, 0 );

	// Error message pointer
	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "query.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	// The first argument is the database connection — for now use NULL
	// (real code would load from a global or parameter)
	// Call __blang_db_query(conn, sql, params, num_params, &error_msg)
	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareDbQuery(),
		{ nullPtr, sqlStr, nullPtr, numParams, errMsgAlloca },
		"query.result" );

	return result;
}

llvm::Value *CodeGen::genInsertExpression( InsertExpression *insert )
{
	SQLStatement sqlStmt = SQLGen::generateInsert( insert, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "insert.sql" );
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );

	// Build parameter array from field values
	int numParams = static_cast<int>( insert->mFieldValues.size() );
	llvm::ArrayType *paramArrType = llvm::ArrayType::get( ptrTy, numParams );
	llvm::AllocaInst *paramArr = mBuilder->CreateAlloca( paramArrType, nullptr, "insert.params" );

	llvm::Value *idx0 = llvm::ConstantInt::get( i32Ty, 0 );
	for ( int i = 0; i < numParams; i++ )
	{
		llvm::Value *val = genExpression( insert->mFieldValues[i] );
		if ( val == nullptr )
			continue;

		// Convert to string representation using snprintf
		llvm::Value *strVal = val;
		if ( val->getType()->isIntegerTy() )
		{
			// Convert int to string via snprintf
			llvm::Type *i8Ty = llvm::Type::getInt8Ty( *mContext );
			llvm::AllocaInst *buf = mBuilder->CreateAlloca(
				i8Ty, llvm::ConstantInt::get( i32Ty, 32 ), "param.buf" );
			llvm::Value *fmt = mBuilder->CreateGlobalStringPtr( "%d", "int.fmt" );
			if ( !val->getType()->isIntegerTy( 32 ) )
				val = mBuilder->CreateSExt( val, i32Ty, "ext" );
			mBuilder->CreateCall( getOrDeclareSnprintf(),
				{ buf, llvm::ConstantInt::get( llvm::Type::getInt64Ty( *mContext ), 32 ),
				  fmt, val } );
			strVal = buf;
		}

		llvm::Value *idxVal = llvm::ConstantInt::get( i32Ty, i );
		llvm::Value *elemPtr = mBuilder->CreateGEP(
			paramArrType, paramArr, { idx0, idxVal }, "param.ptr" );
		mBuilder->CreateStore( strVal, elemPtr );
	}

	// Get pointer to first element
	llvm::Value *paramsPtr = mBuilder->CreateGEP(
		paramArrType, paramArr, { idx0, idx0 }, "params.ptr" );

	// Error message pointer
	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "insert.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	// Call __blang_db_exec(conn, sql, params, num_params, &error_msg)
	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareDbExec(),
		{ nullPtr, sqlStr, paramsPtr,
		  llvm::ConstantInt::get( i32Ty, numParams ), errMsgAlloca },
		"insert.result" );

	return result;
}

llvm::Value *CodeGen::genUpdateExpression( UpdateExpression *update )
{
	SQLStatement sqlStmt = SQLGen::generateUpdate( update, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "update.sql" );
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );

	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "update.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareDbExec(),
		{ nullPtr, sqlStr, nullPtr,
		  llvm::ConstantInt::get( i32Ty, 0 ), errMsgAlloca },
		"update.result" );

	return result;
}

llvm::Value *CodeGen::genDeleteExpression( DeleteExpression *del )
{
	SQLStatement sqlStmt = SQLGen::generateDelete( del, mQLangModule );

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );

	llvm::Value *sqlStr = mBuilder->CreateGlobalStringPtr( sqlStmt.sql, "delete.sql" );
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );

	llvm::AllocaInst *errMsgAlloca = mBuilder->CreateAlloca( ptrTy, nullptr, "delete.err" );
	mBuilder->CreateStore( nullPtr, errMsgAlloca );

	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareDbExec(),
		{ nullPtr, sqlStr, nullPtr,
		  llvm::ConstantInt::get( i32Ty, 0 ), errMsgAlloca },
		"delete.result" );

	return result;
}

// ---- Break/Continue codegen ----

void CodeGen::genBreakStatement()
{
	if ( mLoopStack.empty() )
	{
		cerr << "CodeGen: break statement outside of loop" << endl;
		return;
	}

	llvm::BasicBlock *exitBB = mLoopStack.back().second;
	mBuilder->CreateBr( exitBB );
}

void CodeGen::genContinueStatement()
{
	if ( mLoopStack.empty() )
	{
		cerr << "CodeGen: continue statement outside of loop" << endl;
		return;
	}

	llvm::BasicBlock *continueBB = mLoopStack.back().first;
	mBuilder->CreateBr( continueBB );
}

// ---- Phase 2: Test block codegen ----

llvm::Function *CodeGen::genTestBlock( TestBlock *testBlock )
{
	// Sanitize the test name for use as a function name
	string testName = "__blang_test_";
	for ( char c : testBlock->getName() )
	{
		if ( isalnum( c ) )
			testName += c;
		else
			testName += '_';
	}

	// Create the test function: void __blang_test_xxx()
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), {}, false );
	llvm::Function *testFunc = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, testName, mModule.get() );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", testFunc );
	mBuilder->SetInsertPoint( entryBB );

	// Generate the test body
	if ( testBlock->mBody != nullptr )
		genBlock( testBlock->mBody );

	// Add implicit return
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateRetVoid();

	mVariableMap.clear();
	mMovedVariables.clear();
	return testFunc;
}

void CodeGen::genTestRunner( const std::vector<llvm::Function*> &testFunctions,
	const std::vector<SmartPtr<TestBlock>> &testBlocks )
{
	// Create void __blang_run_tests() that calls each test function
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), {}, false );
	llvm::Function *runTests = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_run_tests", mModule.get() );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", runTests );
	mBuilder->SetInsertPoint( entryBB );

	llvm::Function *putsFunc = getOrDeclarePuts();

	for ( size_t i = 0; i < testFunctions.size(); i++ )
	{
		// Print test name
		std::string msg = "Running test: " + testBlocks[i]->getName();
		llvm::Value *msgVal = mBuilder->CreateGlobalStringPtr( msg, "testmsg" );
		mBuilder->CreateCall( putsFunc, { msgVal } );

		// Call the test function
		mBuilder->CreateCall( testFunctions[i], {} );

		// Print pass (if assert failed inside, exit() already terminated)
		std::string passMsg = "  PASSED: " + testBlocks[i]->getName();
		llvm::Value *passMsgVal = mBuilder->CreateGlobalStringPtr( passMsg, "passmsg" );
		mBuilder->CreateCall( putsFunc, { passMsgVal } );
	}

	mBuilder->CreateRetVoid();
}

// ---- JSON codegen (@json annotation) ----

bool CodeGen::genJsonToJson( StructDefinition *structDef )
{
	llvm::StructType *structType = getOrCreateStructType( structDef );
	std::string funcName = structDef->getName() + "_to_json";

	// StructName_to_json(ptr self) -> char*
	// self is a heap pointer to struct data (structs are by-reference)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	llvm::Function *func = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, funcName, mModule.get() );
	func->getArg( 0 )->setName( "self" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", func );
	mBuilder->SetInsertPoint( entryBB );

	// self is already a pointer to struct data — use it directly for GEP
	llvm::Value *selfAlloca = func->getArg( 0 );

	// Create JSON object
	llvm::Value *jsonObj = mBuilder->CreateCall( getOrDeclareJsonObject(), {}, "json.obj" );

	// For each field: extract, wrap in JSON value, set on object
	for ( unsigned i = 0; i < structDef->mFields.size(); i++ )
	{
		VariableDefinition *field = structDef->mFields[i];
		std::string fieldName = field->getName();
		Type *fieldType = field->getVariableType();
		std::string typeName = fieldType ? fieldType->getName() : "int";

		// GEP to field
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, selfAlloca, i, fieldName + ".ptr" );
		llvm::Value *fieldVal = mBuilder->CreateLoad( getLLVMType( fieldType ), fieldPtr, fieldName + ".val" );

		// Create JSON value based on type
		llvm::Value *jsonVal = nullptr;
		if ( typeName == "int" || typeName == "short" || typeName == "long" )
		{
			// Extend to i64 for the JSON runtime
			llvm::Value *i64Val = fieldVal;
			if ( !fieldVal->getType()->isIntegerTy( 64 ) )
				i64Val = mBuilder->CreateSExt( fieldVal, llvm::Type::getInt64Ty( *mContext ), fieldName + ".ext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonInt(), { i64Val }, fieldName + ".json" );
		}
		else if ( typeName == "char" )
		{
			// Extend i8 to i64 and use JSON int
			llvm::Value *i64Val = mBuilder->CreateSExt( fieldVal, llvm::Type::getInt64Ty( *mContext ), fieldName + ".ext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonInt(), { i64Val }, fieldName + ".json" );
		}
		else if ( typeName == "float" || typeName == "double" )
		{
			llvm::Value *dblVal = fieldVal;
			if ( fieldVal->getType()->isFloatTy() )
				dblVal = mBuilder->CreateFPExt( fieldVal, llvm::Type::getDoubleTy( *mContext ), fieldName + ".fpext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonFloat(), { dblVal }, fieldName + ".json" );
		}
		else if ( typeName == "string" )
		{
			// Extract raw char* from BlangString for JSON runtime
			llvm::StructType *bsType = llvm::StructType::get( *mContext,
				{ ptrTy,
				  llvm::Type::getInt64Ty( *mContext ),
				  llvm::Type::getInt64Ty( *mContext ),
				  llvm::Type::getInt32Ty( *mContext ) } );
			llvm::Value *dataGep = mBuilder->CreateStructGEP(
				bsType, fieldVal, 0, fieldName + ".data.ptr" );
			llvm::Value *rawStr = mBuilder->CreateLoad( ptrTy, dataGep, fieldName + ".data" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonString(), { rawStr }, fieldName + ".json" );
		}
		else if ( typeName == "bool" )
		{
			// Extend i1 to i32
			llvm::Value *i32Val = mBuilder->CreateZExt( fieldVal, llvm::Type::getInt32Ty( *mContext ), fieldName + ".zext" );
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonBool(), { i32Val }, fieldName + ".json" );
		}
		else
		{
			// Check if this is a nested @json struct
			auto it = mStructDefMap.find( typeName );
			if ( it != mStructDefMap.end() )
			{
				StructDefinition *nestedDef = it->second;
				bool hasJson = false;
				for ( const auto &ann : nestedDef->getAnnotations() )
				{
					if ( ann.mName == "json" )
					{
						hasJson = true;
						break;
					}
				}
				if ( hasJson )
				{
					// Call NestedStruct_to_json(ptr) -> char*
					std::string nestedToJson = typeName + "_to_json";
					llvm::Function *nestedFn = mModule->getFunction( nestedToJson );
					if ( !nestedFn )
					{
						// Forward declare if not yet generated
						llvm::FunctionType *nestedFt = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
						nestedFn = llvm::Function::Create(
							nestedFt, llvm::Function::ExternalLinkage, nestedToJson, mModule.get() );
					}
					llvm::Value *nestedStr = mBuilder->CreateCall( nestedFn, { fieldVal }, fieldName + ".str" );

					// Extract raw char* from BlangString for json_decode
					llvm::StructType *bsType2 = llvm::StructType::get( *mContext,
						{ ptrTy,
						  llvm::Type::getInt64Ty( *mContext ),
						  llvm::Type::getInt64Ty( *mContext ),
						  llvm::Type::getInt32Ty( *mContext ) } );
					llvm::Value *nestedDataPtr = mBuilder->CreateStructGEP(
						bsType2, nestedStr, 0, fieldName + ".data.ptr" );
					llvm::Value *nestedRawStr = mBuilder->CreateLoad(
						ptrTy, nestedDataPtr, fieldName + ".data" );

					// Decode the JSON string back to a value tree
					llvm::Value *nullErrPtr = llvm::ConstantPointerNull::get(
						llvm::PointerType::get( *mContext, 0 ) );
					jsonVal = mBuilder->CreateCall(
						getOrDeclareJsonDecode(), { nestedRawStr, nullErrPtr }, fieldName + ".json" );

					// Release the intermediate BlangString
					mBuilder->CreateCall( getOrDeclareStringRelease(), { nestedStr } );
				}
				else
				{
					cerr << "Error: @json struct '" << structDef->getName()
						<< "' has field '" << fieldName << "' of struct type '" << typeName
						<< "' which does not have @json annotation" << endl;
					return false;
				}
			}
			else
			{
				cerr << "Error: @json struct '" << structDef->getName()
					<< "' has field '" << fieldName << "' of unsupported type '" << typeName << "'" << endl;
				return false;
			}
		}

		llvm::Value *keyStr = mBuilder->CreateGlobalStringPtr( fieldName, fieldName + ".key" );
		mBuilder->CreateCall( getOrDeclareJsonObjectSet(), { jsonObj, keyStr, jsonVal } );
	}

	// Encode to string (returns raw char*)
	llvm::Value *rawStr = mBuilder->CreateCall( getOrDeclareJsonEncode(), { jsonObj }, "json.str" );

	// Free the JSON tree
	mBuilder->CreateCall( getOrDeclareJsonFree(), { jsonObj } );

	// Wrap raw char* in BlangString: strlen then __blang_string_create
	llvm::Value *len = mBuilder->CreateCall( getOrDeclareStrlen(), { rawStr }, "json.len" );
	llvm::Value *result = mBuilder->CreateCall(
		getOrDeclareStringCreate(), { rawStr, len }, "json.blangstr" );

	// Free the malloc'd char* from __blang_json_encode (string_create copied it)
	mBuilder->CreateCall( getOrDeclareFree(), { rawStr } );

	mBuilder->CreateRet( result );
	return true;
}

bool CodeGen::genJsonFromJson( StructDefinition *structDef )
{
	llvm::StructType *structType = getOrCreateStructType( structDef );
	std::string funcName = structDef->getName() + "_from_json";

	// StructName_from_json(ptr input) -> ptr (heap-allocated struct)
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	llvm::Function *func = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, funcName, mModule.get() );
	func->getArg( 0 )->setName( "input" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", func );
	mBuilder->SetInsertPoint( entryBB );

	// Extract raw char* from BlangString input (GEP to .data field)
	llvm::StructType *bsType = llvm::StructType::get( *mContext,
		{ ptrTy,
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt64Ty( *mContext ),
		  llvm::Type::getInt32Ty( *mContext ) } );
	llvm::Value *dataPtr = mBuilder->CreateStructGEP(
		bsType, func->getArg( 0 ), 0, "input.data.ptr" );
	llvm::Value *rawInput = mBuilder->CreateLoad( ptrTy, dataPtr, "input.data" );

	// Decode JSON string
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );
	llvm::Value *jsonObj = mBuilder->CreateCall(
		getOrDeclareJsonDecode(), { rawInput, nullPtr }, "json.obj" );

	// Heap-allocate result struct via ARC (with destructor for refcounted fields)
	llvm::DataLayout dl( mModule.get() );
	uint64_t dataSize = dl.getTypeAllocSize( structType );
	llvm::Value *sizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), dataSize );
	std::map<std::string, std::string> emptyTypeSub;
	llvm::Function *dtorFn = getOrGenStructDestructor( structDef, emptyTypeSub );
	llvm::Value *resultAlloca = nullptr;
	if ( dtorFn != nullptr )
		resultAlloca = mBuilder->CreateCall(
			getOrDeclareRcAllocDtor(), { sizeVal, dtorFn }, "result.ptr" );
	else
		resultAlloca = mBuilder->CreateCall(
			getOrDeclareRcAlloc(), { sizeVal }, "result.ptr" );

	// For each field: look up in JSON, extract, store
	for ( unsigned i = 0; i < structDef->mFields.size(); i++ )
	{
		VariableDefinition *field = structDef->mFields[i];
		std::string fieldName = field->getName();
		Type *fieldType = field->getVariableType();
		std::string typeName = fieldType ? fieldType->getName() : "int";

		// Get the field node from JSON
		llvm::Value *keyStr = mBuilder->CreateGlobalStringPtr( fieldName, fieldName + ".key" );
		llvm::Value *fieldNode = mBuilder->CreateCall(
			getOrDeclareJsonObjectGet(), { jsonObj, keyStr }, fieldName + ".node" );

		// Extract typed value
		llvm::Value *fieldVal = nullptr;
		if ( typeName == "int" || typeName == "short" || typeName == "long" )
		{
			llvm::Value *i64Val = mBuilder->CreateCall(
				getOrDeclareJsonGetInt(), { fieldNode }, fieldName + ".i64" );
			if ( typeName == "int" )
				fieldVal = mBuilder->CreateTrunc( i64Val, llvm::Type::getInt32Ty( *mContext ), fieldName + ".val" );
			else if ( typeName == "short" )
				fieldVal = mBuilder->CreateTrunc( i64Val, llvm::Type::getInt16Ty( *mContext ), fieldName + ".val" );
			else
				fieldVal = i64Val;
		}
		else if ( typeName == "char" )
		{
			llvm::Value *i64Val = mBuilder->CreateCall(
				getOrDeclareJsonGetInt(), { fieldNode }, fieldName + ".i64" );
			fieldVal = mBuilder->CreateTrunc( i64Val, llvm::Type::getInt8Ty( *mContext ), fieldName + ".val" );
		}
		else if ( typeName == "float" || typeName == "double" )
		{
			llvm::Value *dblVal = mBuilder->CreateCall(
				getOrDeclareJsonGetFloat(), { fieldNode }, fieldName + ".dbl" );
			if ( typeName == "float" )
				fieldVal = mBuilder->CreateFPTrunc( dblVal, llvm::Type::getFloatTy( *mContext ), fieldName + ".val" );
			else
				fieldVal = dblVal;
		}
		else if ( typeName == "string" )
		{
			// __blang_json_get_string returns raw char*, wrap in BlangString
			llvm::Value *rawStr = mBuilder->CreateCall(
				getOrDeclareJsonGetString(), { fieldNode }, fieldName + ".raw" );
			llvm::Value *len = mBuilder->CreateCall(
				getOrDeclareStrlen(), { rawStr }, fieldName + ".len" );
			fieldVal = mBuilder->CreateCall(
				getOrDeclareStringCreate(), { rawStr, len }, fieldName + ".val" );
		}
		else if ( typeName == "bool" )
		{
			llvm::Value *i32Val = mBuilder->CreateCall(
				getOrDeclareJsonGetBool(), { fieldNode }, fieldName + ".i32" );
			fieldVal = mBuilder->CreateTrunc( i32Val, llvm::Type::getInt1Ty( *mContext ), fieldName + ".val" );
		}
		else
		{
			// Check if this is a nested @json struct
			auto it = mStructDefMap.find( typeName );
			if ( it != mStructDefMap.end() )
			{
				StructDefinition *nestedDef = it->second;
				bool hasJson = false;
				for ( const auto &ann : nestedDef->getAnnotations() )
				{
					if ( ann.mName == "json" )
					{
						hasJson = true;
						break;
					}
				}
				if ( hasJson )
				{
					// Encode the nested JSON node back to a string
					llvm::Value *nestedRawStr = mBuilder->CreateCall(
						getOrDeclareJsonEncode(), { fieldNode }, fieldName + ".rawstr" );

					// Wrap raw char* in BlangString for from_json call
					llvm::Value *nestedLen = mBuilder->CreateCall(
						getOrDeclareStrlen(), { nestedRawStr }, fieldName + ".len" );
					llvm::Value *nestedStr = mBuilder->CreateCall(
						getOrDeclareStringCreate(), { nestedRawStr, nestedLen }, fieldName + ".str" );

					// Free the malloc'd char* from __blang_json_encode (string_create copied it)
					mBuilder->CreateCall( getOrDeclareFree(), { nestedRawStr } );

					// Call NestedStruct_from_json(str) -> ptr (heap-allocated)
					std::string nestedFromJson = typeName + "_from_json";
					llvm::Function *nestedFn = mModule->getFunction( nestedFromJson );
					if ( !nestedFn )
					{
						llvm::FunctionType *nestedFt = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
						nestedFn = llvm::Function::Create(
							nestedFt, llvm::Function::ExternalLinkage, nestedFromJson, mModule.get() );
					}
					fieldVal = mBuilder->CreateCall( nestedFn, { nestedStr }, fieldName + ".val" );

					// Release the intermediate BlangString
					mBuilder->CreateCall( getOrDeclareStringRelease(), { nestedStr } );
				}
				else
				{
					cerr << "Error: @json struct '" << structDef->getName()
						<< "' has field '" << fieldName << "' of struct type '" << typeName
						<< "' which does not have @json annotation" << endl;
					return false;
				}
			}
			else
			{
				cerr << "Error: @json struct '" << structDef->getName()
					<< "' has field '" << fieldName << "' of unsupported type '" << typeName << "'" << endl;
				return false;
			}
		}

		// Store to result struct
		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, resultAlloca, i, fieldName + ".ptr" );
		mBuilder->CreateStore( fieldVal, fieldPtr );
	}

	// Free JSON tree
	mBuilder->CreateCall( getOrDeclareJsonFree(), { jsonObj } );

	// Return the heap-allocated struct pointer
	mBuilder->CreateRet( resultAlloca );
	return true;
}

// ---- JSON runtime declarations ----

llvm::Function *CodeGen::getOrDeclareJsonObject()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_object" );
	if ( f ) return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ), {}, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_object", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonInt()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_int" );
	if ( f ) return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt64Ty( *mContext ) }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_int", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonFloat()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_float" );
	if ( f ) return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getDoubleTy( *mContext ) }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_float", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonString()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_string" );
	if ( f ) return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::PointerType::get( *mContext, 0 ) }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonBool()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_bool" );
	if ( f ) return f;

	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::PointerType::get( *mContext, 0 ),
		{ llvm::Type::getInt32Ty( *mContext ) }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_bool", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonObjectSet()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_object_set" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrTy, ptrTy, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_object_set", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonObjectGet()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_object_get" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_object_get", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonEncode()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_encode" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_encode", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonDecode()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_decode" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy, ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_decode", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonFree()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_free" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ), { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_free", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonGetInt()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_get_int" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt64Ty( *mContext ), { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_get_int", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonGetFloat()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_get_float" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getDoubleTy( *mContext ), { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_get_float", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonGetString()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_get_string" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_get_string", mModule.get() );
}

llvm::Function *CodeGen::getOrDeclareJsonGetBool()
{
	llvm::Function *f = mModule->getFunction( "__blang_json_get_bool" );
	if ( f ) return f;

	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getInt32Ty( *mContext ), { ptrTy }, false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_json_get_bool", mModule.get() );
}
