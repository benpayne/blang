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

SpawnStatement *SpawnStatement::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	// Consume 'spawn' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_SPAWN )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'spawn' keyword" );
	}

	SpawnStatement *statement = new SpawnStatement;
	statement->setLocation( loc );

	// Create anonymous scope for spawn body
	Scope *spawn_scope = new Scope( Scope::kScope_Anonymous );
	spawn_scope->setParent( scope );

	// Expect '{' and parse Block
	if ( l.peekSymbol() != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' after 'spawn'" );
	}

	statement->mBody = Block::Parse( l, spawn_scope );

	PARSE_TRACE( "Completed spawn statement parse" );

	return statement;
}

WaitStatement *WaitStatement::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	// Consume 'wait' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_WAIT )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'wait' keyword" );
	}

	WaitStatement *statement = new WaitStatement;
	statement->setLocation( loc );

	// Parse the expression (Task variable to wait on)
	statement->mExpr = Expression::ParseExpr( l, scope, 0 );
	if ( statement->mExpr == nullptr )
	{
		COMPILE_ERROR( l, "Expected expression after 'wait'" );
	}

	// Expect semicolon
	sym = l.getSymbol();
	if ( sym != ';' )
	{
		COMPILE_ERROR( l, "Expected ';' after wait expression" );
	}

	PARSE_TRACE( "Completed wait statement parse" );

	return statement;
}

WaitAllStatement *WaitAllStatement::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	// Consume 'wait_all' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_WAIT_ALL )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'wait_all' keyword" );
	}

	WaitAllStatement *statement = new WaitAllStatement;
	statement->setLocation( loc );

	// Expect semicolon
	sym = l.getSymbol();
	if ( sym != ';' )
	{
		COMPILE_ERROR( l, "Expected ';' after wait_all" );
	}

	PARSE_TRACE( "Completed wait_all statement parse" );

	return statement;
}
