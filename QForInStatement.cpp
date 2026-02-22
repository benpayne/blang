#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

SET_LOG_CAT( LOG_CAT_ALL );
SET_LOG_LEVEL( LOG_LVL_NOISE );

using namespace QLang;
using namespace std;

// Parse for-in loop: for VAR in EXPR { BODY }
// Parse for-in with two vars: for KEY, VALUE in EXPR { BODY }
// Parse infinite loop: for { BODY }
ForInStatement *ForInStatement::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	// Consume 'for' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_FOR )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'for' keyword" );
	}

	ForInStatement *statement = new ForInStatement;

	Scope *loop_scope = new Scope( Scope::kScope_Loop );
	loop_scope->setParent( scope );

	// Check for infinite loop: for { body }
	if ( l.peekSymbol() == '{' )
	{
		statement->mIsInfinite = true;
		statement->mBody = Block::Parse( l, loop_scope );
		return statement;
	}

	// Parse loop variable name
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
	{
		COMPILE_ERROR( l, "Expected variable name in for-in loop" );
	}
	statement->mVariableName = l.getSymbolText();

	// Check for second variable: for key, value in expr
	if ( l.peekSymbol() == ',' )
	{
		l.getSymbol(); // consume ','
		sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
		{
			COMPILE_ERROR( l, "Expected second variable name after ',' in for-in loop" );
		}
		statement->mSecondVariableName = l.getSymbolText();
	}

	// Expect 'in' keyword
	sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IN )
	{
		COMPILE_ERROR( l, "Expected 'in' keyword in for-in loop" );
	}

	// Add loop variable(s) to scope as variables
	Type *varType = new Type( "var" );
	VariableDefinition *loopVar = new VariableDefinition( varType, statement->mVariableName );
	loop_scope->addSymbol( loopVar );

	if ( !statement->mSecondVariableName.empty() )
	{
		VariableDefinition *loopVar2 = new VariableDefinition( varType, statement->mSecondVariableName );
		loop_scope->addSymbol( loopVar2 );
	}

	// Parse the iterable expression (could be a range like 0..10 or a variable)
	statement->mIterableExpression = Expression::ParseExpr( l, loop_scope, 0 );
	if ( statement->mIterableExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected expression after 'in' in for-in loop" );
	}

	// Parse the loop body
	if ( l.peekSymbol() == '{' )
		statement->mBody = Block::Parse( l, loop_scope );
	else
		statement->mBody = Statement::Parse( l, loop_scope );

	return statement;
}
