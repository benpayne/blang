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

// Parse a field access expression starting from the current lexer position.
// Expects the lexer to be positioned at the beginning of an expression
// that includes a dot (e.g., "point.x").  This parses the object expression,
// then iteratively consumes ".fieldName" postfix operations.
//
// Note: The primary entry point for field access parsing is the postfix loop
// in Expression::ParsePrimary (QExpression.cpp).  This Parse method provides
// a standalone entry point for contexts that need to parse a complete field
// access expression from scratch.
FieldAccessExpression *FieldAccessExpression::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	// Parse the object expression (the part before the dot)
	Expression *object = Expression::ParsePrimary( l, scope );
	if ( object == nullptr )
		COMPILE_ERROR( l, "Expected expression before '.'" );

	// The postfix loop in ParsePrimary already handles chained field access,
	// so if we get here the result should already be a FieldAccessExpression.
	FieldAccessExpression *fieldAccess = dynamic_cast<FieldAccessExpression*>( object );
	if ( fieldAccess == nullptr )
		COMPILE_ERROR( l, "Expected field access expression (expr.field)" );

	return fieldAccess;
}
