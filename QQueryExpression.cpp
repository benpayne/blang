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

// Parse a query pipeline step body: { expression }
// Used for where, order_by, join conditions
static Expression *parseStepBody( Lexer &l, Scope *scope )
{
	int sym = l.getSymbol();
	if ( sym != '{' )
		COMPILE_ERROR( l, "Expected '{' in query pipeline step" );

	Expression *expr = Expression::ParseExpr( l, scope, 0 );
	if ( expr == nullptr )
		COMPILE_ERROR( l, "Expected expression in query pipeline step body" );

	sym = l.getSymbol();
	if ( sym != '}' )
		COMPILE_ERROR( l, "Expected '}' in query pipeline step" );

	return expr;
}

// Parse set step assignments: { .field = value, .field = value, ... }
static QueryPipelineStep parseSetStep( Lexer &l, Scope *scope )
{
	QueryPipelineStep step;
	step.mType = QueryPipelineStep::SET;

	int sym = l.getSymbol();
	if ( sym != '{' )
		COMPILE_ERROR( l, "Expected '{' after 'set'" );

	do {
		// Expect .field
		sym = l.getSymbol();
		if ( sym != '.' )
			COMPILE_ERROR( l, "Expected '.field' in set clause" );

		sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
			COMPILE_ERROR( l, "Expected field name after '.' in set clause" );

		string fieldName = l.getSymbolText();

		// Expect =
		sym = l.getSymbol();
		if ( sym != '=' )
			COMPILE_ERROR( l, "Expected '=' after field name in set clause" );

		// Parse value expression
		Expression *value = Expression::ParseExpr( l, scope, 0 );
		if ( value == nullptr )
			COMPILE_ERROR( l, "Expected expression after '=' in set clause" );

		step.mSetFields.push_back( make_pair( fieldName, SmartPtr<Expression>( value ) ) );

		// Check for ',' or '}'
		if ( l.peekSymbol() == ',' )
			l.getSymbol(); // consume ','
	} while ( l.peekSymbol() != '}' );

	sym = l.getSymbol();
	assert( sym == '}' );

	return step;
}

// Parse pipeline steps after a query/update/delete keyword and table name.
// Steps are: |> where { ... }, |> order_by { ... }, |> limit(n), |> first,
//            |> join Table on { ... }, |> set { ... }
static void parsePipelineSteps( Lexer &l, Scope *scope, vector<QueryPipelineStep> &steps )
{
	while ( l.peekSymbol() == Lexer::PIPE_ARROW )
	{
		l.getSymbol(); // consume |>

		int sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
			COMPILE_ERROR( l, "Expected pipeline step name after '|>'" );

		string stepName = l.getSymbolText();
		QueryPipelineStep step;

		if ( stepName == "where" )
		{
			step.mType = QueryPipelineStep::WHERE;
			step.mExpression = parseStepBody( l, scope );
		}
		else if ( stepName == "order_by" )
		{
			step.mType = QueryPipelineStep::ORDER_BY;
			step.mExpression = parseStepBody( l, scope );
		}
		else if ( stepName == "limit" )
		{
			step.mType = QueryPipelineStep::LIMIT;
			sym = l.getSymbol();
			if ( sym != '(' )
				COMPILE_ERROR( l, "Expected '(' after 'limit'" );
			step.mExpression = Expression::ParseExpr( l, scope, 0 );
			if ( step.mExpression == nullptr )
				COMPILE_ERROR( l, "Expected expression in limit()" );
			sym = l.getSymbol();
			if ( sym != ')' )
				COMPILE_ERROR( l, "Expected ')' after limit expression" );
		}
		else if ( stepName == "first" )
		{
			step.mType = QueryPipelineStep::FIRST;
			// No arguments
		}
		else if ( stepName == "join" )
		{
			step.mType = QueryPipelineStep::JOIN;
			sym = l.getSymbol();
			if ( sym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected table name after 'join'" );
			step.mJoinTable = l.getSymbolText();

			// Expect 'on' keyword
			sym = l.getSymbol();
			if ( sym != Lexer::KEYWORD_ON )
				COMPILE_ERROR( l, "Expected 'on' after join table name" );

			step.mExpression = parseStepBody( l, scope );
		}
		else if ( stepName == "set" )
		{
			step = parseSetStep( l, scope );
		}
		else
		{
			COMPILE_ERROR( l, "Unknown query pipeline step '" + stepName + "'" );
		}

		steps.push_back( step );
	}
}

QueryExpression *QueryExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol(); // consume 'query'
	assert( sym == Lexer::KEYWORD_QUERY );

	// Parse table name
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
		COMPILE_ERROR( l, "Expected table name after 'query'" );

	QueryExpression *expr = new QueryExpression( l.getSymbolText() );
	expr->setLocation( loc );

	// Parse pipeline steps
	parsePipelineSteps( l, scope, expr->mSteps );

	cout << "Completed query expression on " << expr->mTableName << endl;
	return expr;
}

InsertExpression *InsertExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol(); // consume 'insert'
	assert( sym == Lexer::KEYWORD_INSERT );

	// Parse table name
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
		COMPILE_ERROR( l, "Expected table name after 'insert'" );

	InsertExpression *expr = new InsertExpression( l.getSymbolText() );
	expr->setLocation( loc );

	// Expect '{' for field assignments
	sym = l.getSymbol();
	if ( sym != '{' )
		COMPILE_ERROR( l, "Expected '{' after table name in insert expression" );

	// Parse field: value pairs
	if ( l.peekSymbol() != '}' )
	{
		do {
			sym = l.getSymbol();
			if ( sym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected field name in insert expression" );
			string fieldName = l.getSymbolText();

			sym = l.getSymbol();
			if ( sym != ':' )
				COMPILE_ERROR( l, "Expected ':' after field name in insert expression" );

			Expression *value = Expression::ParseExpr( l, scope, 0 );
			if ( value == nullptr )
				COMPILE_ERROR( l, "Expected expression for field value in insert" );

			expr->addField( fieldName, value );

			if ( l.peekSymbol() == ',' )
				l.getSymbol(); // consume ','
		} while ( l.peekSymbol() != '}' );
	}

	sym = l.getSymbol();
	if ( sym != '}' )
		COMPILE_ERROR( l, "Expected '}' in insert expression" );

	cout << "Completed insert expression on " << expr->mTableName << endl;
	return expr;
}

UpdateExpression *UpdateExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol(); // consume 'update'
	assert( sym == Lexer::KEYWORD_UPDATE );

	// Parse table name
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
		COMPILE_ERROR( l, "Expected table name after 'update'" );

	UpdateExpression *expr = new UpdateExpression( l.getSymbolText() );
	expr->setLocation( loc );

	// Parse pipeline steps (where, set)
	parsePipelineSteps( l, scope, expr->mSteps );

	cout << "Completed update expression on " << expr->mTableName << endl;
	return expr;
}

DeleteExpression *DeleteExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	int sym = l.getSymbol(); // consume 'delete'
	assert( sym == Lexer::KEYWORD_DELETE );

	// Parse table name
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
		COMPILE_ERROR( l, "Expected table name after 'delete'" );

	DeleteExpression *expr = new DeleteExpression( l.getSymbolText() );
	expr->setLocation( loc );

	// Parse pipeline steps (where)
	parsePipelineSteps( l, scope, expr->mSteps );

	cout << "Completed delete expression on " << expr->mTableName << endl;
	return expr;
}
