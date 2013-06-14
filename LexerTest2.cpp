#include <assert.h>

#include <iostream>
#include <vector>
#include <map>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

vector<pair<int, string> > gSymbolList;

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		cerr << argv[ 0 ] << " [filename]" << endl;
		return -1;
	}

	LexerReader reader( argv[ 1 ] );
	Lexer l( &reader );
	
	int sym;
	for( int i = 0; i < 10; i++ )
	{
		sym = l.getSymbol();
	}
	
	//sym = l.peekSymbol();

	int pos = l.getCurrentPos();
	
	if ( pos != 10 )
	{
		cout << "Failed to get proper position" << endl;
		return -1;
	}
	
	for( int i = 0; i < 10; i++ )
	{
		sym = l.getSymbol();
		gSymbolList.push_back( pair<int, string>( sym, l.getSymbolText() ) );

		if ( sym < Lexer::NUM_SYMBOLS )
			std::cout << "-Symbol (" << i << ") " << sym << " (" << l.getSymbolText() << ")" << std::endl;
		else
			std::cout << "-Symbol (" << i << ") " << (char)sym << std::endl;
	}
	
	l.setCurrentPos( pos );
	
	for( int i = 0; i < 10; i++ )
	{
		sym = l.getSymbol();
		if ( sym < Lexer::NUM_SYMBOLS )
			std::cout << "+Symbol (" << i << ") " << sym << " (" << l.getSymbolText() << ")" << std::endl;
		else
			std::cout << "+Symbol (" << i << ") " << (char)sym << std::endl;
		if ( gSymbolList[ i ].first != sym or 
			gSymbolList[ i ].second != l.getSymbolText() )
		{
			cerr << "Failded to match on " << i << "nth symbol" << endl;
			return -1;
		}
	}

	l.setCurrentPos( 2 );
	
	for( int i = 0; i < 10; i++ )
	{
		sym = l.getSymbol();
		gSymbolList.push_back( pair<int, string>( sym, l.getSymbolText() ) );

		if ( sym < Lexer::NUM_SYMBOLS )
			std::cout << "Symbol (" << i << ") " << sym << " (" << l.getSymbolText() << ")" << std::endl;
		else
			std::cout << "Symbol (" << i << ") " << (char)sym << std::endl;
	}
	
	cout << "Test passed" << endl;
	
	return 0;
}
