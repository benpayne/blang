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

AssertStatement *AssertStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_ASSERT )
	{
		COMPILE_ERROR( l, "Internal Error" );
	}

	AssertStatement *stmt = new AssertStatement;
	stmt->setLocation( loc );

	stmt->mExpression = Expression::ParseExpr( l, scope, 0 );
	if ( stmt->mExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected expression after 'assert'" );
	}

	// Check for optional message: assert expr, "message";
	if ( l.peekSymbol() == ',' )
	{
		l.getSymbol(); // consume ','

		sym = l.getSymbol();
		if ( sym != Lexer::CONSTANT_STRING )
		{
			COMPILE_ERROR( l, "Expected string literal for assert message" );
		}

		stmt->mMessage = l.getSymbolText();
	}

	sym = l.getSymbol();
	if ( sym != ';' )
	{
		COMPILE_ERROR( l, "Expected ';' after assert statement" );
	}

	return stmt;
}
