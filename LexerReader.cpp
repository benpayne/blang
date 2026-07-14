#include "FileLexer.h"

LexerReader::LexerReader( const std::string &filename ) : mFileName( filename )
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
	char c = mFile.get();
	// Track 1-based line/column of the next unconsumed character. Every
	// character consumed advances the column by one (tabs included); a
	// newline advances the line and resets the column. This is the single
	// consumption funnel, so multi-line strings and block comments are
	// counted correctly.
	if ( c == '\n' )
	{
		mLine += 1;
		mCol = 1;
	}
	else
	{
		mCol += 1;
	}
	return c;
}

void LexerReader::popChar( int count )
{
	for ( int i = 0; i < count; i++ )
		popChar();
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
