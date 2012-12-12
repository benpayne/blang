#include <assert.h>

#include <iostream>
#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

using namespace QLang;
using namespace std;

Block *Block::Parse( Lexer &l, Scope *block_scope )
{
	Block *block = new Block;
	block->mScope = block_scope;
	
	int sym = l.getSymbol();
	if ( sym != '{' )
	{
		if ( sym == 7 )
			cerr << "Symbol found instead: " << l.getSymbolText() << endl;
		else
			cerr << "symbol found is: " << sym << "(" << (char)sym << ")" << endl; 
		COMPILE_ERROR( l, "expected \'{\'" );
	}
	
	while ( l.peekSymbol() != '}' )
	{
		Statement *statement = Statement::Parse( l, block->mScope );
		block->mStatementList.push_back( statement );
	}
	
	sym = l.getSymbol();
	assert( sym == '}' );
	return block;
}
