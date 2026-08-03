#ifndef BLANG_LSP_STRING_LEXER_READER_H_
#define BLANG_LSP_STRING_LEXER_READER_H_

// LexerReader over an in-memory buffer: the LSP server compiles editor
// documents (which may be unsaved) without touching the filesystem. Mirrors
// the file reader's semantics exactly — including returning EOF characters
// past the end and the line/column counting funnel through popChar().

#include <string>

#include "../FileLexer.h"

namespace lsp
{

class StringLexerReader : public LexerReader
{
public:
	// `filename` is what diagnostics report (the document's path).
	StringLexerReader( const std::string &text, const std::string &filename )
		: mText( text )
	{
		mFileName = filename;
	}

	char operator[]( int i ) override
	{
		std::size_t at = mPos + (std::size_t)i;
		return at < mText.size() ? mText[ at ] : kEof;
	}

	char popChar() override
	{
		if ( mPos >= mText.size() )
			return kEof;
		char c = mText[ mPos++ ];
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

	void popChar( int count ) override
	{
		for ( int i = 0; i < count; i++ )
			popChar();
	}

	char peekChar() override
	{
		return mPos < mText.size() ? mText[ mPos ] : kEof;
	}

	bool isEOF() override { return mPos >= mText.size(); }

private:
	// Same value istream::get()/peek() yield at EOF once truncated to char.
	static const char kEof = (char)-1;

	std::string mText;
	std::size_t mPos = 0;
};

} // namespace lsp

#endif // BLANG_LSP_STRING_LEXER_READER_H_
