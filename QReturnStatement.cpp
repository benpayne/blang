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
	ReturnStatement *statement = NULL;
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_RETURN )
	{
		COMPILE_ERROR( l, "Internal Error" );
	}
	
	statement = new ReturnStatement;
	statement->mExpression = Expression::Parse( l, scope );
	if ( statement->mExpression == NULL )
	{
		COMPILE_ERROR( l, "Failed to parse expression in return" );
		delete statement;
		return NULL;
	}
	
	return statement;
}
