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

SpawnStatement *SpawnStatement::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	// Consume 'spawn' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_SPAWN )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'spawn' keyword" );
	}

	SpawnStatement *statement = new SpawnStatement;

	// Create anonymous scope for spawn body
	Scope *spawn_scope = new Scope( Scope::kScope_Anonymous );
	spawn_scope->setParent( scope );

	// Expect '{' and parse Block
	if ( l.peekSymbol() != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' after 'spawn'" );
	}

	statement->mBody = Block::Parse( l, spawn_scope );

	cout << "Completed spawn statement parse" << endl;

	return statement;
}
