#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

ReturnStatement *ReturnStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	ReturnStatement *statement = nullptr;
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_RETURN )
	{
		COMPILE_ERROR( l, "Internal Error" );
	}

	statement = new ReturnStatement;
	statement->setLocation( loc );

	// Check for bare 'return;' (void return)
	if ( l.peekSymbol() == ';' )
	{
		l.getSymbol(); // consume the semicolon
		statement->mExpression = nullptr;
		return statement;
	}

	statement->mExpression = Expression::Parse( l, scope );
	if ( statement->mExpression == nullptr )
	{
		COMPILE_ERROR( l, "Failed to parse expression in return" );
		delete statement;
		return nullptr;
	}

	return statement;
}
