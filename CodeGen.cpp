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

	// Register built-in Option<T>/Result<T,E> for codegen resolution (match,
	// getLLVMType, EnumConstruct, chan recv, the `?` operator) unless the
	// program defines its own — a user definition (already in mEnumDefMap from
	// the loop above) takes precedence. The parser/Sema see these via gScope
	// (see qcc.cpp); codegen needs its own copy in mEnumDefMap because synthetic
	// built-ins are not part of the module's mEnumList. mSyntheticEnums owns the
	// SmartPtr so the raw pointer stored in mEnumDefMap stays alive.
	if ( mEnumDefMap.find( "Option" ) == mEnumDefMap.end() )
	{
		SmartPtr<EnumDefinition> opt = EnumDefinition::CreateBuiltinOption();
		mSyntheticEnums.push_back( opt );
		mEnumDefMap["Option"] = opt;
	}
	if ( mEnumDefMap.find( "Result" ) == mEnumDefMap.end() )
	{
		SmartPtr<EnumDefinition> res = EnumDefinition::CreateBuiltinResult();
		mSyntheticEnums.push_back( res );
		mEnumDefMap["Result"] = res;
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

			// Save state for method body generation
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

	// Generate test runner function if there are tests. In test-runner mode
	// (qcc --emit-test-main) emit a real main() that dispatches to the C test
	// driver; otherwise keep the legacy __blang_run_tests path unchanged.
	if ( !testFunctions.empty() )
	{
		if ( mTestMode )
			genTestMain( testFunctions, mod->mTestBlocks );
		else
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
		// Store the raw verifier text; the driver decides whether to surface
		// it. By default it reports a concise internal-compiler-error line and
		// hides this (U2, FR-010); the raw text is shown only under
		// --debug-compiler.
		mVerifyError = errStream.str();
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
			llvm::Function *mallocFn = getOrDeclareMalloc();
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
			llvm::Function *mallocFn = getOrDeclareMalloc();
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
		else if ( ptName == "Buffer" && !mBufferScopeStack.empty() &&
				  mStructDefMap.find( "Buffer" ) == mStructDefMap.end() )
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
			// Release any temporary struct literals created during this statement
			releaseTempStructs();
			// Release any temporary arrays (rvalue Array<T> from a call/method)
			releaseTempArrays();
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
			emitEnumPayloadRelease( entry.alloca, entry.enumDef, entry.concreteType );
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

// ---- Temporary string/lambda tracking ----

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

void CodeGen::trackTempStruct( llvm::Value *structPtr )
{
	if ( structPtr != nullptr )
		mTempStructs.push_back( structPtr );
}

void CodeGen::releaseTempStructs()
{
	if ( mTempStructs.empty() )
		return;
	if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
	{
		mTempStructs.clear();
		return;
	}
	for ( auto *val : mTempStructs )
		mBuilder->CreateCall( getOrDeclareRcRelease(), { val } );
	mTempStructs.clear();
}

void CodeGen::untrackTempStruct( llvm::Value *structPtr )
{
	for ( auto it = mTempStructs.begin(); it != mTempStructs.end(); ++it )
	{
		if ( *it == structPtr )
		{
			mTempStructs.erase( it );
			return;
		}
	}
}

void CodeGen::trackTempArray( llvm::Value *arrPtr )
{
	if ( arrPtr != nullptr )
		mTempArrays.push_back( arrPtr );
}

void CodeGen::releaseTempArrays()
{
	if ( mTempArrays.empty() )
		return;
	if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
	{
		mTempArrays.clear();
		return;
	}
	for ( auto *val : mTempArrays )
		mBuilder->CreateCall( getOrDeclareArrayRelease(), { val } );
	mTempArrays.clear();
}

void CodeGen::untrackTempArray( llvm::Value *arrPtr )
{
	for ( auto it = mTempArrays.begin(); it != mTempArrays.end(); ++it )
	{
		if ( *it == arrPtr )
		{
			mTempArrays.erase( it );
			return;
		}
	}
}

// ---- Statement dispatcher ----

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

// ---- Expression dispatcher ----

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
	else if ( auto *ctorExpr = dynamic_cast<ConstructExpression*>( expr ) )
		return genConstructExpression( ctorExpr );
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

	// U4 (REQ-012): an expression node reaching here is unhandled by code
	// generation. After the semantic pass this cannot happen for a valid
	// program, so it is a loud internal compiler error — never a silent skip.
	std::cerr << "internal compiler error: unhandled expression node in code "
	             "generation \u2014 please report" << std::endl;
	mHasError = true;
	return nullptr;
}

// ---- Break/Continue ----

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
