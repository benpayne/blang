#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

StructDefinition *StructDefinition::Parse( Lexer &l, Scope *s, bool isPublic )
{
	SourceLocation loc = l.getTokenLocation();
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
	structDef->setLocation( loc );
	structDef->mIsPublic = isPublic;

	// Check for generic parameters: struct Name<T> or struct Name<T: Constraint>
	if ( l.peekSymbol() == '<' )
	{
		l.getSymbol(); // consume '<'

		do {
			sym = l.getSymbol();
			if ( sym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected type parameter name" );

			GenericParam param;
			param.mName = l.getSymbolText();

			// Check for constraint: <T: Comparable>
			if ( l.peekSymbol() == ':' )
			{
				l.getSymbol(); // consume ':'
				sym = l.getSymbol();
				if ( sym != Lexer::SYMBOL )
					COMPILE_ERROR( l, "Expected constraint name after ':'" );
				param.mConstraint = l.getSymbolText();
			}

			structDef->mGenericParams.push_back( param );

			// Register type parameter in scope
			s->addType( new Type( param.mName ) );

			sym = l.getSymbol();
		} while ( sym == ',' );

		if ( sym != '>' )
			COMPILE_ERROR( l, "Expected '>' after generic parameters" );
	}

	sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' in struct definition" );
	}

	while ( l.peekSymbol() != '}' )
	{
		SourceLocation fieldLoc = l.getTokenLocation();
		SmartPtr<Type> fieldType = Type::Parse( l, s, false );
		if ( fieldType == nullptr )
		{
			COMPILE_ERROR( l, "Expected field type in struct definition" );
		}

		// cstring and carray types are only allowed in extern fn declarations
		if ( fieldType->getName() == "cstring" || fieldType->getName() == "carray" )
			COMPILE_ERROR( l, "'" + fieldType->getName() + "' type can only be used in extern fn declarations" );

		sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
		{
			COMPILE_ERROR( l, "Expected field name in struct definition" );
		}

		VariableDefinition *field = new VariableDefinition( fieldType, l.getSymbolText() );
		field->setLocation( fieldLoc );
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
	s->addType( new Type( structDef->getName() ) );

	cout << "Completed struct " << structDef->getName() << endl;

	return structDef;
}
