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

		// If reassigning a struct variable, release the old value and untrack the new
		if ( varDef->getVariableType() != nullptr &&
			 isUserStructType( varDef->getVariableType()->getName() ) )
		{
			llvm::Value *oldVal = mBuilder->CreateLoad(
				llvm::PointerType::get( *mContext, 0 ), alloca, "struct.old" );
			mBuilder->CreateCall( getOrDeclareRcRelease(), { oldVal } );
			untrackTempStruct( rhs );
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
