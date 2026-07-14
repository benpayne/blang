#ifndef FILE_LEXER_H_
#define FILE_LEXER_H_

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "SourceLocation.h"

class LexerReader
{
public:
	LexerReader( const std::string &filename );

	char operator[]( int i );

	char popChar();
	void popChar( int count );
	char peekChar();

	bool isEOF();

	// Position of the next unconsumed character (1-based).
	const std::string &getFileName() const { return mFileName; }
	uint32_t getLine() const { return mLine; }
	uint32_t getCol() const { return mCol; }

private:
	std::ifstream mFile;
	std::string mFileName;
	uint32_t mLine = 1;
	uint32_t mCol = 1;
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
		CONSTANT_BOOL,
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
		// Phase 2 keywords start at 256 to avoid collision with ASCII
		// operator characters (e.g., '-' = 45 would collide with enum value 45)
		KEYWORD_OWN = 256,
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
		// Phase 3 tokens and keywords
		PIPE_ARROW,        // |>
		AT_SIGN,           // @
		KEYWORD_TABLE,
		KEYWORD_QUERY,
		KEYWORD_INSERT,
		KEYWORD_UPDATE,
		KEYWORD_DELETE,
		KEYWORD_WAIT,
		KEYWORD_WAIT_ALL,
		KEYWORD_CSTRING,
		KEYWORD_CARRAY,
		KEYWORD_STATIC,
		KEYWORD_INIT,
		NUM_SYMBOLS
	};
	
	bool isEOF() { return mReader->isEOF(); }

	// Line/column/location of the token at the current parse position — the
	// token the parser is about to consume. Accurate after setCurrentPos
	// backtracking because positions are frozen into the symbol list at
	// scan time. getLineNumber()/getLinePosition() delegate here so error
	// reporting is token-accurate rather than reflecting file read-ahead.
	SourceLocation getTokenLocation();
	const std::string &getFileName() const { return mReader->getFileName(); }
	uint32_t getLineNumber() { return getTokenLocation().line; }
	uint32_t getLinePosition() { return getTokenLocation().col; }

	// Gate the per-token diagnostic echo (off = quiet). Default preserves
	// the historical behavior; qcc disables it for --dump-locations.
	void setTraceEnabled( bool enabled ) { mTraceEnabled = enabled; }

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
	// Quiet by default: the per-token "Symbol …" trace is opt-in via -v
	// (driver calls setTraceEnabled). Defaulting to false keeps any Lexer
	// constructed without explicit configuration silent (R5 / FR-007).
	bool		mTraceEnabled = false;
	// Position of the first character of the token most recently scanned
	// from the file; frozen into SymbolInfo so replay/backtracking returns
	// exact positions.
	uint32_t	mScanLine = 1;
	uint32_t	mScanCol = 1;

	struct SymbolInfo
	{
		SymbolInfo( int s, std::string &str, uint32_t l, uint32_t c ) :
			symbol( s ), symbolText( str ), line( l ), col( c ) {}

		int symbol;
		std::string symbolText;
		uint32_t line;
		uint32_t col;
	};

	std::vector<SymbolInfo> mSymbolList;
	int mCurrentPos;
	
	
};

#endif // FILE_LEXER_H_
