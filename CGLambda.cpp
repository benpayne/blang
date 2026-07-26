#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"

#include <iostream>

using namespace QLang;
using namespace std;

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

		llvm::Function *mallocFn = getOrDeclareMalloc();
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

	// Create callback function. Signature matches the event-loop handler ABI:
	// void(void* ctx, int fd). The fd argument is currently unused by handler
	// bodies (there is no binding syntax yet) but keeps the ABI uniform with
	// __blang_event_on's handler type.
	llvm::FunctionType *cbType = llvm::FunctionType::get(
		llvm::Type::getVoidTy( *mContext ),
		{ llvm::PointerType::get( *mContext, 0 ),
		  llvm::Type::getInt32Ty( *mContext ) },
		false );
	llvm::Function *cbFn = llvm::Function::Create(
		cbType, llvm::Function::InternalLinkage, handlerName, mModule.get() );
	cbFn->getArg( 0 )->setName( "ctx" );
	cbFn->getArg( 1 )->setName( "fd" );

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

	// In the caller: evaluate the event expression. An integer result is an
	// event source fd (a timerfd from timer.every()/after(), or a socket fd) and
	// the handler is registered on the global event loop; the program later
	// enters the loop explicitly via `timer.run()` / `events.run()`. Any other
	// result (e.g. `on someVoidCall()`) falls back to invoking the handler inline
	// once (the pre-event-loop behaviour). (Ported from origin's monolith.)
	llvm::Value *fdVal = nullptr;
	if ( handler->mEventExpression != nullptr )
		fdVal = genExpression( handler->mEventExpression );
	bool registerOnLoop = ( fdVal != nullptr && fdVal->getType()->isIntegerTy() );
	if ( registerOnLoop && !fdVal->getType()->isIntegerTy( 32 ) )
		fdVal = mBuilder->CreateIntCast(
			fdVal, llvm::Type::getInt32Ty( *mContext ), true, "on.fd" );

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

		// When registered on the loop the handler runs later (deferred), so
		// refcounted captures must be retained to survive the registering scope.
		if ( !registerOnLoop )
			continue;
		VariableDefinition *capDef = captures[i].first;
		Type *capType = capDef->getVariableType();
		if ( capType != nullptr )
		{
			OwnershipQualifier ownership = capDef->getOwnership();
			if ( ownership == OwnershipQualifier::kOwnership_Shared ||
				 ownership == OwnershipQualifier::kOwnership_Sync )
				mBuilder->CreateCall( getOrDeclareRcRetain(), { val } );
			else
			{
				const std::string &tn = capType->getName();
				if ( tn == "string" )
					mBuilder->CreateCall( getOrDeclareStringRetain(), { val } );
				else if ( tn == "Array" )
					mBuilder->CreateCall( getOrDeclareArrayRetain(), { val } );
				else if ( tn == "Buffer" )
					mBuilder->CreateCall( getOrDeclareBufferRetain(), { val } );
				else if ( mStructDefMap.find( tn ) != mStructDefMap.end() )
					mBuilder->CreateCall( getOrDeclareRcRetain(), { val } );
			}
		}
	}

	if ( registerOnLoop )
	{
		// Register on the global event loop: fires whenever the source fd is
		// readable. The handler runs when the program explicitly enters the loop
		// via `timer.run()` / `events.run()`.
		mBuilder->CreateCall( getOrDeclareEventOn(), { fdVal, cbFn, ctxAlloc } );
		mUsesConcurrency = true;
	}
	else
	{
		// Legacy fallback: no fd source — invoke the handler inline once.
		mBuilder->CreateCall( cbFn,
			{ ctxAlloc, llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 0 ) } );
	}
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
