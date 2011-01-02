
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/LLVMContext.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace std;

LLVMContext gContext;

#include "Symbol.h"
#include "Scope.h"
#include "parse_helpers.h"
#include "assert.h"

using namespace BLang;

Scope *gGlobalScope = NULL;
Scope *gCurrentScope = NULL;
Symbol *gCurrentSymbol = NULL;
Symbol *gCurrentType = NULL;
Module *gMod = NULL;
vector<Symbol *>	gFunctionParams;
IRBuilder<> *gBuilder = NULL;

extern int lineno;

void init( const char *filename )
{
	gGlobalScope = new Scope( SCOPE_GLOBAL );
	Symbol *_bool = Symbol::CreateType( Type::getInt1Ty( gContext ), "bool" );
	Symbol *_char = Symbol::CreateType( Type::getInt8Ty( gContext ), "char" );
	Symbol *_short = Symbol::CreateType( Type::getInt16Ty( gContext ), "short" );
	Symbol *_int = Symbol::CreateType( Type::getInt32Ty( gContext ), "int" );
	Symbol *_long = Symbol::CreateType( Type::getInt32Ty( gContext ), "long" );
	Symbol *_float = Symbol::CreateType( Type::getFloatTy( gContext ), "float" );
	Symbol *_double = Symbol::CreateType( Type::getDoubleTy( gContext ), "double" );
	Symbol *_string = Symbol::CreateType( PointerType::get( Type::getInt8Ty( gContext ), 0 ), "string" );
	Symbol *_void = Symbol::CreateType( Type::getVoidTy( gContext ), "void" );
	
	gGlobalScope->addType( _bool );
	gGlobalScope->addType( _char );
	gGlobalScope->addType( _short );
	gGlobalScope->addType( _int );
	gGlobalScope->addType( _long );
	gGlobalScope->addType( _float );
	gGlobalScope->addType( _double );
	gGlobalScope->addType( _string );
	gGlobalScope->addType( _void );
	gCurrentScope = gGlobalScope;

	gMod = new Module( filename, gContext );
	
	vector<const Type *> params;
	params.push_back( PointerType::get( Type::getInt8Ty( gContext ), 0 ) );	
	FunctionType *ft = FunctionType::get( Type::getInt32Ty( gContext ), params, true );
	gMod->getOrInsertFunction( "printf", ft );
}

void destroy()
{
	verifyModule(*gMod, PrintMessageAction);
	
	PassManager PM;
	PM.add(createPrintModulePass(&outs()));
	PM.run(*gMod);
	
	delete gMod;	
}

Symbol *lookupSymbol( const char *name )
{
	Symbol *s = gCurrentScope->findSymbol( name );
	
	if ( s == NULL )
	{
		fprintf( stderr, "Error: Failed to find symbol %s at line %d\n", name, lineno );
		exit( -1 );
	}
	
	return s;
}

Symbol *lookupType( const char *name )
{
	Symbol *s = gCurrentScope->findType( name );

	if ( s == NULL )
	{
		fprintf( stderr, "Error: Failed to find type %s at line %d\n", name, lineno );
		exit( -1 );
	}
	
	return s;
}

void getSymbol( const char *name )
{
	gCurrentSymbol = lookupSymbol( name );
}

void addSymbol( const char *name )
{
	if ( gCurrentType == NULL )
	{
		fprintf( stderr, "Internal Error: no type found for symbol %s at line %d\n", name, lineno );
		exit( -1 );
	}
	
	gCurrentSymbol = Symbol::CreateSymbol( gCurrentType->getType(), name );
	gCurrentScope->addSymbol( gCurrentSymbol );
}

llvm::Value *declareVariable( const char *name )
{
	printf( "declare variable %s\n", name );
	addSymbol( name );
	Value *v = gBuilder->CreateAlloca( gCurrentType->getType(), 0, name );
	gCurrentSymbol->setValue( v );
	return v;
}

void getType( const char *name )
{
	gCurrentType = lookupType( name );
}

void addType( const char *name )
{

}

void pushScope( int type, const char *name )
{
	printf( "Push Scope %d %s\n", type, name ); 
	Scope *s = new Scope( type );
	s->setParentScope( gCurrentScope );
	gCurrentScope = s;
}

void popScope()
{
	printf( "Pop Scope\n" );
	Scope *s = gCurrentScope;
	gCurrentScope = s->getParentScope();
	delete s;
}

void startFunction( const char *ret_type, const char *name )
{
	pushScope( SCOPE_FUNCTION, name );
	
	Symbol *s = lookupType( ret_type );
	
	if ( s != NULL )
	{
		gFunctionParams.push_back( Symbol::CreateSymbol( s->getType(), name ) );
	}
	
	printf( "Function %s %s()\n", ret_type, name );
}

