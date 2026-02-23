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

EventHandler *EventHandler::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	// Consume 'on' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_ON )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'on' keyword" );
	}

	EventHandler *handler = new EventHandler;

	// Parse the event expression (e.g., timer.every(1000))
	handler->mEventExpression = Expression::ParseExpr( l, scope, 0 );
	if ( handler->mEventExpression == nullptr )
	{
		COMPILE_ERROR( l, "Expected event expression after 'on'" );
	}

	// Create anonymous scope for event handler body
	Scope *handler_scope = new Scope( Scope::kScope_Anonymous );
	handler_scope->setParent( scope );

	// Expect '{' and parse Block
	if ( l.peekSymbol() != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' after event expression in 'on' handler" );
	}

	handler->mBody = Block::Parse( l, handler_scope );

	cout << "Completed event handler parse" << endl;

	return handler;
}
