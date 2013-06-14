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
		s->addSymbol( def );
	}

	return def;
}

VariableDeclaration *VariableDeclaration::Parse( Lexer &l, Scope *s )
{
	VariableDeclaration *def = new VariableDeclaration;
	SmartPtr<Type> t = Type::Parse( l, s, false );
	
	do {
		VariableDeclaration::DeclData data;
		int sym = l.getSymbol();
		
		if ( t != NULL && sym == Lexer::SYMBOL )
		{
			data.mVaribale = new VariableDefinition( t, l.getSymbolText() );
		}
		else
		{
			// report error
			COMPILE_ERROR( l, "Failed parse varible" );
		}
	
		s->addSymbol( data.mVaribale );
		
		if ( l.peekSymbol() == '=' )
		{
			sym = l.getSymbol();
			data.mInitialValue = Expression::Parse( l, s );
			if ( data.mInitialValue == NULL )
			{
				// report error
				COMPILE_ERROR( l, "Failed parse value" );
			}
		}
		def->mVariables.push_back( data );
	} while ( l.peekSymbol() == ',' );
	
	return def;
}
