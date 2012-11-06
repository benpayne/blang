#ifndef FILE_LEXER_H_
#define FILE_LEXER_H_

#include <fstream>
#include <string>

class LexerReader
{
public:
	LexerReader( const std::string &filename );
	
	char operator[]( int i );
	
	char popChar();
	void popChar( int count );
	char peekChar();
	
	bool isEOF();
	
private:
	std::ifstream mFile;
};

class Lexer
{
public:
	Lexer( LexerReader *reader );
	
	int getSymbol();
	const std::string &getSymbolText();
	
	enum LexerSymbols {
		BUILTIN_TYPE,
		TYPE_MODIFIER,
		KEYWORD_ELSE,
		KEYWORD_FOR,
		KEYWORD_IF,
		KEYWORD_WHILE,
		VOID,
		SYMBOL,
		CONSTANT_STRING,
		CONSTANT_CHAR,
		CONSTANT_NUMBER,
		LOR,
		LAND,
		EQ,
		ASSIGN,
		SHIFT,
		NUM_SYMBOLS
	};
	
	bool isEOF() { return mReader->isEOF(); }
		
	uint32_t getLineNumber() { return lineno; }
	uint32_t getLinePosition() { return charPos; }
	
protected:
	bool match( const char *match_str );
	bool matchKeyword( const char *match_str );
	void readSymbol();
	void readStringConst();
	void readCharacterConst();
	void readConst();
	void handleComment( bool singleLine );
	bool isAlpha( char c );
	bool isAlphaNum( char c );

private:
	LexerReader *mReader;
	std::string	mMatchString;
	uint32_t	lineno;
	uint32_t	charPos;
};

#endif // FILE_LEXER_H_
