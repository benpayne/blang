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

VariableDefinition *VariableDefinition::ParseFuncParam( Lexer &l, Scope *s, bool isExtern, int paramIndex )
{
	VariableDefinition *def = nullptr;
	SmartPtr<Type> t = Type::Parse( l, s, false );

	if ( t == nullptr )
		return nullptr;

	// Peek at the next token to determine if a parameter name follows
	int sym = l.peekSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		// Named parameter — consume the name
		l.getSymbol();
		def = new VariableDefinition( t, l.getSymbolText() );
		s->addSymbol( def );
	}
	else if ( isExtern && ( sym == ',' || sym == ')' ) )
	{
		// Unnamed parameter in an extern declaration — generate a synthetic name
		string syntheticName = "_arg" + to_string( paramIndex );
		def = new VariableDefinition( t, syntheticName );
		s->addSymbol( def );
	}
	else
	{
		// Non-extern function requires named parameters
		COMPILE_ERROR( l, "Expected parameter name" );
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
		
		if ( t != nullptr && sym == Lexer::SYMBOL )
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
			if ( data.mInitialValue == nullptr )
			{
				// report error
				COMPILE_ERROR( l, "Failed parse value" );
			}
		}
		def->mVariables.push_back( data );

		if ( l.peekSymbol() != ',' )
			break;

		l.getSymbol(); // consume ','
	} while ( true );
	
	return def;
}
