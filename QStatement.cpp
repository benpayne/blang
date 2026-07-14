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

// Of two parse-attempt errors, return the one that progressed further into the
// source (greater line, then greater column). A statement that is neither a
// valid declaration nor a valid expression should report the deeper, more
// specific cause — the attempt that consumed more input before failing — not a
// generic "Unexpected token" at column 1 (U2, FR-006 / R4).
static const CompileError &deeperError( const CompileError &a, const CompileError &b )
{
	const SourceLocation &la = a.getLocation();
	const SourceLocation &lb = b.getLocation();
	if ( lb.line > la.line || ( lb.line == la.line && lb.col > la.col ) )
		return b;
	return a;
}

Statement *Statement::Parse( Lexer &l, Scope *scope )
{
	TRACE_BEGIN( LOG_LVL_INFO );
	Statement *statement = nullptr;
	int pos = l.getCurrentPos();
	
	LOG( "Saving position: %d", pos );
	
	switch ( l.peekSymbol() )
	{
	case Lexer::KEYWORD_WHILE:
		statement = WhileStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_IF:
		statement = IfStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_FOR:
		statement = ForInStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_RETURN:
		statement = ReturnStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_BREAK:
		statement = BreakStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_CONTINUE:
		statement = ContinueStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_MATCH:
		statement = MatchExpression::Parse( l, scope );
		break;
	case Lexer::KEYWORD_SPAWN:
		statement = SpawnStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_WAIT:
		statement = WaitStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_WAIT_ALL:
		statement = WaitAllStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_ASSERT:
		statement = AssertStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_ON:
		statement = EventHandler::Parse( l, scope );
		break;
	case '{':
		statement = Block::Parse( l, scope );
		break;
	case ';':
		l.getSymbol();
		break;
	default:
		try {
			statement = VariableDeclaration::Parse( l, scope );
		} catch( CompileError &declErr ) {
			LOG( "Not a decl, resetting position: %d", pos );
			l.setCurrentPos( pos );
			try {
				statement = Expression::Parse( l, scope );
			} catch ( CompileError &exprErr ) {
				l.setCurrentPos( pos );
				// Neither a declaration nor an expression: surface the deeper,
				// located cause instead of a generic "Unexpected token". Fall
				// back to a located generic error only if neither attempt
				// carries a usable location.
				const CompileError &deepest = deeperError( declErr, exprErr );
				if ( deepest.getLocation().isSet() )
					throw deepest;
				COMPILE_ERROR( l, "Unexpected token" );
			}
			if ( statement == nullptr )
			{
				l.setCurrentPos( pos );
				COMPILE_ERROR( l, "Unexpected token" );
			}
		}
		break;
	}
	
	return statement;
}

