#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

ProtocolDefinition *ProtocolDefinition::Parse( Lexer &l, Scope *s )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_PROTOCOL )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
	{
		COMPILE_ERROR( l, "Expected protocol name" );
	}

	ProtocolDefinition *protoDef = new ProtocolDefinition( l.getSymbolText() );

	// Check for generic parameters: protocol Name<T>
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

			protoDef->mGenericParams.push_back( param );

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
		COMPILE_ERROR( l, "Expected '{' in protocol definition" );
	}

	// Use a local scope for method signatures so they don't pollute the global
	// scope and don't conflict with impl block methods of the same name.
	SmartPtr<Scope> protoScope = new Scope( Scope::kScope_Class, protoDef->getName() );
	protoScope->setParent( s );

	while ( l.peekSymbol() != '}' )
	{
		// Each method signature must start with 'fn'
		if ( l.peekSymbol() != Lexer::KEYWORD_FN )
		{
			COMPILE_ERROR( l, "Expected 'fn' for method signature in protocol" );
		}

		// Use FunctionDefinition::Parse to handle the fn signature.
		// The signature ends with ';' (no body), which FunctionDefinition::Parse
		// handles as a bodyless declaration.
		SmartPtr<FunctionDefinition> method = FunctionDefinition::Parse( l, protoScope );

		protoDef->mRequiredMethods.push_back( method );
	}

	sym = l.getSymbol();
	assert( sym == '}' );

	s->addSymbol( protoDef );

	cout << "Completed protocol " << protoDef->getName() << endl;

	return protoDef;
}
