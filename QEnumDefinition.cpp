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

// Parse: enum Name { Variant1, Variant2(Type1, Type2), ... }
// Parse: enum Name<T> { Variant1, Variant2(T), ... }
EnumDefinition *EnumDefinition::Parse( Lexer &l, Scope *s, bool isPublic )
{
	TRACE_BEGIN( LOG_LVL_INFO );

	SourceLocation loc = l.getTokenLocation();
	// Consume 'enum' keyword
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_ENUM )
	{
		COMPILE_ERROR( l, "Internal Error: expected 'enum' keyword" );
	}

	// Parse enum name
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
	{
		COMPILE_ERROR( l, "Expected enum name" );
	}
	string enumName = l.getSymbolText();

	EnumDefinition *enumDef = new EnumDefinition( enumName );
	enumDef->setLocation( loc );
	enumDef->mIsPublic = isPublic;

	// Check for generic parameters: <T> or <T: Constraint>
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

			enumDef->mGenericParams.push_back( param );

			// Register type parameter in scope
			s->addType( new Type( param.mName ) );

			sym = l.getSymbol();
		} while ( sym == ',' );

		if ( sym != '>' )
			COMPILE_ERROR( l, "Expected '>' after generic parameters" );
	}

	// Register the enum's TYPE name before parsing the variants so a variant
	// payload can reference the enum itself (recursive enums:
	// `enum Expr { num(int), add(Expr, Expr) }`). The definition symbol is
	// still registered after the body parses.
	s->addType( new Type( enumName ) );

	// Expect '{'
	sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' after enum name" );
	}

	// Parse variants until '}'
	while ( l.peekSymbol() != '}' )
	{
		Variant variant;
		variant.mLocation = l.getTokenLocation();

		// Parse variant name
		sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
		{
			COMPILE_ERROR( l, "Expected variant name in enum" );
		}
		variant.mName = l.getSymbolText();

		// Check for associated types: Variant(Type1, Type2)
		if ( l.peekSymbol() == '(' )
		{
			l.getSymbol(); // consume '('

			if ( l.peekSymbol() != ')' )
			{
				do {
					Type *t = Type::Parse( l, s, false );
					if ( t == nullptr )
						COMPILE_ERROR( l, "Expected type in enum variant" );
					variant.mAssociatedTypes.push_back( t );

					sym = l.getSymbol();
				} while ( sym == ',' );

				if ( sym != ')' )
					COMPILE_ERROR( l, "Expected ')' after variant types" );
			}
			else
			{
				l.getSymbol(); // consume ')'
			}
		}

		enumDef->mVariants.push_back( variant );

		// Optional comma between variants
		if ( l.peekSymbol() == ',' )
			l.getSymbol();
	}

	// Consume '}'
	sym = l.getSymbol();
	assert( sym == '}' );

	// Register the enum definition symbol (the type name was registered before
	// the variant loop, for recursive self-references)
	s->addSymbol( enumDef );

	LOG( "Parsed enum: %s with %d variants", enumName.c_str(), (int)enumDef->mVariants.size() );

	return enumDef;
}
