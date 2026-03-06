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

	// Handle 'self' parameter specially — it has an implicit type
	if ( l.peekSymbol() == Lexer::KEYWORD_SELF )
	{
		l.getSymbol(); // consume 'self'
		SmartPtr<Type> selfType = new Type( "self" );
		def = new VariableDefinition( selfType, "self" );
		s->addSymbol( def );
		return def;
	}

	// Check for ownership qualifier before type: own, shared, sync
	OwnershipQualifier ownership = OwnershipQualifier::kOwnership_Value;
	if ( l.peekSymbol() == Lexer::KEYWORD_OWN )
	{
		l.getSymbol(); // consume 'own'
		ownership = OwnershipQualifier::kOwnership_Own;
	}
	else if ( l.peekSymbol() == Lexer::KEYWORD_SHARED )
	{
		l.getSymbol(); // consume 'shared'
		ownership = OwnershipQualifier::kOwnership_Shared;
	}
	else if ( l.peekSymbol() == Lexer::KEYWORD_SYNC )
	{
		l.getSymbol(); // consume 'sync'
		ownership = OwnershipQualifier::kOwnership_Sync;
	}

	SmartPtr<Type> t = Type::Parse( l, s, false );

	if ( t == nullptr )
		return nullptr;

	// cstring and carray types are only allowed in extern fn declarations
	if ( !isExtern && t != nullptr && ( t->getName() == "cstring" || t->getName() == "carray" ) )
		COMPILE_ERROR( l, "'" + t->getName() + "' type can only be used in extern fn declarations" );

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

	// Apply ownership qualifier if specified
	if ( def != nullptr && ownership != OwnershipQualifier::kOwnership_Value )
		def->setOwnership( ownership );

	return def;
}

VariableDeclaration *VariableDeclaration::Parse( Lexer &l, Scope *s )
{
	VariableDeclaration *def = new VariableDeclaration;
	bool isConst = false;
	bool isVar = false;
	OwnershipQualifier ownership = OwnershipQualifier::kOwnership_Value;

	// Check for ownership qualifier before type: own, shared, sync
	if ( l.peekSymbol() == Lexer::KEYWORD_OWN )
	{
		l.getSymbol(); // consume 'own'
		ownership = OwnershipQualifier::kOwnership_Own;
	}
	else if ( l.peekSymbol() == Lexer::KEYWORD_SHARED )
	{
		l.getSymbol(); // consume 'shared'
		ownership = OwnershipQualifier::kOwnership_Shared;
	}
	else if ( l.peekSymbol() == Lexer::KEYWORD_SYNC )
	{
		l.getSymbol(); // consume 'sync'
		ownership = OwnershipQualifier::kOwnership_Sync;
	}

	// Check for const or var modifier before parsing the type
	if ( l.peekSymbol() == Lexer::TYPE_MODIFIER )
	{
		// Peek doesn't consume, so we need to consume to get the text
		int modSym = l.getSymbol();
		string modText = l.getSymbolText();

		if ( modText == "var" )
		{
			isVar = true;
		}
		else if ( modText == "const" )
		{
			isConst = true;
		}
	}

	SmartPtr<Type> t = nullptr;

	if ( isVar )
	{
		// var keyword already consumed, use placeholder type
		t = new Type( "var" );
	}
	else
	{
		// Normal type parse
		t = Type::Parse( l, s, false );
	}

	// cstring and carray types are only allowed in extern fn declarations
	if ( t != nullptr && ( t->getName() == "cstring" || t->getName() == "carray" ) )
		COMPILE_ERROR( l, "'" + t->getName() + "' type can only be used in extern fn declarations" );

	do {
		VariableDeclaration::DeclData data;
		int sym = l.getSymbol();

		if ( t != nullptr && sym == Lexer::SYMBOL )
		{
			data.mVaribale = new VariableDefinition( t, l.getSymbolText() );
			data.mVaribale->setConst( isConst );
			data.mVaribale->setOwnership( ownership );
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

		// const variables must have an initializer
		if ( isConst && data.mInitialValue == nullptr )
		{
			COMPILE_ERROR( l, "const variable must be initialized" );
		}

		// var variables must have an initializer for type inference
		if ( isVar && data.mInitialValue == nullptr )
		{
			COMPILE_ERROR( l, "var variable must be initialized" );
		}

		def->mVariables.push_back( data );

		if ( l.peekSymbol() != ',' )
			break;

		l.getSymbol(); // consume ','
	} while ( true );

	return def;
}
