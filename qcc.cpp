#include <assert.h>

#include <iostream>
#include <sstream>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

Scope *gScope;

string CompileError::getMessage() const
{
	stringstream s;
	s << "Compiler Error in " << mFilename << ":" << mLineno << endl;
	s << mMessage << " at line: " << mLexer.getLineNumber();
	return s.str();
}

Module *Module::Parse( Lexer &l, Scope *s )
{
	Module *mod = new Module();
	SmartPtr<FunctionDefinition> def;
	try { 
		while( !l.isEOF() )
		{
			def = FunctionDefinition::Parse( l, s );
			mod->mFunctionList.push_back( def );
			cout << *def << endl;
		}
	} catch( CompileError &err ) {
		cerr << err.getMessage() << endl;
		return NULL;
	}				
	
	return mod;
}

WhileStatement *WhileStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_WHILE )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	WhileStatement *statement = new WhileStatement;
	
	statement->mLoopExpression = Expression::Parse( l, scope, ')' );
	
	if ( statement->mLoopExpression == NULL )
	{
		COMPILE_ERROR( l, "Expected Expression in while statement" );
	}
	
	Scope *loop_scope = new Scope( Scope::kScope_Loop );
	loop_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mLoopStatement = Block::Parse( l, loop_scope );
	else
		statement->mLoopStatement = Statement::Parse( l, loop_scope );

	return statement;
}

IfStatement *IfStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IF )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	IfStatement *statement = new IfStatement;
	
	statement->mIfExpression = Expression::Parse( l, scope, ')' );
	
	if ( statement->mIfExpression == NULL )
	{
		COMPILE_ERROR( l, "Expected Expression in while statement" );
	}
	
	Scope *if_scope = new Scope( Scope::kScope_IfElse );
	if_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mStatement = Block::Parse( l, if_scope );
	else
		statement->mStatement = Statement::Parse( l, if_scope );
	
	if ( l.peekSymbol() == Lexer::KEYWORD_ELSE )
	{
		Scope *else_scope = new Scope( Scope::kScope_IfElse );
		else_scope->setParent( scope );
		
		sym = l.getSymbol();
		if ( l.peekSymbol() == '{' )
			statement->mElseStatement = Block::Parse( l, else_scope );
		else
			statement->mElseStatement = Statement::Parse( l, else_scope );
	}

	return statement;
}

ForStatement *ForStatement::Parse( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IF )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		COMPILE_ERROR( l, "Expected \'(\'" );
	}

	ForStatement *statement = new ForStatement;
	
	statement->mInitialExpression = Expression::Parse( l, scope );
	if ( statement->mInitialExpression == NULL )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}
	
	statement->mTestExpression = Expression::Parse( l, scope );
	if ( statement->mTestExpression == NULL )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}
	
	statement->mIterationExpression = Expression::Parse( l, scope, ')' );
	if ( statement->mIterationExpression == NULL )
	{
		COMPILE_ERROR( l, "Expected Expression in for statement" );
	}
	
	Scope *for_scope = new Scope( Scope::kScope_Loop );
	for_scope->setParent( scope );
	
	if ( l.peekSymbol() == '{' )
		statement->mStatement = Block::Parse( l, for_scope );
	else
		statement->mStatement = Statement::Parse( l, for_scope );
	
	return NULL;
}

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::cerr << argv[ 0 ] << " [filename]" << std::endl;
		return -1;
	}

	gScope = new Scope( Scope::kScope_Global );
	gScope->addType( new Type( "int" ) );
	gScope->addType( new Type( "char" ) );
	gScope->addType( new Type( "string" ) );
	LexerReader reader( argv[ 1 ] );
	Lexer l( &reader );
	
	SmartPtr<Module> mod = Module::Parse( l, gScope );

	if ( mod == NULL )
	{
		return -1;
	}
	else
	{
		cout << "Completed parse" << endl;
	}
	
	return 0;
}
