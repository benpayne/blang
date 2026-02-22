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

	sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' in protocol definition" );
	}

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
		SmartPtr<FunctionDefinition> method = FunctionDefinition::Parse( l, s );

		protoDef->mRequiredMethods.push_back( method );
	}

	sym = l.getSymbol();
	assert( sym == '}' );

	s->addSymbol( protoDef );

	cout << "Completed protocol " << protoDef->getName() << endl;

	return protoDef;
}
