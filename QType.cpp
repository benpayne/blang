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
	return out;
}

Type *Type::Parse( Lexer &l, Scope *s, bool allow_void )
{
	int sym = l.getSymbol();
	Type *t = NULL;
	
	while ( sym == Lexer::TYPE_MODIFIER )
	{
		// save modifier
		sym = l.getSymbol();
	}
	
	if ( sym == Lexer::BUILTIN_TYPE || ( allow_void and sym == Lexer::VOID ) )
	{
		t = s->findType( l.getSymbolText() );
	}
	else if ( sym == Lexer::SYMBOL )
	{
		t = s->findType( l.getSymbolText() );
	}
	else
	{
		COMPILE_ERROR( l, "Parse Error" );
	}
	
	return t;
}

