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

MatchExpression *MatchExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	// Consume 'match' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_MATCH )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'match' keyword" );
	}

	MatchExpression *expr = new MatchExpression;

	// Parse the subject expression (a primary expression before '{')
	expr->mSubject = Expression::ParsePrimary( l, scope );
	if ( expr->mSubject == nullptr )
	{
		COMPILE_ERROR( l, "Expected expression after 'match'" );
	}

	// Expect '{'
	sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' after match subject" );
	}

	// Parse match arms until '}'
	while ( l.peekSymbol() != '}' )
	{
		MatchArm arm;

		// Parse pattern: integer literal, string literal, or identifier
		sym = l.peekSymbol();
		if ( sym == Lexer::CONSTANT_NUMBER ||
			 sym == Lexer::CONSTANT_STRING ||
			 sym == Lexer::CONSTANT_CHAR )
		{
			l.getSymbol();
			arm.mPattern = l.getSymbolText();
		}
		else if ( sym == Lexer::SYMBOL )
		{
			l.getSymbol();
			arm.mPattern = l.getSymbolText();
		}
		else
		{
			COMPILE_ERROR( l, "Expected pattern in match arm (constant or identifier)" );
		}

		// Parse the arm body as a Block
		Scope *armScope = new Scope( Scope::kScope_Anonymous );
		armScope->setParent( scope );
		arm.mBody = Block::Parse( l, armScope );
		if ( arm.mBody == nullptr )
		{
			COMPILE_ERROR( l, "Expected block body for match arm" );
		}

		expr->mArms.push_back( arm );
	}

	// Consume '}'
	sym = l.getSymbol();
	assert( sym == '}' );

	return expr;
}
