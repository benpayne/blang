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

llvm::Type *CodeGen::getLLVMType( Type *type )
{
	if ( type == nullptr )
		return llvm::Type::getVoidTy( *mContext );

	const std::string &name = type->getName();

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
	else if ( name == "void" )
		return llvm::Type::getVoidTy( *mContext );
	else if ( name == "bool" )
		return llvm::Type::getInt1Ty( *mContext );

	// Check for known struct types
	auto structIt = mStructTypeMap.find( name );
	if ( structIt != mStructTypeMap.end() )
		return structIt->second;

	// Look up struct definitions registered during generate()
	auto defIt = mStructDefMap.find( name );
	if ( defIt != mStructDefMap.end() )
		return getOrCreateStructType( defIt->second );

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

	// Generate methods from impl blocks as regular LLVM functions
	for ( auto &structDef : mod->mStructList )
	{
		for ( auto &method : structDef->mMethods )
		{
			// Skip generic methods
			if ( method->isGeneric() )
				continue;

			// Generate the method with a mangled name: StructName_methodName
			string mangledName = structDef->getName() + "_" + method->getName();

			// Build the function type
			llvm::Type *retType = getLLVMType( method->mReturnType );
			std::vector<llvm::Type*> paramTypes;
			for ( auto &param : method->mParameters )
			{
				// Handle 'self' parameter — use the struct type
				if ( param->getVariableType() != nullptr &&
					 param->getVariableType()->getName() == "self" )
				{
					paramTypes.push_back( getOrCreateStructType( structDef ) );
				}
				else
				{
					paramTypes.push_back( getLLVMType( param->getVariableType() ) );
				}
			}

			llvm::FunctionType *ft = llvm::FunctionType::get(
				retType, paramTypes, method->isVariadic() );
			llvm::Function *llvmFunc = llvm::Function::Create(
				ft, llvm::Function::ExternalLinkage, mangledName, mModule.get() );

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

	return true;
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

	std::vector<llvm::Type*> paramTypes;
	for ( auto &param : func->mParameters )
	{
		paramTypes.push_back( getLLVMType( param->getVariableType() ) );
	}

	llvm::FunctionType *ft = llvm::FunctionType::get( retType, paramTypes, func->isVariadic() );
	llvm::Function *llvmFunc = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, func->getName(), mModule.get() );

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
		return llvmFunc;
	}

	// Name the parameters
	unsigned idx = 0;
	for ( auto &arg : llvmFunc->args() )
	{
		arg.setName( func->mParameters[idx]->getName() );
		idx++;
	}

	// Create the entry basic block
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", llvmFunc );
	mBuilder->SetInsertPoint( entryBB );

	// Track current function for contract support
	mCurrentFunction = func;
	mResultAlloca = nullptr;

	// Create allocas for parameters and store the argument values
	idx = 0;
	for ( auto &arg : llvmFunc->args() )
	{
		VariableDefinition *paramDef = func->mParameters[idx];
		llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
			arg.getType(), nullptr, paramDef->getName() );
		mBuilder->CreateStore( &arg, alloca );
		mVariableMap[paramDef] = alloca;
		idx++;
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

	// Now that body is generated, inject runtime init if concurrency was discovered
	if ( isMain && mUsesConcurrency && !concurrencyBefore )
	{
		// Insert the init call at the end of the entry block, before the branch
		llvm::Instruction *brInst = initBB->getTerminator();
		mBuilder->SetInsertPoint( brInst );
		mBuilder->CreateCall( getOrDeclareRuntimeInit(),
			{ llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 4 ) } );
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

	// Clear function context
	mCurrentFunction = nullptr;
	mResultAlloca = nullptr;
	mVariableMap.clear();

	return llvmFunc;
}

