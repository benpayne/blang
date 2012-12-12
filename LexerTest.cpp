#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::cerr << argv[ 0 ] << " [filename]" << std::endl;
		return -1;
	}

	LexerReader reader( argv[ 1 ] );
	Lexer l( &reader );
	
	int sym;
	while ( (sym = l.getSymbol()) != -1 )
	{
		if ( sym < Lexer::NUM_SYMBOLS )
			std::cout << "Symbol " << sym << " (" << l.getSymbolText() << ")" << std::endl;
		else
			std::cout << "Symbol " << (char)sym << std::endl;
	}

	cout << "Completed parse" << endl;
	
	return 0;
}