void addFunctionParam( const char *type, const char *name )
{
	getType( type );
	addSymbol( name );
	
	printf( "Function param %s %s\n", type, name );

	gFunctionParams.push_back( gCurrentSymbol );
}

void endFunctionDef()
{
	vector<const Type *> params;
	printf( "function %s\n", gFunctionParams[ 0 ]->getName().c_str() );
	for ( unsigned i = 1; i < gFunctionParams.size(); i++ )
	{
		params.push_back( gFunctionParams[ i ]->getType() );
		printf( "\tparam %s\n", gFunctionParams[ i ]->getName().c_str() );
	}
		
	FunctionType *ft = FunctionType::get( gFunctionParams[ 0 ]->getType(), params, false );
	
	Constant* c = gMod->getOrInsertFunction( gFunctionParams[ 0 ]->getName().c_str(), ft );
	
	Function* func = cast<Function>(c);
  
	Function::arg_iterator args = func->arg_begin();

	for ( unsigned i = 1; i < gFunctionParams.size(); i++ )
	{
		Value* x = args++;
		x->setName( gFunctionParams[ i ]->getName().c_str() );
		gFunctionParams[ i ]->setValue( x );
	}
	
	gCurrentScope->setFunction( func );
	gCurrentScope->createBasicBlock();

	gBuilder = new IRBuilder<>( gCurrentScope->getBasicBlock() );	
}

void endFunction()
{
	printf( "End of function\n" );
	
	if ( gFunctionParams[ 0 ]->getType() == Type::getVoidTy( gContext ) )
		gBuilder->CreateRetVoid();

	delete gBuilder;
	gBuilder = NULL;

	delete gFunctionParams[ 0 ];
	gFunctionParams.clear();	
}

std::vector<Value*> gArgs;

void startFunctionCall()
{
	gArgs.clear();
}

void addFunctionCallParam( llvm::Value *v )
{
	gArgs.push_back( v );
}

llvm::Value *endFunctionCall( const char *name )
{
	Function *func = gMod->getFunction( name );
	
	if ( func == NULL )
	{
		printf( "Error: failed to find function %s\n", name );
		exit( 1 );
	}
	
	Value* recur_1 = gBuilder->CreateCall( func, gArgs.begin(), gArgs.end() );

	return recur_1;
}

llvm::Value *getVariableValue( const char *name )
{
	Symbol *s = lookupSymbol( name );
	return s->getValue();
}

llvm::Value *getArrayLookupValue( llvm::Value *array, llvm::Value *index )
{
	return array;	
}

llvm::Value *getConstantNumberValue( ConstData *data )
{
	const Type *t = NULL;
	
	printf ( "Constant Number %d(%d), width %d, signed %d\n", data->value, data->uvalue, data->width, data->_signed );
	
	switch( data->width )
	{
		case 32:
			t = Type::getInt32Ty( gContext );
			break;
		case 64:
			t = Type::getInt64Ty( gContext );
			break;
		case 8:
			t = Type::getInt8Ty( gContext );
			break;
		default:
			t = Type::getInt32Ty( gContext );
			break;
	}
	
	if ( data->_signed )
		return ConstantInt::get( t, data->value, false );
	else
		return ConstantInt::get( t, data->uvalue, false );		
}

llvm::Value *getConstantStringValue( const char *str )
{
	std::vector<Constant *> constStr;
	int i = 0;

	if ( str[ 0 ] == '\"' )
		str += 1;
		
	printf( "Creating string contant \"%s\"\n", str );

	// TODO handle escape characters	
	for( ; str[ i ] != '\0' && str[ i ] != '\"'; i++ )
	{
		constStr.push_back( ConstantInt::get( Type::getInt8Ty( gContext ), str[ i ] ) );
	}
	
	return ConstantArray::get( ArrayType::get( Type::getInt8Ty( gContext ), i + 1 ), constStr );
}

