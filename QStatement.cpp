#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

Statement *Statement::Parse( Lexer &l, Scope *scope )
{
	Statement *statement = NULL;
	int pos = l.getCurrentPos();
	
	cout << "Saving position" << endl;
	
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
			statement = VariableDefinition::Parse( l, scope );
		} catch( CompileError &err ) {
			cerr << "Setting position to " << pos << endl;
			l.setCurrentPos( pos );
			statement = Expression::Parse( l, scope );
		}				
		break;
	}
	
	return statement;
}

