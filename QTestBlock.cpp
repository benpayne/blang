#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"
#include "Frontend.h"

SET_LOG_CAT( LOG_CAT_ALL );
SET_LOG_LEVEL( LOG_LVL_NOISE );

using namespace QLang;
using namespace std;

TestBlock *TestBlock::Parse( Lexer &l, Scope *s )
{
	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_TEST )
	{
		COMPILE_ERROR( l, "Internal Error" );
	}

	sym = l.getSymbol();
	if ( sym != Lexer::CONSTANT_STRING )
	{
		COMPILE_ERROR( l, "Expected test name string after 'test'" );
	}

	string name = l.getSymbolText();

	Scope *testScope = new Scope( Scope::kScope_Function, name );
	testScope->setParent( s );

	TestBlock *testBlock = new TestBlock( name );
	testBlock->setLocation( loc );

	testBlock->mBody = Block::Parse( l, testScope );

	PARSE_TRACE( "Completed test block: " << name );

	return testBlock;
}
