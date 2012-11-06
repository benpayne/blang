#include <iostream>
#include "FileLexer.h"
#include "Type.h"

#include "logging.h"

using namespace QLang;
using namespace std;

Scope *gScope;

Type *Type::Parse( Lexer &l, Scope &s, bool allow_void )
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
		t = s.findType( l.getSymbolText() );
	}
	else if ( sym == Lexer::SYMBOL )
	{
		t = s.findType( l.getSymbolText() );
	}
	else
	{
		cerr << "Parse Error at: " << l.getLineNumber() << endl;
		exit(-1);
	}
	
	return t;
}

#if 0

void parse_type_name( Lexer &l, bool allow_void )
{
	int sym = l.getSymbol();
	
	while ( sym == Lexer::TYPE_MODIFIER )
	{
		// save modifier
		sym = l.getSymbol();
	}
	
	if ( sym == Lexer::BUILTIN_TYPE || ( allow_void and sym == Lexer::VOID ) )
	{
		// get type
	}
	else if ( sym == Lexer::SYMBOL )
	{
		// Lookup type
	}
	else
	{
		// report error
	}
}

void parse_function_definition( Lexer &l )
{
	parse_type_name( l, true );
	int sym = l.getSymbol();
	if ( sym == Lexer::SYMBOL )
	{
		// read function name
	}
	else
	{
		cerr << "
		// report error
	}

	sym = l.getSymbol();
	if ( sym != '(' )
	{
		// report error
	}

	sym = l.getSymbol();
	if ( sym != ')' )
	{
		do {
			parse_type_name( l, false );
			sym = l.getSymbol();
			if ( sym == Lexer::SYMBOL )
			{
			}
			else
			{
				// report error
			}
			sym = l.getSymbol();
		} while ( sym == ',' );

		if ( sym != ')' )
		{
			// report error
		}
	}
}

void parse_file( Lexer &l )
{
	while( !l.isEOF() )
	{
		parse_function_definition( l );
	}
}
#endif

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::cerr << argv[ 0 ] << " [filename]" << std::endl;
		return -1;
	}

	gScope = new Scope( Scope::kScope_Global );
	LexerReader reader( argv[ 1 ] );
	Lexer l( &reader );
	
#if 1
	int sym;
	while ( (sym = l.getSymbol()) != -1 )
	{
		if ( sym < Lexer::NUM_SYMBOLS )
			std::cout << "Symbol " << sym << " (" << l.getSymbolText() << ")" << std::endl;
		else
			std::cout << "Symbol " << (char)sym << std::endl;
	}
#else
	parse_file( l );
#endif

	return 0;
}
