#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

StructDefinition *StructDefinition::Parse( Lexer &l, Scope *s )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_STRUCT )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
	{
		COMPILE_ERROR( l, "Expected struct name" );
	}

	StructDefinition *structDef = new StructDefinition( l.getSymbolText() );

	sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' in struct definition" );
	}

	while ( l.peekSymbol() != '}' )
	{
		SmartPtr<Type> fieldType = Type::Parse( l, s, false );
		if ( fieldType == nullptr )
		{
			COMPILE_ERROR( l, "Expected field type in struct definition" );
		}

		sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
		{
			COMPILE_ERROR( l, "Expected field name in struct definition" );
		}

		VariableDefinition *field = new VariableDefinition( fieldType, l.getSymbolText() );
		structDef->mFields.push_back( field );

		sym = l.getSymbol();
		if ( sym != ';' )
		{
			COMPILE_ERROR( l, "Expected ';' after struct field" );
		}
	}

	sym = l.getSymbol();
	assert( sym == '}' );

	s->addSymbol( structDef );

	cout << "Completed struct " << structDef->getName() << endl;

	return structDef;
}
