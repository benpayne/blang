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

MatchExpression *MatchExpression::Parse( Lexer &l, Scope *scope, bool exprMode )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	// Consume 'match' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_MATCH )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'match' keyword" );
	}

	MatchExpression *expr = new MatchExpression;
	expr->setLocation( loc );
	expr->mExprMode = exprMode;

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

		// Parse pattern: integer literal, string literal, identifier, wildcard (_),
		// or destructuring pattern like ok(value), some(x)
		sym = l.peekSymbol();
		if ( sym == Lexer::CONSTANT_NUMBER ||
			 sym == Lexer::CONSTANT_FLOAT ||
			 sym == Lexer::CONSTANT_STRING ||
			 sym == Lexer::CONSTANT_CHAR )
		{
			l.getSymbol();
			arm.mPattern = l.getSymbolText();
			arm.mPatternIsString = ( sym == Lexer::CONSTANT_STRING );
		}
		else if ( sym == Lexer::WILDCARD )
		{
			l.getSymbol();
			arm.mPattern = "_";
			arm.mIsWildcard = true;
		}
		else if ( sym == Lexer::SYMBOL )
		{
			l.getSymbol();
			arm.mPattern = l.getSymbolText();

			// Check for destructuring: pattern(binding)
			// e.g., ok(value), err(e), some(x)
			if ( l.peekSymbol() == '(' )
			{
				l.getSymbol(); // consume '('

				sym = l.getSymbol();
				if ( sym == Lexer::SYMBOL )
				{
					arm.mBindingName = l.getSymbolText();
				}
				else if ( sym != ')' )
				{
					COMPILE_ERROR( l, "Expected variable name or ')' in match pattern" );
				}

				if ( sym != ')' )
				{
					sym = l.getSymbol();
					if ( sym != ')' )
						COMPILE_ERROR( l, "Expected ')' after binding name in match pattern" );
				}
			}
		}
		else
		{
			COMPILE_ERROR( l, "Expected pattern in match arm (constant, identifier, or '_')" );
		}

		// Parse the arm body as a Block
		Scope *armScope = new Scope( Scope::kScope_Anonymous );
		armScope->setParent( scope );

		// If there's a binding, add it to the arm scope
		if ( !arm.mBindingName.empty() )
		{
			Type *varType = new Type( "var" );
			VariableDefinition *binding = new VariableDefinition( varType, arm.mBindingName );
			armScope->addSymbol( binding );
		}

		if ( exprMode )
		{
			// Expression-form arm: `pattern { expression }` — exactly one
			// expression, no semicolon; its value is the arm's result.
			sym = l.getSymbol();
			if ( sym != '{' )
				COMPILE_ERROR( l, "Expected '{' after match arm pattern" );
			arm.mValue = Expression::ParseExpr( l, armScope, 0 );
			if ( arm.mValue == nullptr )
				COMPILE_ERROR( l, "Expected expression in match arm (a value-producing match arm holds a single expression)" );
			sym = l.getSymbol();
			if ( sym != '}' )
				COMPILE_ERROR( l, "Expected '}' after match arm expression (a value-producing match arm holds a single expression, no ';')" );
			arm.mScope = armScope;
		}
		else
		{
			arm.mBody = Block::Parse( l, armScope );
			if ( arm.mBody == nullptr )
			{
				COMPILE_ERROR( l, "Expected block body for match arm" );
			}
		}

		expr->mArms.push_back( arm );
	}

	// Consume '}'
	sym = l.getSymbol();
	assert( sym == '}' );

	return expr;
}
