#include "FileLexer.h"

LexerReader::LexerReader( const std::string &filename )
{
	mFile.open( filename.c_str() );
}

char LexerReader::operator[]( int i )
{
	std::streampos p = mFile.tellg();
	std::streampos orig_p = p;
	p += i;
	mFile.seekg( p );
	char c = mFile.peek();
	mFile.seekg( orig_p );
	return c;
}
	
char LexerReader::popChar()
{
	return mFile.get();
}

void LexerReader::popChar( int count )
{
	for ( int i = 0; i < count; i++ )
		mFile.get();
}

char LexerReader::peekChar()
{
	return mFile.peek();
}

bool LexerReader::isEOF()
{
	// called to set the EOF flag if we are at the end.
	mFile.peek();
	return mFile.eof();
}

