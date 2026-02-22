#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

void StructDefinition::ParseImplBlock( Lexer &l, Scope *s )
{
	int sym = l.getSymbol();
	if ( sym != Lexer::KEYWORD_IMPL )
	{
		COMPILE_ERROR( l, "Internal Compiler Error" );
	}

	// Read the first identifier (could be protocol name or struct name)
	sym = l.getSymbol();
	if ( sym != Lexer::SYMBOL )
	{
		COMPILE_ERROR( l, "Expected identifier after 'impl'" );
	}

	string firstName = l.getSymbolText();
	string structName;
	string protocolName;

	// Check if this is 'impl Protocol for Struct' or 'impl Struct'
	sym = l.peekSymbol();
	if ( sym == Lexer::KEYWORD_FOR )
	{
		// impl Protocol for Struct { ... }
		l.getSymbol(); // consume 'for'
		protocolName = firstName;

		sym = l.getSymbol();
		if ( sym != Lexer::SYMBOL )
		{
			COMPILE_ERROR( l, "Expected struct name after 'for'" );
		}

		structName = l.getSymbolText();
	}
	else
	{
		// impl Struct { ... }
		structName = firstName;
	}

	// Look up the struct in scope
	Symbol *structSym = s->findSymbol( structName );
	if ( structSym == nullptr )
	{
		COMPILE_ERROR( l, "Unknown struct '" + structName + "' in impl block" );
	}

	StructDefinition *structDef = dynamic_cast<StructDefinition *>( structSym );
	if ( structDef == nullptr )
	{
		COMPILE_ERROR( l, "'" + structName + "' is not a struct" );
	}

	// If a protocol was specified, verify it exists
	if ( !protocolName.empty() )
	{
		Symbol *protoSym = s->findSymbol( protocolName );
		if ( protoSym == nullptr )
		{
			COMPILE_ERROR( l, "Unknown protocol '" + protocolName + "' in impl block" );
		}

		ProtocolDefinition *protoDef = dynamic_cast<ProtocolDefinition *>( protoSym );
		if ( protoDef == nullptr )
		{
			COMPILE_ERROR( l, "'" + protocolName + "' is not a protocol" );
		}
	}

	// Expect '{'
	sym = l.getSymbol();
	if ( sym != '{' )
	{
		COMPILE_ERROR( l, "Expected '{' in impl block" );
	}

	// Use a local scope for impl methods so they don't pollute the global scope
	// and don't conflict with protocol declarations of the same method name.
	SmartPtr<Scope> implScope = new Scope( Scope::kScope_Class, structName );
	implScope->setParent( s );

	// Parse methods until '}'
	while ( l.peekSymbol() != '}' )
	{
		if ( l.peekSymbol() != Lexer::KEYWORD_FN )
		{
			COMPILE_ERROR( l, "Expected 'fn' for method in impl block" );
		}

		SmartPtr<FunctionDefinition> method = FunctionDefinition::Parse( l, implScope );
		structDef->addMethod( method );
	}

	sym = l.getSymbol();
	assert( sym == '}' );

	if ( !protocolName.empty() )
	{
		// Verify that the struct implements all required protocol methods
		Symbol *protoSym = s->findSymbol( protocolName );
		ProtocolDefinition *protoDef = dynamic_cast<ProtocolDefinition *>( protoSym );

		// protoDef is non-null here — we already validated it above
		const std::vector<SmartPtr<FunctionDefinition> > &required = protoDef->getRequiredMethods();
		const std::vector<SmartPtr<FunctionDefinition> > &implemented = structDef->getMethods();

		for ( const SmartPtr<FunctionDefinition> &req : required )
		{
			bool found = false;
			for ( const SmartPtr<FunctionDefinition> &impl : implemented )
			{
				if ( impl->getName() == req->getName() )
				{
					found = true;
					break;
				}
			}

			if ( !found )
			{
				COMPILE_ERROR( l, "Struct '" + structName + "' does not implement method '" + req->getName() + "' required by protocol '" + protocolName + "'" );
			}
		}

		cout << "Completed impl " << protocolName << " for " << structName << endl;
	}
	else
	{
		cout << "Completed impl " << structName << endl;
	}
}
