#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

std::ostream &QLang::operator<<(std::ostream &out, const Type &type)
{
	out << type.mName;
	if ( !type.mTypeParams.empty() )
	{
		out << "<";
		for ( size_t i = 0; i < type.mTypeParams.size(); i++ )
		{
			if ( i > 0 ) out << ", ";
			out << *type.mTypeParams[ i ];
		}
		out << ">";
	}
	return out;
}

Type *Type::Parse( Lexer &l, Scope *s, bool allow_void )
{
	int sym = l.getSymbol();
	Type *t = nullptr;

	while ( sym == Lexer::TYPE_MODIFIER )
	{
		// save modifier
		sym = l.getSymbol();
	}

	if ( sym == Lexer::BUILTIN_TYPE || sym == Lexer::BOOL || ( allow_void and sym == Lexer::VOID ) )
	{
		t = s->findType( l.getSymbolText() );
	}
	else if ( sym == Lexer::KEYWORD_CSTRING )
	{
		t = new Type( "cstring" );
	}
	else if ( sym == Lexer::KEYWORD_CARRAY )
	{
		// carray requires generic type argument: carray<T>
		if ( l.peekSymbol() != '<' )
			COMPILE_ERROR( l, "carray requires a type argument: carray<T>" );
		l.getSymbol(); // consume '<'
		Type *elemType = Type::Parse( l, s, false );
		if ( elemType == nullptr )
			COMPILE_ERROR( l, "Expected type argument for carray" );
		t = new Type( "carray" );
		t->addTypeParam( elemType );
		int closeSym = l.getSymbol();
		if ( closeSym != '>' )
			COMPILE_ERROR( l, "Expected '>' after carray type argument" );
		return t; // already parsed generic args, skip the generic check below
	}
	else if ( sym == Lexer::KEYWORD_CHAN )
	{
		// Channel type requires a generic type argument: chan<T>
		if ( l.peekSymbol() != '<' )
			COMPILE_ERROR( l, "chan requires a type argument: chan<T>" );
		l.getSymbol(); // consume '<'
		Type *elemType = Type::Parse( l, s, false );
		if ( elemType == nullptr )
			COMPILE_ERROR( l, "Expected type argument for chan" );
		t = new Type( "chan" );
		t->addTypeParam( elemType );
		int closeSym = l.getSymbol();
		if ( closeSym != '>' )
			COMPILE_ERROR( l, "Expected '>' after chan type argument" );
		return t; // already parsed generic args, skip the generic check below
	}
	else if ( sym == Lexer::KEYWORD_FN )
	{
		// Function type: fn(ParamTypes) -> RetType
		FunctionType *ft = new FunctionType();
		if ( l.getSymbol() != '(' )
			COMPILE_ERROR( l, "Expected '(' in function type" );
		if ( l.peekSymbol() != ')' )
		{
			do {
				Type *pt = Type::Parse( l, s, false );
				if ( pt == nullptr )
					COMPILE_ERROR( l, "Expected parameter type in function type" );
				ft->addParamType( pt );
				int next = l.getSymbol();
				if ( next == ')' ) break;
				if ( next != ',' )
					COMPILE_ERROR( l, "Expected ',' or ')' in function type" );
			} while ( true );
		}
		else
		{
			l.getSymbol(); // consume ')'
		}
		if ( l.peekSymbol() == Lexer::ARROW )
		{
			l.getSymbol(); // consume '->'
			ft->setReturnType( Type::Parse( l, s, false ) );
		}
		return ft;
	}
	else if ( sym == Lexer::SYMBOL )
	{
		string symName = l.getSymbolText();

		// Check for module-qualified type: net.Socket
		Scope *nsScope = s->findNamespace( symName );
		if ( nsScope != nullptr && s->isModuleImported( symName ) && l.peekSymbol() == '.' )
		{
			l.getSymbol(); // consume '.'
			int memberSym = l.getSymbol();
			if ( memberSym != Lexer::SYMBOL )
				COMPILE_ERROR( l, "Expected type name after '" + symName + ".'" );
			string memberName = l.getSymbolText();
			t = nsScope->findType( memberName );
			if ( t == nullptr )
				COMPILE_ERROR( l, "Module '" + symName + "' has no type '" + memberName + "'" );
		}
		else
		{
			t = s->findType( symName );
		}
	}
	else
	{
		COMPILE_ERROR( l, "Parse Error" );
	}

	// Check for generic type arguments: Type<Arg1, Arg2>
	if ( t != nullptr && l.peekSymbol() == '<' )
	{
		l.getSymbol(); // consume '<'
		Type *genericType = new Type( t->getName() );

		do {
			Type *param = Type::Parse( l, s, false );
			if ( param == nullptr )
				COMPILE_ERROR( l, "Expected type argument" );
			genericType->addTypeParam( param );

			int nextSym = l.getSymbol();
			if ( nextSym == '>' )
				break;
			if ( nextSym != ',' )
				COMPILE_ERROR( l, "Expected ',' or '>' in type arguments" );
		} while ( true );

		t = genericType;
	}

	return t;
}
