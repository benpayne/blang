#ifndef FILE_LEXER_H_
#define FILE_LEXER_H_

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

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
	
	int peekSymbol();
	int getSymbol();
	const std::string &getSymbolText();
	
	int getCurrentPos();
	void setCurrentPos( int pos );
	
	enum LexerSymbols {
		BUILTIN_TYPE,
		BOOL,
		TYPE_MODIFIER,
		KEYWORD_ELSE,
		KEYWORD_FOR,
		KEYWORD_IF,
		KEYWORD_WHILE,
		KEYWORD_RETURN,
		KEYWORD_FN,
		KEYWORD_STRUCT,
		KEYWORD_IMPL,
		KEYWORD_SELF,
		KEYWORD_PROTOCOL,
		KEYWORD_MATCH,
		KEYWORD_IMPORT,
		KEYWORD_PUB,
		KEYWORD_BREAK,
		KEYWORD_CONTINUE,
		KEYWORD_ENUM,
		KEYWORD_IN,
		VOID,
		SYMBOL,
		CONSTANT_STRING,
		CONSTANT_CHAR,
		CONSTANT_NUMBER,
		CONSTANT_FLOAT,
		LOR,
		LAND,
		EQ,
		ASSIGN,
		SHIFT,
		ELLIPSIS,
		RANGE,
		ARROW,
		WILDCARD,
		QUESTION_MARK,
		KEYWORD_OWN,
		KEYWORD_SHARED,
		KEYWORD_SYNC,
		KEYWORD_SPAWN,
		KEYWORD_CHAN,
		KEYWORD_ASYNC,
		KEYWORD_AWAIT,
		KEYWORD_ON,
		KEYWORD_REQUIRES,
		KEYWORD_ENSURES,
		KEYWORD_TEST,
		KEYWORD_ASSERT,
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
	bool readConst();
	void handleComment( bool singleLine );
	bool isAlpha( char c );
	bool isAlphaNum( char c );
	int getSymbolInternal();
	int getSymbolFromFile();

private:
	LexerReader *mReader;
	std::string	mMatchString;
	int			mLastSym;
	uint32_t	lineno;
	uint32_t	charPos;
	
	struct SymbolInfo 
	{
		SymbolInfo( int s, std::string &str ) : symbol( s ), symbolText( str ) {}
		
		int symbol;
		std::string symbolText;
	};
	
	std::vector<SymbolInfo> mSymbolList;
	int mCurrentPos;
	
	
};

#endif // FILE_LEXER_H_
