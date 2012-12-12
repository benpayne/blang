#include "FileLexer.h"
#include <iostream>

using namespace std;

Lexer::Lexer( LexerReader *reader ) : mReader( reader ), 
	mLastSym( -1 ), mCurrentPos( 0 )
{
	lineno = 1;
	charPos = 0;
}

const std::string &Lexer::getSymbolText()
{
	return mMatchString;
}

bool Lexer::isAlpha( char c )
{
	if (( c >= 'a' && c <= 'z' ) ||
		( c >= 'A' && c <= 'Z' ) ||
		( c == '_' ) )
	{
		return true;
	}
	
	return false;
}

bool Lexer::isAlphaNum( char c )
{
	if (( c >= 'a' && c <= 'z' ) ||
		( c >= 'A' && c <= 'Z' ) ||
		( c >= '0' && c <= '9' ) ||
		( c == '_' ) )
	{
		return true;
	}
	
	return false;
}

bool Lexer::match( const char *match_str )
{
	int len = strlen( match_str );
	
	for ( int i = 0; i < len; i++ )
	{
		if ( (*mReader)[ i ] != match_str[ i ] )
			return false;
	}

	mMatchString = match_str;
	mReader->popChar( len );
	return true;
}

bool Lexer::matchKeyword( const char *match_str )
{
	int len = strlen( match_str );
	int i = 0;
	
	for ( ; i < len; i++ )
	{
		if ( (*mReader)[ i ] != match_str[ i ] )
			return false;
	}

	char endChar = (*mReader)[ i ];
	
	if ( isAlphaNum( endChar ) )
		return false;
	
	mMatchString = match_str;
	mReader->popChar( len );
	return true;
}

void Lexer::readSymbol()
{
	mMatchString.append( 1, mReader->popChar() );

	while ( !mReader->isEOF() )
	{
		if ( isAlphaNum( mReader->peekChar() ) )
		{
			mMatchString.append( 1, mReader->popChar() );
		}
		else
			return;
	}
}

void Lexer::readStringConst()
{
	// pop the " char
	mReader->popChar();
	
	while ( !mReader->isEOF() )
	{
		if ( mReader->peekChar() == '\n' )
			printf( "Multi-line string constant on line %d\n", lineno );
		
		if ( mReader->peekChar() == '\\' )
		{
			switch ( (*mReader)[ 1 ] )
			{
				case 'a': // 7
				case 'b': // 8
				case 't': // 9
				case 'n': // 10
				case 'v': // 11
				case 'f': // 12
				case 'r': // 13
				case '\"':
				case '\'':
					mMatchString.append( 1, (*mReader)[ 1 ] );
					mReader->popChar( 2 );
					break;
				case '0':
					mMatchString.append( 1, '\0' );
					mReader->popChar( 2 );
					break;				
			}
		}
		else if ( mReader->peekChar() != '\"' )
		{
			mMatchString.append( 1, mReader->popChar() );
		}
		else
		{
			mReader->popChar();
			return;
		}
	}
}

void Lexer::readCharacterConst()
{
	// pop the ' char
	mReader->popChar();
	
	while ( !mReader->isEOF() )
	{
		if ( mReader->peekChar() == '\n' )
			printf( "Multi-line char constant on line %d\n", lineno );

		// could improve the proper grammmer for the constant value.		
		if ( mReader->peekChar() != '\'' )
		{
			mMatchString.append( 1, mReader->popChar() );
		}
		else
		{
			mReader->popChar();
			return;
		}
	}
}

void Lexer::readConst()
{
	bool hex = false;
	bool oct = false;
	bool valid_char = true;
	
	if ( mReader->peekChar() == '0' )
	{
		if ( (*mReader)[ 1 ] == 'x' or (*mReader)[ 1 ] == 'X' )
		{
			hex = true;
			mReader->popChar( 2 );
			mMatchString = "0x";
		}
		else
		{
			oct = true;
			mReader->popChar( 1 );
			mMatchString = "0";
		}
	}
	
	while ( !mReader->isEOF() and valid_char )
	{
		char c = mReader->peekChar();
	
		if ( hex and ((c >= '0' and c <= '9') or (c >= 'a' and c <= 'f') or (c >= 'A' and c <= 'F')) )
			mMatchString.append( 1, mReader->popChar() );
		else if ( oct and (c >= '0' and c <= '7') )
			mMatchString.append( 1, mReader->popChar() );
		else if ( c >= '0' and c <= '9' )
			mMatchString.append( 1, mReader->popChar() );
		else
			valid_char = false;
	}
}

void Lexer::handleComment( bool singleLine )
{
	while ( !mReader->isEOF() )
	{
		//std::cout << "single " << singleLine << ", char " << mReader->peekChar() << std::endl;
		if ( !singleLine and match( "*/" ) )
		{
			mMatchString.clear();
			return;
		}
		else if ( mReader->peekChar() == '\n' )
		{
			lineno += 1;
			mReader->popChar();
			if ( singleLine )
			{
				mMatchString.clear();
				return;
			}
		}
		else
			mReader->popChar();
	}
}

int Lexer::peekSymbol()
{
	if ( mLastSym == -1 )
		mLastSym = getSymbolInternal();
	return mLastSym;
}

int Lexer::getSymbol()
{	
	int sym = -1;
	
	if ( mLastSym != -1 )
	{
		sym = mLastSym;
		mLastSym = -1;
	}
	else 
		sym = getSymbolInternal();

	mCurrentPos += 1;		
	
	return sym;
}

