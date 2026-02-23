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

	// Check for known enum types — represented as i32 tag
	auto enumIt = mEnumDefMap.find( name );
	if ( enumIt != mEnumDefMap.end() )
		return llvm::Type::getInt32Ty( *mContext );

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

bool CodeGen::generate( Module *mod )
{
	// Store the module scope for type resolution
	mScope = mod->mScope;

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

	// Generate top-level functions
	for ( auto &func : mod->mFunctionList )
	{
		// Skip generic functions — they're templates, not concrete code
		if ( func->isGeneric() )
			continue;

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

	llvm::FunctionType *ft = llvm::FunctionType::get( retType, paramTypes, func->isVariadic() );
	llvm::Function *llvmFunc = llvm::Function::Create(
		ft, llvm::Function::ExternalLinkage, func->getName(), mModule.get() );

	// Store the mapping
	mFunctionMap[func] = llvmFunc;

	// Extern functions are declarations only — no body
	if ( func->isExtern() )
		return llvmFunc;

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
			mBuilder->CreateRet( llvm::Constant::getNullValue( retType ) );
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
		{
			// Don't generate code after a terminator (unreachable code)
			if ( mBuilder->GetInsertBlock()->getTerminator() != nullptr )
				break;
			genStatement( stmt );
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
			{
				// Cast if types don't match (e.g., int literal for struct return type)
				if ( initVal->getType() != llvmType )
				{
					if ( llvmType->isIntegerTy() && initVal->getType()->isIntegerTy() )
						initVal = mBuilder->CreateIntCast( initVal, llvmType, true, "icast" );
					else if ( llvmType->isFloatTy() && initVal->getType()->isDoubleTy() )
						initVal = mBuilder->CreateFPTrunc( initVal, llvmType, "fptrunc" );
					else if ( llvmType->isDoubleTy() && initVal->getType()->isFloatTy() )
						initVal = mBuilder->CreateFPExt( initVal, llvmType, "fpext" );
					else
						initVal = nullptr; // type mismatch we can't resolve, skip store
				}
				if ( initVal != nullptr )
					mBuilder->CreateStore( initVal, alloca );
			}
		}
	}
}

void CodeGen::genReturnStatement( ReturnStatement *ret )
{
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
	else if ( auto *unary = dynamic_cast<UnaryExpression*>( expr ) )
		return genUnaryExpression( unary );
	else if ( auto *field = dynamic_cast<FieldAccessExpression*>( expr ) )
		return genFieldAccess( field );
	else if ( auto *method = dynamic_cast<MethodCallExpression*>( expr ) )
		return genMethodCall( method );
	else if ( auto *slit = dynamic_cast<StructLiteralExpression*>( expr ) )
		return genStructLiteral( slit );
	else if ( auto *matchExpr = dynamic_cast<MatchExpression*>( expr ) )
		return genMatchExpression( matchExpr );
	else if ( auto *tryExpr = dynamic_cast<TryExpression*>( expr ) )
		return genTryExpression( tryExpr );
	else if ( auto *arrLit = dynamic_cast<ArrayLiteralExpression*>( expr ) )
		return genArrayLiteral( arrLit );
	else if ( auto *idxExpr = dynamic_cast<IndexExpression*>( expr ) )
		return genIndexExpression( idxExpr );

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

// ---- Match codegen (Task 53) ----

llvm::Value *CodeGen::genMatchExpression( MatchExpression *expr )
{
	llvm::Value *subject = genExpression( expr->mSubject );
	if ( subject == nullptr )
		return nullptr;

	llvm::Function *func = mBuilder->GetInsertBlock()->getParent();
	llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create( *mContext, "matchend", func );

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

	// Build switch for integer subjects
	if ( subject->getType()->isIntegerTy() )
	{
		// Count actual cases for the switch
		int numCases = 0;
		for ( size_t i = 0; i < expr->mArms.size(); i++ )
		{
			if ( !expr->mArms[i].mIsWildcard )
				numCases++;
		}

		llvm::SwitchInst *switchInst = mBuilder->CreateSwitch(
			subject, defaultBB, numCases );

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
				if ( !found )
				{
					// Use arm index as fallback
					patternVal = static_cast<int64_t>( i );
				}
			}

			switchInst->addCase(
				llvm::ConstantInt::get(
					llvm::cast<llvm::IntegerType>( subject->getType() ),
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

		// If the arm has a binding, create a variable for it
		if ( !expr->mArms[i].mBindingName.empty() )
		{
			// Create an alloca for the bound variable and store the subject value
			llvm::Type *bindType = subject->getType();
			llvm::AllocaInst *bindAlloca = mBuilder->CreateAlloca(
				bindType, nullptr, expr->mArms[i].mBindingName );
			mBuilder->CreateStore( subject, bindAlloca );

			// Register in variable map — look up the VariableDefinition from the
			// arm body's scope. For now, we create a temporary mapping by scanning
			// the block's scope for the binding name.
			if ( expr->mArms[i].mBody != nullptr &&
				 expr->mArms[i].mBody->mScope != nullptr )
			{
				Symbol *bindSym = expr->mArms[i].mBody->mScope->findSymbol(
					expr->mArms[i].mBindingName );
				if ( auto *bindVar = dynamic_cast<VariableDefinition*>( bindSym ) )
					mVariableMap[bindVar] = bindAlloca;
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