void CodeGen::genBlock( Block *block )
{
	// Push a new ARC scope to track shared/sync variables declared in this block
	mArcScopeStack.push_back( {} );

	for ( auto &stmt : block->mStatementList )
	{
		if ( stmt != nullptr )
		{
			// Don't generate code after a terminator (unreachable code)
			if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
				break;
			genStatement( stmt );
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
	}

	mArcScopeStack.pop_back();
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
			// Heap-allocate via runtime ARC.
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
		else
		{
			// Value type or own: stack allocation (same as before)
			llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
				llvmType, nullptr, varDef->getName() );
			mVariableMap[varDef] = alloca;

			// If there's an initializer, generate it and store
			if ( data.mInitialValue != nullptr )
			{
				llvm::Value *initVal = genExpression( data.mInitialValue );
				if ( initVal != nullptr )
				{
					// Cast if types don't match
					if ( initVal->getType() != llvmType )
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
						mBuilder->CreateStore( initVal, alloca );
				}
			}
		}
	}
}

void CodeGen::genReturnStatement( ReturnStatement *ret )
{
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

	// In an async wrapper, return statements store the value and branch to exit
	if ( mAsyncExitBB != nullptr )
	{
		if ( ret->mExpression != nullptr && mAsyncResultAlloca != nullptr )
		{
			llvm::Value *retVal = genExpression( ret->mExpression );
			if ( retVal != nullptr )
			{
				// Cast if needed
				if ( retVal->getType() != mAsyncReturnType )
				{
					if ( mAsyncReturnType->isIntegerTy() && retVal->getType()->isIntegerTy() )
						retVal = mBuilder->CreateIntCast( retVal, mAsyncReturnType, true, "icast" );
				}
				mBuilder->CreateStore( retVal, mAsyncResultAlloca );
			}
		}
		mBuilder->CreateBr( mAsyncExitBB );
		return;
	}

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::Type *expectedType = func->getReturnType();

	if ( ret->mExpression != nullptr )
	{
		llvm::Value *retVal = genExpression( ret->mExpression );
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
					// Returning a primitive for a struct-typed function — create a
					// zero-initialized struct (semantic mismatch, but valid IR)
					retVal = llvm::Constant::getNullValue( expectedType );
				}
				else if ( expectedType->isIntegerTy() && retVal->getType()->isPointerTy() )
				{
					// Pointer to integer conversion (e.g., query result as int)
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
		else if ( expectedType->isVoidTy() )
		{
			mBuilder->CreateRetVoid();
		}
		else
		{
			// Expression generation failed — emit a default return value
			mBuilder->CreateRet( llvm::Constant::getNullValue( expectedType ) );
		}
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

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

	llvm::BasicBlock *thenBB = llvm::BasicBlock::Create( *mContext, "then", func );
	llvm::BasicBlock *elseBB = llvm::BasicBlock::Create( *mContext, "else", func );
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "ifmerge", func );

	mBuilder->CreateCondBr( condVal, thenBB, elseBB );

	// Then block
	mBuilder->SetInsertPoint( thenBB );
	if ( ifStmt->mStatement != nullptr )
		genStatement( ifStmt->mStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( mergeBB );

	// Else block
	mBuilder->SetInsertPoint( elseBB );
	if ( ifStmt->mElseStatement != nullptr )
		genStatement( ifStmt->mElseStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( mergeBB );

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
		mBuilder->CreateCondBr( condVal, bodyBB, afterBB );
	}

	// Push loop targets for break/continue
	mLoopStack.push_back( { condBB, afterBB } );

	// Body block
	mBuilder->SetInsertPoint( bodyBB );
	if ( whileStmt->mLoopStatement != nullptr )
		genStatement( whileStmt->mLoopStatement );
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
	return mBuilder->CreateGlobalStringPtr( cs->mValue, "str" );
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
	auto it = mVariableMap.find( varDef );
	if ( it == mVariableMap.end() )
	{
		cerr << "CodeGen: undefined variable '" << varDef->getName() << "'" << endl;
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

	// Look up the LLVM function
	llvm::Function *llvmFunc = nullptr;
	auto it = mFunctionMap.find( funcDef );
	if ( it != mFunctionMap.end() )
	{
		llvmFunc = it->second;
	}
	else
	{
		// Try by name in the module
		llvmFunc = mModule->getFunction( funcDef->getName() );
	}

	if ( llvmFunc == nullptr )
	{
		cerr << "CodeGen: undefined function '" << funcDef->getName() << "'" << endl;
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

	if ( llvmFunc->getReturnType()->isVoidTy() )
	{
		mBuilder->CreateCall( llvmFunc, args );
		return nullptr;
	}

	return mBuilder->CreateCall( llvmFunc, args, "calltmp" );
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

	const string &op = ops->mOperation;

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
		mBuilder->CreateStore( rhs, alloca );
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
	llvm::StructType *structType = getOrCreateStructType( structDef );

	// Allocate the struct on the stack
	llvm::AllocaInst *alloca = mBuilder->CreateAlloca( structType, nullptr, "structlit" );

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

		llvm::Value *fieldVal = genExpression( expr->mFieldValues[i] );
		if ( fieldVal == nullptr )
			continue;

		llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, alloca, fieldIdx, "field" );
		mBuilder->CreateStore( fieldVal, fieldPtr );
	}

	// Load and return the struct value
	return mBuilder->CreateLoad( structType, alloca, "structval" );
}

llvm::Value *CodeGen::genFieldAccess( FieldAccessExpression *expr )
{
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

	llvm::Value *fieldPtr = mBuilder->CreateStructGEP( structType, objAddr, fieldIdx, expr->mFieldName );
	return mBuilder->CreateLoad(
		structType->getElementType( fieldIdx ), fieldPtr, expr->mFieldName + ".val" );
}

llvm::Value *CodeGen::genMethodCall( MethodCallExpression *expr )
{
	// Determine the struct type from the object
	StructDefinition *structDef = nullptr;
	string structName;

	if ( auto *ve = dynamic_cast<VariableExpression*>( (Expression*)expr->mObject ) )
	{
		Type *varType = ve->mVariable->getVariableType();
		if ( varType != nullptr )
		{
			structName = varType->getName();
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

	if ( methodDef == nullptr )
		return nullptr;

	// Look up the LLVM function for this method
	llvm::Function *llvmFunc = nullptr;
	auto it = mFunctionMap.find( methodDef );
	if ( it != mFunctionMap.end() )
		llvmFunc = it->second;

	if ( llvmFunc == nullptr )
	{
		// Try by mangled name
		string mangledName = structName + "_" + expr->mMethodName;
		llvmFunc = mModule->getFunction( mangledName );
	}

	if ( llvmFunc == nullptr )
		return nullptr;

	// Build arguments: self first, then explicit args
	std::vector<llvm::Value*> args;

	// Generate self (the object value)
	llvm::Value *selfVal = genExpression( expr->mObject );
	if ( selfVal != nullptr )
		args.push_back( selfVal );

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

llvm::Value *CodeGen::genTryExpression( TryExpression *expr )
{
	// Generate the operand expression (e.g., might_fail())
	llvm::Value *result = genExpression( expr->mOperand );
	if ( result == nullptr )
		return nullptr;

	// Simplified ? operator: pass the value through.
	// Full implementation would check the tag for Result/Option and
	// branch to an early return on error. For now, we treat the value
	// as already unwrapped (works when Result/Option maps to i32).
	return result;
}

// ---- Array codegen ----

llvm::Value *CodeGen::genArrayLiteral( ArrayLiteralExpression *expr )
{
	if ( expr->mElements.empty() )
		return nullptr;

	// Determine element type from the first element
	llvm::Value *firstElem = genExpression( expr->mElements[0] );
	if ( firstElem == nullptr )
		return nullptr;

	llvm::Type *elemType = firstElem->getType();
	int numElements = static_cast<int>( expr->mElements.size() );
	llvm::ArrayType *arrType = llvm::ArrayType::get( elemType, numElements );

	// Allocate the array on the stack
	llvm::AllocaInst *alloca = mBuilder->CreateAlloca( arrType, nullptr, "arr" );

	// Store the first element
	llvm::Value *idx0 = llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 0 );
	llvm::Value *elemPtr = mBuilder->CreateGEP( arrType, alloca,
		{ idx0, idx0 }, "arr.elem" );
	mBuilder->CreateStore( firstElem, elemPtr );

	// Store remaining elements
	for ( int i = 1; i < numElements; i++ )
	{
		llvm::Value *elemVal = genExpression( expr->mElements[i] );
		if ( elemVal == nullptr )
			continue;

		llvm::Value *idxVal = llvm::ConstantInt::get(
			llvm::Type::getInt32Ty( *mContext ), i );
		llvm::Value *ep = mBuilder->CreateGEP( arrType, alloca,
			{ idx0, idxVal }, "arr.elem" );
		mBuilder->CreateStore( elemVal, ep );
	}

	return alloca;
}

llvm::Value *CodeGen::genIndexExpression( IndexExpression *expr )
{
	// Generate the array (should be a pointer to an array alloca)
	llvm::Value *arrVal = genExpression( expr->mObject );
	if ( arrVal == nullptr )
		return nullptr;

	// Generate the index
	llvm::Value *idxVal = genExpression( expr->mIndex );
	if ( idxVal == nullptr )
		return nullptr;

	// arrVal should be a pointer to an array from genArrayLiteral
	if ( auto *alloca = llvm::dyn_cast<llvm::AllocaInst>( arrVal ) )
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

	// void __blang_spawn( void(*fn)(void*), void *ctx )
	llvm::FunctionType *ft = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::PointerType::get( *mContext, 0 ) },
		false );
	return llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, "__blang_spawn", mModule.get() );
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

void CodeGen::genSpawnStatement( SpawnStatement *spawn )
{
	if ( spawn->mBody == nullptr )
		return;

	mUsesConcurrency = true;

	// Identify captured variables: variables referenced in the spawn body
	// that are defined in an outer scope. We scan the block's scope for
	// all variables and check which ones have allocas in mVariableMap
	// (meaning they were defined before the spawn block).
	Block *bodyBlock = spawn->mBody;
	std::vector<std::pair<VariableDefinition*, llvm::AllocaInst*>> captures;

	// Collect outer variables used in this scope, enforcing ownership rules
	if ( bodyBlock->mScope != nullptr )
	{
		for ( auto &entry : mVariableMap )
		{
			OwnershipQualifier capOwnership = entry.first->getOwnership();

			// Reject own variables in spawn — own cannot cross spawn boundaries
			if ( capOwnership == OwnershipQualifier::kOwnership_Own )
			{
				cerr << "CodeGen error: own variable '" << entry.first->getName()
					 << "' cannot be captured in spawn block" << endl;
				return;
			}

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

	// Generate the spawn function body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", spawnFn );
	mBuilder->SetInsertPoint( entryBB );

	mVariableMap.clear();
	mLoopStack.clear();
	mArcScopeStack.clear();

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

	// Back in the caller: allocate context, populate, and call __blang_spawn
	llvm::DataLayout dl( mModule.get() );
	uint64_t ctxSize = dl.getTypeAllocSize( ctxType );

	llvm::Function *mallocFn = getOrDeclareMalloc();
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

	// Call __blang_spawn(spawn_body, ctx)
	mBuilder->CreateCall( getOrDeclareSpawn(), { spawnFn, ctxAlloc } );
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

	// Generate callback body
	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create(
		*mContext, "entry", cbFn );
	mBuilder->SetInsertPoint( entryBB );

	mVariableMap.clear();
	mLoopStack.clear();
	mArcScopeStack.clear();

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

	// In the caller: evaluate the event expression (for side effects)
	if ( handler->mEventExpression != nullptr )
		genExpression( handler->mEventExpression );

	// Allocate context and populate captures
	llvm::DataLayout dl( mModule.get() );
	uint64_t ctxSize = dl.getTypeAllocSize( ctxType );
	llvm::Function *mallocFn = getOrDeclareMalloc();
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

		if ( forInStmt->mBody != nullptr )
			genStatement( forInStmt->mBody );

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
			if ( forInStmt->mBody != nullptr )
				genStatement( forInStmt->mBody );
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

// ---- String interpolation codegen ----

llvm::Value *CodeGen::genStringInterpolation( StringInterpolation *interp )
{
	if ( interp->mParts.empty() )
		return mBuilder->CreateGlobalStringPtr( "", "empty.str" );

	// Build a format string and collect argument values for snprintf
	std::string fmtStr;
	std::vector<llvm::Value*> fmtArgs;

	for ( auto &part : interp->mParts )
	{
		if ( auto *cs = dynamic_cast<ConstString*>( (Expression*)part ) )
		{
			// Literal string segment — append to format string as-is
			// Escape any % characters for printf
			for ( char c : cs->mValue )
			{
				if ( c == '%' )
					fmtStr += "%%";
				else
					fmtStr += c;
			}
		}
		else
		{
			// Expression segment — generate value and add format specifier
			llvm::Value *val = genExpression( part );
			if ( val == nullptr )
				continue;

			if ( val->getType()->isIntegerTy() )
			{
				fmtStr += "%d";
				// Extend to i32 if narrower (e.g., i1 for bool)
				if ( !val->getType()->isIntegerTy( 32 ) )
					val = mBuilder->CreateSExt( val,
						llvm::Type::getInt32Ty( *mContext ), "ext" );
				fmtArgs.push_back( val );
			}
			else if ( val->getType()->isFloatTy() || val->getType()->isDoubleTy() )
			{
				fmtStr += "%f";
				if ( val->getType()->isFloatTy() )
					val = mBuilder->CreateFPExt( val,
						llvm::Type::getDoubleTy( *mContext ), "fpext" );
				fmtArgs.push_back( val );
			}
			else if ( val->getType()->isPointerTy() )
			{
				fmtStr += "%s";
				fmtArgs.push_back( val );
			}
			else
			{
				fmtStr += "<?>";
			}
		}
	}

	// Use snprintf to build the result string into a stack buffer
	llvm::Function *snprintfFn = getOrDeclareSnprintf();

	// Allocate a 256-byte buffer on the stack
	llvm::Type *i8Ty = llvm::Type::getInt8Ty( *mContext );
	llvm::Type *i32Ty = llvm::Type::getInt32Ty( *mContext );
	llvm::Value *bufSize = llvm::ConstantInt::get( i32Ty, 256 );
	llvm::AllocaInst *buf = mBuilder->CreateAlloca( i8Ty, bufSize, "interp.buf" );

	// Build snprintf call: snprintf(buf, 256, fmt, args...)
	std::vector<llvm::Value*> snprintfArgs;
	snprintfArgs.push_back( buf );
	llvm::Value *sizeVal = llvm::ConstantInt::get(
		llvm::Type::getInt64Ty( *mContext ), 256 );
	snprintfArgs.push_back( sizeVal );
	snprintfArgs.push_back( mBuilder->CreateGlobalStringPtr( fmtStr, "interp.fmt" ) );
	for ( auto *arg : fmtArgs )
		snprintfArgs.push_back( arg );

	mBuilder->CreateCall( snprintfFn, snprintfArgs );

	return buf;
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

	// StructName_to_json(StructType self) -> char*
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { structType }, false );
	llvm::Function *func = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, funcName, mModule.get() );
	func->getArg( 0 )->setName( "self" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", func );
	mBuilder->SetInsertPoint( entryBB );

	// Store struct arg to alloca for GEP access
	llvm::AllocaInst *selfAlloca = mBuilder->CreateAlloca( structType, nullptr, "self.addr" );
	mBuilder->CreateStore( func->getArg( 0 ), selfAlloca );

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
			jsonVal = mBuilder->CreateCall( getOrDeclareJsonString(), { fieldVal }, fieldName + ".json" );
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
					// Call NestedStruct_to_json(fieldVal) -> char*
					std::string nestedToJson = typeName + "_to_json";
					llvm::Function *nestedFn = mModule->getFunction( nestedToJson );
					if ( !nestedFn )
					{
						// Forward declare if not yet generated
						llvm::StructType *nestedStructType = getOrCreateStructType( nestedDef );
						llvm::FunctionType *nestedFt = llvm::FunctionType::get( ptrTy, { nestedStructType }, false );
						nestedFn = llvm::Function::Create(
							nestedFt, llvm::Function::ExternalLinkage, nestedToJson, mModule.get() );
					}
					llvm::Value *nestedStr = mBuilder->CreateCall( nestedFn, { fieldVal }, fieldName + ".str" );

					// Decode the JSON string back to a value tree
					llvm::Value *nullErrPtr = llvm::ConstantPointerNull::get(
						llvm::PointerType::get( *mContext, 0 ) );
					jsonVal = mBuilder->CreateCall(
						getOrDeclareJsonDecode(), { nestedStr, nullErrPtr }, fieldName + ".json" );

					// Free the intermediate string
					llvm::Function *freeFn = mModule->getFunction( "free" );
					if ( !freeFn )
					{
						llvm::FunctionType *freeFt = llvm::FunctionType::get(
							llvm::Type::getVoidTy( *mContext ), { ptrTy }, false );
						freeFn = llvm::Function::Create(
							freeFt, llvm::Function::ExternalLinkage, "free", mModule.get() );
					}
					mBuilder->CreateCall( freeFn, { nestedStr } );
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

	// Encode to string
	llvm::Value *result = mBuilder->CreateCall( getOrDeclareJsonEncode(), { jsonObj }, "json.str" );

	// Free the JSON tree
	mBuilder->CreateCall( getOrDeclareJsonFree(), { jsonObj } );

	mBuilder->CreateRet( result );
	return true;
}

bool CodeGen::genJsonFromJson( StructDefinition *structDef )
{
	llvm::StructType *structType = getOrCreateStructType( structDef );
	std::string funcName = structDef->getName() + "_from_json";

	// StructName_from_json(char*) -> StructType
	llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
	llvm::FunctionType *ft = llvm::FunctionType::get( structType, { ptrTy }, false );
	llvm::Function *func = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, funcName, mModule.get() );
	func->getArg( 0 )->setName( "input" );

	llvm::BasicBlock *entryBB = llvm::BasicBlock::Create( *mContext, "entry", func );
	mBuilder->SetInsertPoint( entryBB );

	// Decode JSON string
	llvm::Value *nullPtr = llvm::ConstantPointerNull::get(
		llvm::PointerType::get( *mContext, 0 ) );
	llvm::Value *jsonObj = mBuilder->CreateCall(
		getOrDeclareJsonDecode(), { func->getArg( 0 ), nullPtr }, "json.obj" );

	// Alloca for result struct
	llvm::AllocaInst *resultAlloca = mBuilder->CreateAlloca( structType, nullptr, "result" );
	// Zero-initialize
	mBuilder->CreateStore( llvm::Constant::getNullValue( structType ), resultAlloca );

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
			fieldVal = mBuilder->CreateCall(
				getOrDeclareJsonGetString(), { fieldNode }, fieldName + ".val" );
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
					llvm::Value *nestedStr = mBuilder->CreateCall(
						getOrDeclareJsonEncode(), { fieldNode }, fieldName + ".str" );

					// Call NestedStruct_from_json(str) -> StructType
					std::string nestedFromJson = typeName + "_from_json";
					llvm::StructType *nestedStructType = getOrCreateStructType( nestedDef );
					llvm::Function *nestedFn = mModule->getFunction( nestedFromJson );
					if ( !nestedFn )
					{
						llvm::FunctionType *nestedFt = llvm::FunctionType::get( nestedStructType, { ptrTy }, false );
						nestedFn = llvm::Function::Create(
							nestedFt, llvm::Function::ExternalLinkage, nestedFromJson, mModule.get() );
					}
					fieldVal = mBuilder->CreateCall( nestedFn, { nestedStr }, fieldName + ".val" );

					// Free the intermediate string
					llvm::Function *freeFn = mModule->getFunction( "free" );
					if ( !freeFn )
					{
						llvm::FunctionType *freeFt = llvm::FunctionType::get(
							llvm::Type::getVoidTy( *mContext ), { ptrTy }, false );
						freeFn = llvm::Function::Create(
							freeFt, llvm::Function::ExternalLinkage, "free", mModule.get() );
					}
					mBuilder->CreateCall( freeFn, { nestedStr } );
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

	// Load and return result
	llvm::Value *result = mBuilder->CreateLoad( structType, resultAlloca, "result.val" );
	mBuilder->CreateRet( result );
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
