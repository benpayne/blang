#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

std::ostream &QLang::operator<<(std::ostream &out, const VariableDefinition &var)
{
	out << var.mType << " " << var.getName();
	return out;
}

VariableDefinition *VariableDefinition::ParseFuncParam( Lexer &l, Scope *s )
{
	VariableDefinition *def = NULL;
	SmartPtr<Type> t = Type::Parse( l, s, false );
	int sym = l.getSymbol();
	if ( t != NULL && sym == Lexer::SYMBOL )
	{
		def = new VariableDefinition( t, l.getSymbolText() );
	}

	return def;
}

VariableDeclaration *VariableDeclaration::Parse( Lexer &l, Scope *s )
{
	VariableDefinition *def = NULL;
	SmartPtr<Type> t = Type::Parse( l, s, false );
	
	do {
		int sym = l.getSymbol();
		if ( t != NULL && sym == Lexer::SYMBOL )
		{
			def = new VariableDefinition( t, l.getSymbolText() );
		}
		else
		{
			// report error
			COMPILE_ERROR( l, "Failed parse varible" );
		}
	
		s->addSymbol( def );
		
		if ( l.peekSymbol() == '=' )
		{
			sym = l.getSymbol();
			Expression *exp = Expression::Parse( l, scope );
			if ( exp != NULL )
			{
				AssignmentExpression *assign = new AssignmentExpression( "=", def, exp );
				def				
	} while ( l.peekSymbol() == ',' )
	
	return def;
}
