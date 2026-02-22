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

	cerr << "CodeGen: unknown type '" << name << "'" << endl;
	return llvm::Type::getInt32Ty( *mContext );
}

bool CodeGen::generate( Module *mod )
{
	for ( auto &func : mod->mFunctionList )
	{
		if ( genFunction( func ) == nullptr )
			return false;
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

	llvm::FunctionType *ft = llvm::FunctionType::get( retType, paramTypes, false );
	llvm::Function *llvmFunc = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, func->getName(), mModule.get() );

	// Store the mapping
	mFunctionMap[func] = llvmFunc;

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

	// Generate the function body
	if ( func->mFuncBody != nullptr )
	{
		genBlock( func->mFuncBody );
	}

	// If the function is void and the last block has no terminator, add ret void
	llvm::BasicBlock *currentBB = mBuilder->GetInsertBlock();
	if ( currentBB->getTerminator() == nullptr )
	{
		if ( retType->isVoidTy() )
		{
			mBuilder->CreateRetVoid();
		}
		else
		{
			// Implicit return 0 for non-void functions without explicit return
			mBuilder->CreateRet( llvm::ConstantInt::get( retType, 0 ) );
		}
	}

	// Clear variable map for this function scope
	// (parameters and locals are no longer valid)
	mVariableMap.clear();

	return llvmFunc;
}

void CodeGen::genBlock( Block *block )
{
	for ( auto &stmt : block->mStatementList )
	{
		if ( stmt != nullptr )
			genStatement( stmt );
	}
}

void CodeGen::genStatement( Statement *stmt )
{
	if ( stmt == nullptr )
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

		llvm::AllocaInst *alloca = mBuilder->CreateAlloca(
			llvmType, nullptr, varDef->getName() );
		mVariableMap[varDef] = alloca;

		// If there's an initializer, generate it and store
		if ( data.mInitialValue != nullptr )
		{
			llvm::Value *initVal = genExpression( data.mInitialValue );
			if ( initVal != nullptr )
				mBuilder->CreateStore( initVal, alloca );
		}
	}
}

void CodeGen::genReturnStatement( ReturnStatement *ret )
{
	if ( ret->mExpression != nullptr )
	{
		llvm::Value *retVal = genExpression( ret->mExpression );
		if ( retVal != nullptr )
			mBuilder->CreateRet( retVal );
		else
			mBuilder->CreateRetVoid();
	}
	else
	{
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

	// Body block
	mBuilder->SetInsertPoint( bodyBB );
	if ( whileStmt->mLoopStatement != nullptr )
		genStatement( whileStmt->mLoopStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( condBB );

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

	// Body
	mBuilder->SetInsertPoint( bodyBB );
	if ( forStmt->mStatement != nullptr )
		genStatement( forStmt->mStatement );
	if ( mBuilder->GetInsertBlock()->getTerminator() == nullptr )
		mBuilder->CreateBr( iterBB );

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

	const string &op = ops->mOperation;

	// Integer arithmetic
	if ( op == "+" )  return mBuilder->CreateAdd( left, right, "addtmp" );
	if ( op == "-" )  return mBuilder->CreateSub( left, right, "subtmp" );
	if ( op == "*" )  return mBuilder->CreateMul( left, right, "multmp" );
	if ( op == "/" )  return mBuilder->CreateSDiv( left, right, "divtmp" );
	if ( op == "%" )  return mBuilder->CreateSRem( left, right, "modtmp" );

	// Bitwise
	if ( op == "&" )  return mBuilder->CreateAnd( left, right, "andtmp" );
	if ( op == "|" )  return mBuilder->CreateOr( left, right, "ortmp" );
	if ( op == "^" )  return mBuilder->CreateXor( left, right, "xortmp" );
	if ( op == "<<" ) return mBuilder->CreateShl( left, right, "shltmp" );
	if ( op == ">>" ) return mBuilder->CreateAShr( left, right, "shrtmp" );

	// Comparisons (produce i1)
	if ( op == "==" ) return mBuilder->CreateICmpEQ( left, right, "eqtmp" );
	if ( op == "!=" ) return mBuilder->CreateICmpNE( left, right, "netmp" );
	if ( op == "<" )  return mBuilder->CreateICmpSLT( left, right, "lttmp" );
	if ( op == ">" )  return mBuilder->CreateICmpSGT( left, right, "gttmp" );
	if ( op == "<=" ) return mBuilder->CreateICmpSLE( left, right, "letmp" );
	if ( op == ">=" ) return mBuilder->CreateICmpSGE( left, right, "getmp" );

	// Logical (treat operands as booleans via != 0)
	if ( op == "&&" )
	{
		llvm::Value *lBool = mBuilder->CreateICmpNE(
			left, llvm::ConstantInt::get( left->getType(), 0 ), "lbool" );
		llvm::Value *rBool = mBuilder->CreateICmpNE(
			right, llvm::ConstantInt::get( right->getType(), 0 ), "rbool" );
		return mBuilder->CreateAnd( lBool, rBool, "landtmp" );
	}
	if ( op == "||" )
	{
		llvm::Value *lBool = mBuilder->CreateICmpNE(
			left, llvm::ConstantInt::get( left->getType(), 0 ), "lbool" );
		llvm::Value *rBool = mBuilder->CreateICmpNE(
			right, llvm::ConstantInt::get( right->getType(), 0 ), "rbool" );
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
	llvm::Value *rhs = genExpression( assign->mValue );
	if ( rhs == nullptr )
		return nullptr;

	const string &op = assign->mOperation;

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