llvm::Value *getBinaryExpression( const char *op, llvm::Value *l, llvm::Value *r )
{
	Value *v = NULL;
	
	printf( "Binary Op \"%s\" l type %s, r type %s\n", op, 
		l->getType()->getDescription().c_str(), 
		r->getType()->getDescription().c_str() );
	
	if ( l->getType()->getTypeID() == Type::PointerTyID )
	{
		l = gBuilder->CreateLoad( l );
	}

	if ( r->getType()->getTypeID() == Type::PointerTyID )
	{
		r = gBuilder->CreateLoad( r );
	}
	
	assert( l->getType()->getTypeID() == Type::IntegerTyID );
	assert( r->getType()->getTypeID() == Type::IntegerTyID );

	printf( "zero extend numbers l->%d r->%d\n", 
		l->getType()->getPrimitiveSizeInBits(),
		r->getType()->getPrimitiveSizeInBits());			
	
	if ( l->getType()->getPrimitiveSizeInBits() != r->getType()->getPrimitiveSizeInBits() )
	{
		if ( l->getType()->getPrimitiveSizeInBits() > r->getType()->getPrimitiveSizeInBits() )
			r = gBuilder->CreateZExt( r, l->getType() );
		else
			l = gBuilder->CreateZExt( l, r->getType() );
	}
	
	if ( r == NULL || l == NULL )
		printf( "Failed to get one operand l=%p r=%p", l, r );
		
	switch( op[ 0 ] )
	{				
		case '+':
			v = gBuilder->CreateAdd( l, r );
			break;
			
		case '-':
			v = gBuilder->CreateSub( l, r );
			break;
			
		case '*':
			v = gBuilder->CreateMul( l, r );
			break;
			
		case '%':
			v = gBuilder->CreateURem( l, r );
			break;
			
		case '/':
			v = gBuilder->CreateUDiv( l, r );
			break;
		
		case '<':
			if ( op[ 1 ] == '<' )
			{
				v = gBuilder->CreateShl( l, r );
			}
			else if ( op[ 1 ] == '=' )
			{
				v = gBuilder->CreateICmpULE( l, r );
			}
			else if ( op[ 1 ] == '\0' )
			{
				v = gBuilder->CreateICmpULT( l, r );
			}
			break;

		case '>':
			if ( op[ 1 ] == '>' )
			{
				v = gBuilder->CreateAShr( l, r );
			}
			else if ( op[ 1 ] == '=' )
			{
				v = gBuilder->CreateICmpUGE( l, r );
			}
			else if ( op[ 1 ] == '\0' )
			{
				v = gBuilder->CreateICmpUGT( l, r );
			}
			break;

		case '=':
			if ( op[ 1 ] == '=' )
			{
				v = gBuilder->CreateICmpEQ( l, r );
			}
			break;

		case '!':
			if ( op[ 1 ] == '=' )
			{
				v = gBuilder->CreateICmpNE( l, r );
			}
			break;
			
		default:
			break;
	}
	
	if ( v == NULL )
		printf( "Failed to build operation!" );

	return v;
}

llvm::Value *getAssignExpression( const char *op, llvm::Value *l, llvm::Value *r )
{
	Value *v = NULL;
	
	printf( "Assign Op \"%s\" l type %s, r type %s\n", op, 
		l->getType()->getDescription().c_str(), 
		r->getType()->getDescription().c_str() );
	
	if ( l->getType()->getTypeID() != Type::PointerTyID )
	{
		printf( "failed to get pointer type\n" );
	}

	if ( r->getType()->getTypeID() == Type::PointerTyID )
	{
		r = gBuilder->CreateLoad( r );
	}

	PointerType *lPrt = (PointerType*)l->getType();
	const IntegerType *lType = (IntegerType*)lPrt->getElementType();
	
	if ( ((IntegerType*)r->getType())->getBitWidth() != 
			lType->getBitWidth() )
	{
		if ( ((IntegerType*)r->getType())->getBitWidth() > 
				lType->getBitWidth() )
			r = gBuilder->CreateTrunc( r, lType );
		else
			r = gBuilder->CreateZExt( r, lType );
	}
	
	switch( op[ 0 ] )
	{
		case '=':
			v = gBuilder->CreateStore( r, l );
								
		default:
			break;
	}
	
	return v;
}

void addReturn( llvm::Value *val )
{
	if ( val == NULL )
		gBuilder->CreateRetVoid();
	else
		gBuilder->CreateRet( val );		
}

static llvm::Value *gIfValue;
static llvm::BasicBlock *gIfBlock;

void startIf( llvm::Value *expression )
{
	printf( "Starting if with %p\n", expression );
	gIfValue = expression;
	pushScope( SCOPE_IF, NULL );
	gIfBlock = gCurrentScope->getBasicBlock();
}

void endIf()
{
	printf( "Ending if\n" );
	popScope();
}

void startElse( llvm::Value *expression )
{
	printf( "Starting else with %p\n", expression );
	popScope();
	pushScope( SCOPE_IF, NULL );
	gBuilder->CreateCondBr( gIfValue, gIfBlock, gCurrentScope->getBasicBlock() );
}


