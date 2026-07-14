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

BreakStatement *BreakStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_BREAK )
	{
		COMPILE_ERROR( l, "Internal Error" );
	}

	sym = l.getSymbol();
	if ( sym != ';' )
	{
		COMPILE_ERROR( l, "Expected ';' after break" );
	}

	BreakStatement *statement = new BreakStatement;
	statement->setLocation( loc );
	return statement;
}

ContinueStatement *ContinueStatement::Parse( Lexer &l, Scope *scope )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_CONTINUE )
	{
		COMPILE_ERROR( l, "Internal Error" );
	}

	sym = l.getSymbol();
	if ( sym != ';' )
	{
		COMPILE_ERROR( l, "Expected ';' after continue" );
	}

	ContinueStatement *statement = new ContinueStatement;
	statement->setLocation( loc );
	return statement;
}