int Lexer::getCurrentPos()
{
	return mCurrentPos;
}

void Lexer::setCurrentPos( int pos )
{
	if ( pos >= 0 or pos < mSymbolList.size() )
		mCurrentPos = pos;
	else
		mCurrentPos = mSymbolList.size() - 1;
}

int Lexer::getSymbolInternal()
{
	int sym = -1;
	
	if ( mCurrentPos == mSymbolList.size() )
	{
		sym = getSymbolFromFile();
		mSymbolList.push_back( SymbolInfo( sym, mMatchString ) );
	}
	else
	{
		sym = mSymbolList[ mCurrentPos ].symbol;
		mMatchString = mSymbolList[ mCurrentPos ].symbolText;
	}

	if ( sym < Lexer::NUM_SYMBOLS )
		std::cout << "Symbol " << sym << " (" << mMatchString << ")" << std::endl;
	else
		std::cout << "Symbol " << (char)sym << std::endl;
	
	return sym;
}

int Lexer::getSymbolFromFile()
{
	mMatchString.clear();
	mLastSym = -1;
	
	while ( !mReader->isEOF() )
	{
		//std::cout << "Lexer Symbol " << (int)mReader->peekChar() << std::endl;
		
		// check for keywords
		switch ( mReader->peekChar() )
		{
			case 'c':
				if ( matchKeyword( "char" ) )
					return BUILTIN_TYPE;
				else if ( matchKeyword( "const" ) )
					return TYPE_MODIFIER;
				break;
			case 'd':
				if ( matchKeyword( "double" ) )
					return BUILTIN_TYPE;
				break;
			case 'e':
				if ( matchKeyword( "else" ) )
					return KEYWORD_ELSE;
				else if ( matchKeyword( "extern" ) )
					return TYPE_MODIFIER;
				break;
			case 'f':
				if ( matchKeyword( "float" ) )
					return BUILTIN_TYPE;
				else if ( matchKeyword( "for" ) )
					return KEYWORD_FOR;
				break;
			case 'i':
				if ( matchKeyword( "int" ) )
					return BUILTIN_TYPE;
				else if ( matchKeyword( "if" ) )
					return KEYWORD_IF;
				break;
			case 'l':
				if ( matchKeyword( "long" ) )
					return BUILTIN_TYPE;
				break;
			case 'r':
				if ( matchKeyword( "return" ) )
					return KEYWORD_RETURN;
				break;
			case 's':
				if ( matchKeyword( "string" ) )
					return BUILTIN_TYPE;
				else if ( matchKeyword( "static" ) )
					return TYPE_MODIFIER;
				else if ( matchKeyword( "signed" ) )
					return TYPE_MODIFIER;
				else if ( matchKeyword( "short" ) )
					return BUILTIN_TYPE;
				break;
			case 'u':
				if ( matchKeyword( "unsigned" ) )
					return TYPE_MODIFIER;
				break;
			case 'v':
				if ( matchKeyword( "void" ) )
					return VOID;
				break;
			case 'w':
				if ( matchKeyword( "while" ) )
					return KEYWORD_WHILE;
				break;
		}
		
		if ( isAlpha( mReader->peekChar() ) )
		{
			readSymbol();
			return SYMBOL;
		}
		
		if ( mReader->peekChar() == '\"' )
		{
			readStringConst();
			return CONSTANT_STRING;
		}
	
		if ( mReader->peekChar() == '\'' )
		{
			readCharacterConst();
			return CONSTANT_CHAR;
		}
	
		if ( mReader->peekChar() >= '0' && mReader->peekChar() <= '9' )
		{
			readConst();
			return CONSTANT_NUMBER;
		}
		
		switch( mReader->peekChar() )
		{
			case '|':
				if ( match( "||" ) )
					return LOR;
				else
					return mReader->popChar();
				break;
				
			case '&':
				if ( match( "&&" ) )
					return LAND;
				else
					return mReader->popChar();
				break;
	
			case '=':
				if ( match( "==" ) )
					return EQ;
				else
					return mReader->popChar();
				break;
				
			case '!':
				if ( match( "!=" ) )
					return EQ;
				else
					return mReader->popChar();
				break;
			case '<':
				if ( match( "<<" ) )
					return SHIFT;
				else if ( match( "<=" ) )
					return ASSIGN;
				else
					return mReader->popChar();
				break;
			case '>':
				if ( match( ">>" ) )
					return SHIFT;
				else if ( match( ">=" ) )
					return ASSIGN;
				else
					return mReader->popChar();
				break;
					
			case ' ':
			case '\t':
				mReader->popChar();
				break;
			case '\n':
				lineno += 1;
				mReader->popChar();
				break;
				
			case '/':
				if ( match( "//" ) )
				{
					handleComment( true );
					break;
				}
				else if ( match( "/*" ) )
				{
					handleComment( false );
					break;
				}
				// drop thur to operations
			case '-':
			case '+':
			case '*':
			case '%':
			case '^':
				if ( (*mReader)[ 1 ] == '=' )
				{
					mMatchString.append( 1, mReader->popChar() );
					mMatchString.append( 1, mReader->popChar() );
					return ASSIGN;
				}
				else
				{
					return mReader->popChar();
				}
				break;
				
			case '{':
			case '}':
			case '[':
			case ']':
			case '(':
			case ')':
			case ';':
			case ',':
				return mReader->popChar();
				break;
			
			default:
				printf( "Unknown Charater %d\n", mReader->peekChar() );
				return mReader->popChar();
				break;
		}			
	}
	
	return -1;
}

