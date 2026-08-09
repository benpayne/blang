#include "CodeGen.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "FormatString.h"

#include <iostream>

using namespace QLang;
using namespace std;

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

	// U6: use-after-move / move analysis was lifted into the semantic pass
	// (Sema.cpp), which runs before codegen in all build modes and reports located
	// diagnostics (and correctly clears moved state on reassignment). Codegen no
	// longer re-checks moves here — sema is authoritative.

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
		if ( funcDef->getName() == "to_json" )
		{
			return genToJsonCall( call );
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
					reportError( call, "@format function '" + funcDef->getName() +
						"': format string has " + to_string( phCount ) +
						" placeholder(s) but " + to_string( extraArgs ) +
						" extra argument(s) provided" );
					return nullptr;
				}
			}
			break;
		}
	}

	// Handle generic function instantiation. When the caller wrote no explicit
	// <...> list, infer the type arguments from the argument expressions
	// (identity(w) with a string w == identity<string>(w)); a generic call whose
	// arguments cannot bind every parameter is a LOUD error — previously it fell
	// through to "undefined function" without setting mHasError, so the call was
	// silently dropped and the target variable read uninitialized memory.
	if ( funcDef->isGeneric() && call->mTypeArgs.empty() )
	{
		if ( !inferCallTypeArgs( call, funcDef ) )
		{
			reportError( call, "cannot infer type arguments for generic function '" +
				funcDef->getName() + "' — call it with explicit type arguments, e.g. " +
				funcDef->getName() + "<int>(...)" );
			return nullptr;
		}

		// Constraint checking for INFERRED type arguments: Sema checks
		// explicit-arg calls (REQ-008), but inferred arguments only exist
		// after inference here. Structural: the bound struct must implement
		// every required method by name (the arity/shape check happened at
		// the protocol's impl site). Non-struct / unknown bindings are left
		// unchecked, mirroring Sema's stance.
		const auto &cgps = funcDef->getGenericParams();
		for ( size_t gi = 0; gi < cgps.size() && gi < call->mTypeArgs.size(); gi++ )
		{
			if ( cgps[gi].mConstraint.empty() )
				continue;
			auto pIt = mProtocolDefMap.find( cgps[gi].mConstraint );
			if ( pIt == mProtocolDefMap.end() )
				continue;
			auto sIt = mStructDefMap.find( ( (Type *)call->mTypeArgs[gi] )->getName() );
			if ( sIt == mStructDefMap.end() )
				continue;
			for ( auto &req : pIt->second->getRequiredMethods() )
			{
				if ( req == nullptr )
					continue;
				bool found = false;
				for ( auto &m : sIt->second->getMethods() )
					if ( m != nullptr && m->getName() == req->getName() )
					{
						found = true;
						break;
					}
				if ( !found )
				{
					reportError( call, "inferred type '" +
						( (Type *)call->mTypeArgs[gi] )->getName() +
						"' does not satisfy constraint '" + cgps[gi].mConstraint +
						"' on generic parameter '" + cgps[gi].mName +
						"' of '" + funcDef->getName() + "': missing method '" +
						req->getName() + "'" );
					return nullptr;
				}
			}
		}
	}

	if ( !call->mTypeArgs.empty() && funcDef->isGeneric() )
	{
		llvm::Function *genFunc = instantiateGenericFunction( funcDef, call->mTypeArgs );
		if ( genFunc == nullptr )
		{
			reportError( call, "failed to instantiate generic function '" +
				funcDef->getName() + "'" );
			return nullptr;
		}

		// Generate argument values
		std::vector<llvm::Value*> args;
		for ( size_t pi = 0; pi < call->mParams.size(); pi++ )
		{
			llvm::Value *argVal = genExpression( call->mParams[pi] );
			if ( argVal == nullptr )
				return nullptr;
			// A payload-carrying enum rvalue argument owns its payload with no
			// releasing owner — register it for scope-exit release (ledger #7).
			VariableDefinition *pd = ( pi < (size_t)funcDef->getNumberParams() )
				? funcDef->getParam( pi ) : nullptr;
			trackEnumArgTemp( call->mParams[pi], argVal,
				pd != nullptr ? pd->getVariableType() : nullptr );

			// Integer width coercion to the instantiated parameter type,
			// mirroring the non-generic call path — without it a `bool` param
			// (i1) receiving a bool literal (codegen'd i32) fails IR
			// verification (`pick<T>(T a, T b, bool first)`).
			if ( pi < genFunc->arg_size() )
			{
				llvm::Type *paramType = genFunc->getFunctionType()->getParamType( pi );
				if ( paramType->isIntegerTy() && argVal->getType()->isIntegerTy() &&
					 paramType->getIntegerBitWidth() < argVal->getType()->getIntegerBitWidth() )
					argVal = mBuilder->CreateTrunc( argVal, paramType, "garg.trunc" );
				else if ( paramType->isIntegerTy() && argVal->getType()->isIntegerTy() &&
						  paramType->getIntegerBitWidth() > argVal->getType()->getIntegerBitWidth() )
					argVal = mBuilder->CreateSExt( argVal, paramType, "garg.ext" );
			}
			args.push_back( argVal );
		}

		if ( genFunc->getReturnType()->isVoidTy() )
		{
			mBuilder->CreateCall( genFunc, args );
			return nullptr;
		}

		llvm::Value *callResult = mBuilder->CreateCall( genFunc, args, "calltmp" );
		// Track refcounted returns as temporaries, keyed on the CONCRETE return
		// type — the declared name is an erased param ("T") for calls like
		// pick_first<string>, so callReturnTypeName maps it through the type
		// arguments. (See the non-generic path below for the ownership model.)
		string genRetName = callReturnTypeName( call );
		if ( genRetName == "string" )
			trackTempString( callResult );
		else if ( genRetName == "Array" )
			trackTempArray( callResult );
		else if ( isUserStructType( genRetName ) )
			trackTempStruct( callResult );
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
		// Loud: without mHasError the compile exited 0 with the call silently
		// dropped, leaving the consumer reading uninitialized memory.
		reportError( call, "undefined function '" + funcDef->getName() + "'" +
			( !call->mMangledName.empty()
				? " (mangled: " + call->mMangledName + ")" : "" ) );
		return nullptr;
	}

	// Generate argument values with FFI conversion for extern functions
	std::vector<llvm::Value*> args;
	for ( size_t argIdx = 0; argIdx < call->mParams.size(); argIdx++ )
	{
		llvm::Value *argVal = genExpression( call->mParams[argIdx] );
		if ( argVal == nullptr )
			return nullptr;

		// A payload-carrying enum rvalue argument owns its payload with no
		// releasing owner — register it for scope-exit release (ledger #7).
		{
			VariableDefinition *pd = ( argIdx < (size_t)funcDef->getNumberParams() )
				? funcDef->getParam( argIdx ) : nullptr;
			trackEnumArgTemp( call->mParams[argIdx], argVal,
				pd != nullptr ? pd->getVariableType() : nullptr );
		}

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

		// Integer type coercion: widen or narrow to match parameter type
		if ( llvmFunc != nullptr && argIdx < llvmFunc->arg_size() )
		{
			llvm::Type *paramType = llvmFunc->getFunctionType()->getParamType( argIdx );
			if ( paramType->isIntegerTy() && argVal->getType()->isIntegerTy() &&
				 paramType->getIntegerBitWidth() > argVal->getType()->getIntegerBitWidth() )
			{
				argVal = mBuilder->CreateSExt( argVal, paramType, "arg.ext" );
			}
			else if ( paramType->isIntegerTy() && argVal->getType()->isIntegerTy() &&
					  paramType->getIntegerBitWidth() < argVal->getType()->getIntegerBitWidth() )
			{
				argVal = mBuilder->CreateTrunc( argVal, paramType, "arg.trunc" );
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

	// Track struct-returning function calls as temporaries so the fresh
	// refcount-1 heap struct (allocated by __blang_rc_alloc inside the callee,
	// then untracked at its `return`) is released at the end of the enclosing
	// statement — unless it is stored into a variable / struct field / enum
	// payload / returned, each of which untracks it (ownership transfers).
	// This mirrors the temp-tracking of struct literals (genStructLiteral /
	// genConstructExpression) so an rvalue struct from a call and one from a
	// literal have identical ARC lifetimes. Without this, an unstored struct
	// rvalue — e.g. `make_info(...).has_flag()` — leaks.
	if ( funcDef->getReturnType() != nullptr &&
		 isUserStructType( funcDef->getReturnType()->getName() ) )
	{
		trackTempStruct( callResult );
	}

	// Track Array<T>-returning calls as temporaries (see genReturnStatement for
	// the matching ownership contract): a function returns an owned array
	// reference, released at statement end unless it is stored / transferred.
	if ( funcDef->getReturnType() != nullptr &&
		 funcDef->getReturnType()->getName() == "Array" )
	{
		trackTempArray( callResult );
	}

	return callResult;
}

llvm::Value *CodeGen::genOperationsExpression( OperationsExpression *ops )
{
	const string &op = ops->mOperation;

	// Short-circuit logical operators (&&, ||): the RHS must be evaluated ONLY
	// when the LHS does not already determine the result — so its side effects
	// are skipped otherwise. This MUST run before the eager operand evaluation
	// below (which would force the RHS). Lowers to a branch + i1 phi.
	if ( op == "&&" || op == "||" )
	{
		bool isAnd = ( op == "&&" );
		llvm::Function *func = mBuilder->GetInsertBlock()->getParent();

		// Evaluate the LHS and coerce to i1 (!= 0 for int, != 0.0 for float).
		llvm::Value *lhs = genExpression( ops->mOp1 );
		if ( lhs == nullptr )
			return nullptr;
		llvm::Value *lBool = lhs->getType()->isFloatingPointTy()
			? mBuilder->CreateFCmpONE( lhs, llvm::ConstantFP::get( lhs->getType(), 0.0 ), "lbool" )
			: mBuilder->CreateICmpNE( lhs, llvm::ConstantInt::get( lhs->getType(), 0 ), "lbool" );

		llvm::BasicBlock *entryBB = mBuilder->GetInsertBlock();
		llvm::BasicBlock *rhsBB = llvm::BasicBlock::Create(
			*mContext, isAnd ? "land.rhs" : "lor.rhs", func );
		llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(
			*mContext, isAnd ? "land.end" : "lor.end", func );

		// &&: LHS true -> evaluate RHS, else short-circuit to false.
		// ||: LHS true -> short-circuit to true, else evaluate RHS.
		if ( isAnd )
			mBuilder->CreateCondBr( lBool, rhsBB, mergeBB );
		else
			mBuilder->CreateCondBr( lBool, mergeBB, rhsBB );

		// RHS block: evaluate the RHS (its side effects run only here) and
		// coerce to i1. Refcounted temporaries born during the RHS (e.g. the
		// string argument of `s.has("a")`) must be released INSIDE this block,
		// not deferred to statement scope: the statement-end release lands in the
		// merge block, which the RHS block does not dominate when the LHS
		// short-circuits — so the release would use a value defined only on the
		// taken edge ("instruction does not dominate all uses" → IR-verify ICE).
		// Snapshot the temp lists, then flush what the RHS added, in this block.
		size_t tmMarkStr = mTempStrings.size(), tmMarkLam = mTempLambdaCtxs.size();
		size_t tmMarkStruct = mTempStructs.size(), tmMarkArr = mTempArrays.size();
		mBuilder->SetInsertPoint( rhsBB );
		llvm::Value *rhs = genExpression( ops->mOp2 );
		if ( rhs == nullptr )
			return nullptr;
		llvm::Value *rBool = rhs->getType()->isFloatingPointTy()
			? mBuilder->CreateFCmpONE( rhs, llvm::ConstantFP::get( rhs->getType(), 0.0 ), "rbool" )
			: mBuilder->CreateICmpNE( rhs, llvm::ConstantInt::get( rhs->getType(), 0 ), "rbool" );
		// Release RHS-born temporaries here (the RHS result is an i1, never a
		// tracked temp), confining them to the RHS edge.
		auto flushTempsSince = [&]( std::vector<llvm::Value*> &v, size_t mark,
			llvm::FunctionCallee fn ) {
			for ( size_t i = mark; i < v.size(); ++i )
				mBuilder->CreateCall( fn, { v[i] } );
			v.resize( mark );
		};
		flushTempsSince( mTempStrings, tmMarkStr, getOrDeclareStringRelease() );
		flushTempsSince( mTempLambdaCtxs, tmMarkLam, getOrDeclareLambdaCtxRelease() );
		flushTempsSince( mTempStructs, tmMarkStruct, getOrDeclareRcRelease() );
		flushTempsSince( mTempArrays, tmMarkArr, getOrDeclareArrayRelease() );
		// Nested control flow in the RHS may have changed the current block, so
		// snapshot the real predecessor for the phi.
		llvm::BasicBlock *rhsEndBB = mBuilder->GetInsertBlock();
		mBuilder->CreateBr( mergeBB );

		// Merge: pick the short-circuit constant (false for &&, true for ||)
		// from the entry edge, or the computed RHS bool from the RHS edge.
		mBuilder->SetInsertPoint( mergeBB );
		llvm::Type *i1Ty = llvm::Type::getInt1Ty( *mContext );
		llvm::PHINode *phi = mBuilder->CreatePHI( i1Ty, 2, isAnd ? "landtmp" : "lortmp" );
		phi->addIncoming( llvm::ConstantInt::get( i1Ty, isAnd ? 0 : 1 ), entryBB );
		phi->addIncoming( rBool, rhsEndBB );
		return phi;
	}

	llvm::Value *left = genExpression( ops->mOp1 );
	llvm::Value *right = genExpression( ops->mOp2 );

	if ( left == nullptr || right == nullptr )
		return nullptr;

	// `byte` is unsigned (byte->int conversion already zero-extends, see
	// codegen_byte.b). Widen byte operands with ZExt (not SExt) and use a
	// logical right shift, so byte arithmetic/print stays unsigned (0-255).
	bool leftIsByte = isByteExpression( ops->mOp1 );
	bool rightIsByte = isByteExpression( ops->mOp2 );

	// Type promotion for mixed-width operands
	if ( left->getType() != right->getType() )
	{
		if ( left->getType()->isIntegerTy() && right->getType()->isIntegerTy() )
		{
			unsigned leftBits = left->getType()->getIntegerBitWidth();
			unsigned rightBits = right->getType()->getIntegerBitWidth();
			if ( leftBits < rightBits )
			{
				if ( leftBits == 1 || leftIsByte )
					left = mBuilder->CreateZExt( left, right->getType(), "bpromote" );
				else
					left = mBuilder->CreateSExt( left, right->getType(), "promote" );
			}
			else
			{
				if ( rightBits == 1 || rightIsByte )
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
	// String relational operators are LEXICOGRAPHIC via __blang_string_compare
	// (returns <0/0/>0), not a pointer compare. Without this, `<`/`>`/`<=`/`>=`
	// on strings fell through to an integer compare of the string POINTERS —
	// meaningless ordering (surfaced by U5's generic sort<string>).
	if ( isString && ( op == "<" || op == ">" || op == "<=" || op == ">=" ) )
	{
		llvm::Value *cmp = mBuilder->CreateCall(
			getOrDeclareStringCompare(), { left, right }, "strcmp" );
		llvm::Value *zero = llvm::ConstantInt::get( llvm::Type::getInt32Ty( *mContext ), 0 );
		if ( op == "<" )  return mBuilder->CreateICmpSLT( cmp, zero, "strlt" );
		if ( op == ">" )  return mBuilder->CreateICmpSGT( cmp, zero, "strgt" );
		if ( op == "<=" ) return mBuilder->CreateICmpSLE( cmp, zero, "strle" );
		return mBuilder->CreateICmpSGE( cmp, zero, "strge" );
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
	if ( op == ">>" ) return ( leftIsByte )
		? mBuilder->CreateLShr( left, right, "shrtmp" )
		: mBuilder->CreateAShr( left, right, "shrtmp" );

	// Comparisons (produce i1)
	if ( op == "==" ) return isFloat ? mBuilder->CreateFCmpOEQ( left, right, "eqtmp" ) : mBuilder->CreateICmpEQ( left, right, "eqtmp" );
	if ( op == "!=" ) return isFloat ? mBuilder->CreateFCmpONE( left, right, "netmp" ) : mBuilder->CreateICmpNE( left, right, "netmp" );
	if ( op == "<" )  return isFloat ? mBuilder->CreateFCmpOLT( left, right, "lttmp" ) : mBuilder->CreateICmpSLT( left, right, "lttmp" );
	if ( op == ">" )  return isFloat ? mBuilder->CreateFCmpOGT( left, right, "gttmp" ) : mBuilder->CreateICmpSGT( left, right, "gttmp" );
	if ( op == "<=" ) return isFloat ? mBuilder->CreateFCmpOLE( left, right, "letmp" ) : mBuilder->CreateICmpSLE( left, right, "letmp" );
	if ( op == ">=" ) return isFloat ? mBuilder->CreateFCmpOGE( left, right, "getmp" ) : mBuilder->CreateICmpSGE( left, right, "getmp" );

	// Logical operators &&/|| are handled at the top of this function with
	// short-circuit branching (they never reach here).

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
		reportError( assign, "cannot assign to shared variable '" +
			varDef->getName() + "' — shared values are immutable" );
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

		// If reassigning a struct variable, release the old value and untrack the new
		if ( varDef->getVariableType() != nullptr &&
			 isUserStructType( varDef->getVariableType()->getName() ) )
		{
			llvm::Value *oldVal = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "struct.old" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { oldVal } );
			untrackTempStruct( rhs );
		}

		// If reassigning an array variable, release the old value and untrack the
		// new (the variable now owns it; released at scope exit).
		if ( varDef->getVariableType() != nullptr &&
			 varDef->getVariableType()->getName() == "Array" )
		{
			llvm::Value *oldVal = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "arr.old" );
			mBuilder->CreateCall( getOrDeclareArrayRelease(), { oldVal } );
			untrackTempArray( rhs );
		}

		// If reassigning an enum variable whose payloads are refcounted (boxed
		// children, strings), release the OLD value's payloads before the
		// store — otherwise `d = Expr.add(d, ...)` leaks the previous tree.
		// The RHS was evaluated above, so a self-referencing RHS already
		// retained anything it copied from the old value.
		if ( varDef->getVariableType() != nullptr )
		{
			auto edIt = mEnumDefMap.find( varDef->getVariableType()->getName() );
			if ( edIt != mEnumDefMap.end() &&
				 enumHasRefcountedPayload( edIt->second, varDef->getVariableType() ) )
			{
				emitEnumPayloadRelease( alloca, edIt->second,
					varDef->getVariableType() );
			}
		}

		// Coerce integer RHS to the destination width before storing. Without this,
		// assigning a bool literal (`true`/`false` — codegen'd as i32) to an i1 bool
		// variable emits `store i32 into i1*`, a 4-byte write into a 1-byte stack slot
		// that corrupts adjacent locals (e.g. a loop counter → infinite loop). The
		// var-decl initializer path already coerces; the assignment path must too.
		llvm::Value *storeVal = rhs;
		llvm::Type *destTy = alloca->getAllocatedType();
		if ( rhs->getType()->isIntegerTy() && destTy->isIntegerTy() &&
			 rhs->getType() != destTy )
		{
			if ( destTy->isIntegerTy( 1 ) )
			{
				// Any nonzero value becomes true.
				storeVal = mBuilder->CreateICmpNE(
					rhs, llvm::ConstantInt::get( rhs->getType(), 0 ), "assign.tobool" );
			}
			else
			{
				unsigned srcBits = rhs->getType()->getIntegerBitWidth();
				unsigned dstBits = destTy->getIntegerBitWidth();
				storeVal = ( dstBits < srcBits )
					? mBuilder->CreateTrunc( rhs, destTy, "assign.trunc" )
					: mBuilder->CreateSExt( rhs, destTy, "assign.sext" );
			}
		}

		mBuilder->CreateStore( storeVal, alloca );

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
	{
		// Float/double negation must use fneg — CreateNeg emits an integer
		// `sub 0, x`, which is invalid on floating-point operands (surfaced by
		// U4's first float codegen test: `math.fabs(-2.5)` produced an illegal
		// `sub (double 0.0, double 2.5)` constexpr).
		if ( operand->getType()->isFloatingPointTy() )
			return mBuilder->CreateFNeg( operand, "negtmp" );
		return mBuilder->CreateNeg( operand, "negtmp" );
	}

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


// The struct a string-interpolation part denotes, or nullptr when the part is
// not a struct value. Used to route struct parts through Printable rather than
// handing a struct pointer to the string runtime as if it were a BlangString.
// The LLVM function implementing a struct's `to_string`. Prefers the emitted
// symbol, then falls back to the method's entry in mFunctionMap — the symbol may
// not exist yet when the CALLER is generated before the conformance impl block
// that defines it (method emission follows source order).
llvm::Function *CodeGen::lookupToStringFn( StructDefinition *sd )
{
	if ( sd == nullptr )
		return nullptr;
	if ( llvm::Function *fn = mModule->getFunction( sd->getName() + "_to_string" ) )
		return fn;
	for ( auto &msp : sd->mMethods )
	{
		FunctionDefinition *m = const_cast<FunctionDefinition*>(
			(const FunctionDefinition*)msp );
		if ( m == nullptr || m->getName() != "to_string" )
			continue;
		auto it = mFunctionMap.find( m );
		if ( it != mFunctionMap.end() )
			return it->second;

		// The method is declared on the struct but its LLVM function has not
		// been created yet: method bodies are generated in source order, so a
		// caller earlier in the file reaches here before the conformance impl
		// block that defines to_string. Forward-declare it; the definition lands
		// later in this same module.
		llvm::Type *ptrTy = llvm::PointerType::get( *mContext, 0 );
		llvm::FunctionType *ft = llvm::FunctionType::get( ptrTy, { ptrTy }, false );
		return llvm::Function::Create( ft, llvm::Function::ExternalLinkage,
			sd->getName() + "_to_string", mModule.get() );
	}
	return nullptr;
}

// Only a VARIABLE or a FIELD ACCESS can be an interpolation part today — the
// parser builds nothing else — and receiverStructDef handles both, including a
// `self` base. Anything else is not a struct value.
StructDefinition *CodeGen::structDefForInterpolationPart( Expression *part )
{
	if ( dynamic_cast<VariableExpression*>( part ) == nullptr &&
		 dynamic_cast<FieldAccessExpression*>( part ) == nullptr )
		return nullptr;
	return receiverStructDef( part );
}

// Does this struct render through Printable? Explicit `impl Printable` always
// counts; a structural `to_string` counts only for a type defined in THIS module
// — an imported type must state conformance through the .bmod record (D16), so a
// private to_string that vanishes under `pub` filtering does not silently qualify
// it. The single source of truth for both the print and interpolation paths.
bool CodeGen::structIsPrintable( StructDefinition *sd )
{
	if ( sd == nullptr )
		return false;
	for ( const auto &proto : sd->getConformedProtocols() )
		if ( proto == "Printable" )
			return true;
	if ( !sd->isFromInterface() )
	{
		for ( auto &m : sd->getMethods() )
			if ( m->getName() == "to_string" )
				return true;
	}
	return false;
}

// The self pointer for a struct receiver, taken from the value's ADDRESS. A
// loaded value is wrong for `shared`/`sync`, whose variable holds a heap pointer
// that genVariableExpression loads twice (yielding the struct's first 8 bytes as
// a pointer). A receiver with no addressable slot — a field access or a call
// result — has genExpression yield the heap pointer directly, which is correct.
llvm::Value *CodeGen::structSelfPointer( Expression *argExpr )
{
	llvm::AllocaInst *addr = getExpressionAddress( argExpr );
	if ( addr != nullptr && addr->getAllocatedType()->isPointerTy() )
		return mBuilder->CreateLoad(
			llvm::PointerType::get( *mContext, 0 ), addr, "self.ptr" );
	return genExpression( argExpr );
}

// Render a struct value through the Printable protocol. `selfPtr` must already
// be the struct's self pointer (see structSelfPointer for why that must come
// from the variable's ADDRESS and not from a loaded value).
llvm::Value *CodeGen::genPrintableToString( StructDefinition *sd, Expression *node,
	llvm::Value *selfPtr )
{
	if ( !structIsPrintable( sd ) )
	{
		reportError( node, "type '" + sd->getName() +
			"' is not printable — implement the Printable protocol" );
		return nullptr;
	}

	llvm::Function *toStrFn = lookupToStringFn( sd );
	if ( toStrFn == nullptr )
	{
		reportError( node, "'" + sd->getName() + "_to_string' function not found" );
		return nullptr;
	}
	return mBuilder->CreateCall( toStrFn, { selfPtr }, "interp.structstr" );
}

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
					// byte is unsigned: zero-extend so 0-255 prints unsigned.
					llvm::Value *ext = isByteExpression( part )
						? mBuilder->CreateZExt( val,
							llvm::Type::getInt64Ty( *mContext ), "ext64" )
						: mBuilder->CreateSExt( val,
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
				// A pointer here is EITHER a BlangString or a struct value —
				// struct values are refcounted heap pointers too. Passing a
				// struct straight through handed a non-BlangString to
				// __blang_string_concat_many, which read a string header out of
				// struct memory and rendered empty. Dispatch a struct through
				// Printable instead, exactly as the `{}` placeholder path does.
				StructDefinition *psd = structDefForInterpolationPart( part );
				if ( psd != nullptr )
				{
					// The self pointer must come from the ADDRESS, not `val`:
					// `val` is genExpression's result, which double-loads a
					// shared/sync struct variable and would pass its first 8
					// bytes as a pointer (known-issues KI-20). This mirrors the
					// direct-print path in genPrintCall.
					llvm::Value *selfPtr = structSelfPointer( part );
					if ( selfPtr == nullptr )
						return nullptr;
					llvm::Value *strPart = genPrintableToString( psd, part, selfPtr );
					if ( strPart == nullptr )
						return nullptr;   // reportError already fired
					// to_string returns a FRESH string (refcount 1) that nothing
					// else owns — exactly like the intstr/fltstr/boolstr parts
					// above — so it must be tracked or the interpolation leaks it.
					// A BlangString part (the else branch) is BORROWED from the
					// variable that holds it and must NOT be tracked.
					trackTempString( strPart );
					parts.push_back( strPart );
				}
				else
				{
					// Already a BlangString pointer — use directly (borrowed)
					parts.push_back( val );
				}
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
			reportError( call, "print/println format string must be a string literal, not a string interpolation with variables" );
			return;
		}
	}
	else
	{
		reportError( call, "print/println format string must be a string literal" );
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
						reportError( call, "empty format specifier after ':'" );
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
									reportError( call, "invalid precision in format specifier: '" + spec + "'" );
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
						reportError( call, "unknown format specifier: '" + spec + "'" );
						return;
					}
				}

				if ( i >= fmtStr.size() || fmtStr[i] != '}' )
				{
					reportError( call, "unterminated format placeholder" );
					return;
				}
				i++;
				parsed.placeholders.push_back( ph );
			}
			else if ( c == '}' )
			{
				reportError( call, "unexpected '}' in format string (use '}}' for literal '}')" );
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
		reportError( call, "format string has " + to_string( numPlaceholders ) +
			" placeholder(s) but " + to_string( numArgs ) + " argument(s) provided" );
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
				reportError( call, std::string( "format specifier ':" ) + ph.type +
					"' requires integer type, got float/double" );
				return;
			}
		}
		else if ( ph.type == 'f' || ph.type == 'e' )
		{
			if ( isIntArg )
			{
				reportError( call, std::string( "format specifier ':" ) + ph.type +
					"' requires float/double type, got integer" );
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

			// Decide struct-ness from the AST BEFORE generating anything. A
			// struct receiver needs its self POINTER, which is not what
			// genExpression yields for every ownership qualifier (see below), so
			// generating first and reinterpreting afterwards is what produced a
			// segfault for `shared`/`sync` receivers.
			//
			// receiverStructDef, not a bare mStructDefMap lookup: the implicit
			// `self` parameter's declared type name is the literal "self", so a
			// name lookup missed and `self` fell through to the generic path,
			// which handed a raw struct pointer to __blang_string_concat_many AS
			// IF it were a BlangString (known-issues KI-10). It also resolves a
			// FIELD-access receiver (`println("{}", h.inner)`), which the earlier
			// VariableExpression-only test dropped onto that same raw-pointer path
			// (known-issues KI-21).
			bool isStructArg = false;
			std::string structTypeName;
			if ( dynamic_cast<VariableExpression*>( argExpr ) != nullptr ||
				 dynamic_cast<FieldAccessExpression*>( argExpr ) != nullptr )
			{
				if ( StructDefinition *rsd = receiverStructDef( argExpr ) )
				{
					structTypeName = rsd->getName();
					isStructArg = true;
				}
			}

			llvm::Value *val = nullptr;
			if ( !isStructArg )
			{
				val = genExpression( argExpr );
				if ( val == nullptr )
					continue;
			}

			if ( isStructArg )
			{
				// Struct type → call StructName_to_string. structIsPrintable is
				// the single Printable test shared with the interpolation path:
				// an imported type must state conformance through its .bmod record
				// (design record D16) — its private to_string does not silently
				// count; a same-module type may conform structurally via a
				// to_string method.
				StructDefinition *sd = mStructDefMap[structTypeName];

				if ( !structIsPrintable( sd ) )
				{
					if ( sd != nullptr && sd->isFromInterface() )
						reportError( call, "imported type '" + structTypeName +
							"' is not printable — its interface declares no "
							"'impl Printable for " + structTypeName + "'" );
					else
						reportError( call, "type '" + structTypeName +
							"' is not printable — implement the Printable protocol" );
					return;
				}
				llvm::Function *toStrFn = lookupToStringFn( sd );
				if ( toStrFn )
				{
					// The self POINTER must come from the receiver's ADDRESS, not
					// a loaded value — structSelfPointer carries the reasoning
					// (shared/sync double-load; field-access/call fall back to the
					// heap pointer directly).
					llvm::Value *selfPtr = structSelfPointer( argExpr );
					if ( selfPtr == nullptr )
						continue;

					llvm::Value *strPart = mBuilder->CreateCall(
						toStrFn, { selfPtr }, "print.structstr" );
					trackTempString( strPart );
					parts.push_back( strPart );
				}
				else
				{
					reportError( call, "'" + structTypeName +
						"_to_string' function not found" );
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
					// byte is unsigned: zero-extend so 0-255 formats unsigned.
					llvm::Value *ext = isByteExpression( argExpr )
						? mBuilder->CreateZExt( val,
							llvm::Type::getInt64Ty( *mContext ), "ext64" )
						: mBuilder->CreateSExt( val,
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
					// byte is unsigned: zero-extend so 0-255 prints unsigned.
					llvm::Value *ext = isByteExpression( argExpr )
						? mBuilder->CreateZExt( val,
							llvm::Type::getInt64Ty( *mContext ), "ext64" )
						: mBuilder->CreateSExt( val,
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
