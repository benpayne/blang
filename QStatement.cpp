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
		statement = ForStatement::Parse( l, scope );
		break;
	case Lexer::KEYWORD_RETURN:
		statement = ReturnStatement::Parse( l, scope );
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
		} catch( CompileError &err ) {
			LOG( "Not a decl, resetting position: %d", pos );
			l.setCurrentPos( pos );
			try {
				statement = Expression::Parse( l, scope );
			} catch ( CompileError &err2 ) {
				l.setCurrentPos( pos );
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

