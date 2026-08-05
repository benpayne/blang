#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"
#include "Frontend.h"

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
		// Check for 'static fn' methods
		bool isStatic = false;
		if ( l.peekSymbol() == Lexer::KEYWORD_STATIC )
		{
			l.getSymbol(); // consume 'static'
			isStatic = true;
		}

		// Check for 'init' constructor
		if ( l.peekSymbol() == Lexer::KEYWORD_INIT )
		{
			l.getSymbol(); // consume 'init'

			if ( structDef->hasInit() )
				COMPILE_ERROR( l, "Duplicate 'init' constructor for struct '" + structName + "'" );

			SmartPtr<FunctionDefinition> initFunc = FunctionDefinition::ParseInit( l, implScope );
			structDef->addMethod( initFunc );
			structDef->setInitMethod( initFunc );
			continue;
		}

		if ( l.peekSymbol() != Lexer::KEYWORD_FN )
		{
			COMPILE_ERROR( l, "Expected 'fn', 'init', or 'static fn' in impl block" );
		}

		SmartPtr<FunctionDefinition> method = FunctionDefinition::Parse( l, implScope );
		method->setStatic( isStatic );

		// Validate: static methods must NOT have self parameter
		if ( isStatic )
		{
			for ( int pi = 0; pi < method->getNumberParams(); pi++ )
			{
				VariableDefinition *param = method->getParam( pi );
				if ( param->getVariableType() != nullptr &&
					 param->getVariableType()->getName() == "self" )
					COMPILE_ERROR( l, "Static methods cannot have 'self' parameter" );
			}
		}

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

		// A protocol GENERIC param (protocol Container<T> { fn get(self, int)
		// -> T; }) acts as a wildcard: the implementing method may use any
		// concrete type in that position. Concrete protocol types must match
		// exactly, and arity must agree — name-only conformance accepted
		// impls with the wrong shape and deferred the failure to a confusing
		// call-site/link error.
		auto isProtoWildcard = [&]( Type *t ) -> bool
		{
			if ( t == nullptr )
				return false;
			for ( const auto &gp : protoDef->getGenericParams() )
				if ( gp.mName == t->getName() )
					return true;
			return false;
		};
		auto typeText = []( Type *t ) -> std::string
		{
			return ( t == nullptr ) ? "void" : t->getName();
		};

		for ( const SmartPtr<FunctionDefinition> &req : required )
		{
			FunctionDefinition *match = nullptr;
			for ( const SmartPtr<FunctionDefinition> &impl : implemented )
			{
				if ( impl->getName() == req->getName() )
				{
					match = (FunctionDefinition *)(const FunctionDefinition *)impl;
					break;
				}
			}

			if ( match == nullptr )
			{
				COMPILE_ERROR( l, "Struct '" + structName + "' does not implement method '" + req->getName() + "' required by protocol '" + protocolName + "'" );
			}

			FunctionDefinition *reqFn = (FunctionDefinition *)(const FunctionDefinition *)req;
			if ( match->getNumberParams() != reqFn->getNumberParams() )
			{
				COMPILE_ERROR( l, "method '" + req->getName() + "' of struct '" + structName +
					"' takes " + std::to_string( match->getNumberParams() ) +
					" parameter(s) but protocol '" + protocolName + "' requires " +
					std::to_string( reqFn->getNumberParams() ) );
			}

			for ( int pi = 0; pi < reqFn->getNumberParams(); pi++ )
			{
				VariableDefinition *rp = reqFn->getParam( pi );
				VariableDefinition *ip = match->getParam( pi );
				if ( rp == nullptr || ip == nullptr )
					continue;
				if ( rp->getName() == "self" || ip->getName() == "self" )
					continue;
				Type *rt = rp->getVariableType();
				Type *it = ip->getVariableType();
				if ( isProtoWildcard( rt ) )
					continue;
				if ( rt != nullptr && it != nullptr && rt->getName() != it->getName() )
				{
					COMPILE_ERROR( l, "method '" + req->getName() + "' of struct '" + structName +
						"': parameter " + std::to_string( pi ) + " is '" + typeText( it ) +
						"' but protocol '" + protocolName + "' requires '" + typeText( rt ) + "'" );
				}
			}

			Type *reqRet = reqFn->getReturnType();
			Type *implRet = match->getReturnType();
			if ( !isProtoWildcard( reqRet ) )
			{
				std::string rn = typeText( reqRet ), in = typeText( implRet );
				if ( rn != in )
				{
					COMPILE_ERROR( l, "method '" + req->getName() + "' of struct '" + structName +
						"' returns '" + in + "' but protocol '" + protocolName +
						"' requires '" + rn + "'" );
				}
			}
		}

		// Record the conformance so the .bmod can carry it across a module
		// boundary (D16). Until now the protocol name was used for checking and
		// then discarded, so a consumer had no way to know a foreign type was
		// Printable.
		structDef->addConformedProtocol( protocolName );

		PARSE_TRACE( "Completed impl " << protocolName << " for " << structName );
	}
	else
	{
		PARSE_TRACE( "Completed impl " << structName );
	}
}
