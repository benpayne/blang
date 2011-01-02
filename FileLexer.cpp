#include "Lexer.h"

Lexer::Lexer( LexerReader *reader ) : mReader( reader )
{
	lineno = 0;
	charPos = 0;
}

std::string Lexer::getSymbolText()
{
	return mMatchString;
}

bool Lexer::match( const char *match_str )
{
	int len = strlen( match_str );
	
	for ( int i = 0; i < len; i++ )
	{
		if ( mReader[ i ] != match_str[ i ] )
			return false;
	}

	mMatchString = match_str;
	return true;
}

void Lexer::readSymbol()
{
	mMatchString.append( 1, mReader->popChar() );

	while ( !mReader->isEOF() )
	{
		if (( mReader->peekChar() >= 'a' && mReader->peekChar() <= 'z' ) ||
			( mReader->peekChar() >= 'A' && mReader->peekChar() <= 'Z' ) ||
			( mReader->peekChar() >= '0' && mReader->peekChar() <= '9' ) ||
			( mReader->peekChar() >= '_' ) )
		{
			mMatchString.append( 1, mReader->popChar() );
		}
		else
			return;
	}
}

void Lexer::readStringConst()
{
	mReader->popChar();
	
	while ( !mReader->isEOF() )
	{
		if ( mReader->peekChar() == '\n' )
			printf( "Multi-line string constant on line %d", lineno );
		
		if ( mReader->peekChar() == '\\' )
		{
			switch ( mReader[ 1 ] )
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
					mMatchString.append( 1, mReader[ 1 ] );
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
			return;
	}
}

void Lexer::readCharacterConst()
{
	while ( !mReader->isEOF() )
	{
		if ( mReader->peekChar() == '\n' )
			printf( "Multi-line string constant on line %d", lineno );
		
		if ( mReader->peekChar() != '\'' )
		{
			mMatchString.append( 1, mReader->popChar() );
		}
		else
			return;
	}
}

void Lexer::readConst()
{

}

void Lexer::handleComment( bool singleLine )
{
}

int Lexer::getSymbol()
{
	mMatchString.clear();

	while ( !mReader->isEOF() )
	{
		// check for keywords
		switch ( mReader->peekChar() )
		{
			case 'c':
				if ( match( "char" ) )
					return BUILTIN_TYPE;
				else if ( match( "const" ) )
					return TYPE_MODIFIER;
				break;
			case 'd':
				if ( match( "double" ) )
					return BUILTIN_TYPE;
				break;
			case 'e':
				if ( match( "else" ) )
					return KEYWORD_ELSE;
				else if ( match( "extern" ) )
					return TYPE_MODIFIER;
				break;
			case 'f':
				if ( match( "float" ) )
					return BUILTIN_TYPE;
				else if ( match( "for" ) )
					return KEYWORD_FOR;
				break;
			case 'i':
				if ( match( "int" ) )
					return BUILTIN_TYPE;
				else if ( match( "if" ) )
					return KEYWORD_IF;
				break;
			case 'l':
				if ( match( "long" ) )
					return BUILTIN_TYPE;
				break;
			case 'r':
				if ( match( "return" ) )
					return BUILTIN_TYPE;
				break;
			case 's':
				if ( match( "string" ) )
					return BUILTIN_TYPE;
				else if ( match( "static" ) )
					return TYPE_MODIFIER;
				else if ( match( "signed" ) )
					return TYPE_MODIFIER;
				else if ( match( "short" ) )
					return BUILTIN_TYPE;
				break;
			case 'u':
				if ( match( "unsigned" ) )
					return TYPE_MODIFIER;
				break;
			case 'v':
				if ( match( "void" ) )
					return VOID;
				break;
			case 'w':
				if ( match( "while" ) )
					return BUILTIN_TYPE;
				break;
		}
		
		if ( ( mReader->peekChar() >= 'a' && mReader->peekChar() <= 'z' ) ||
			( mReader->peekChar() >= 'A' && mReader->peekChar() <= 'Z' ) )
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
				if ( match( "!=" ) )
					return EQ;
				else if ( match( "=" ) )
					return ASSIGN;
				break;
			case '>':
				if ( match( ">>" ) )
					return SHIFT;
				else if ( match( "=" ) )
					return ASSIGN;
				break;
	
			case '/':
				if ( match( "//" ) )
					handleComment( true );
				else if ( match( "/*" ) )
					handleComment( false );
				break;
				
			case ' ':
			case '\t':
				mReader->popChar();
				break;
			case '\n':
				lineno += 1;
				mReader->popChar();
				break;
				
			case '-':
			case '+':
			case '*':
			case '/':
			case '%':
			case '^':
				if ( mReader[ 1 ] == '=' )
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
				printf( "Unknown Charater %c\n", mReader->peekChar() );
				return mReader->popChar();
				break;
		}			
	}
}

