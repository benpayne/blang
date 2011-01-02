#ifndef LEXER_H_
#define LEXER_H_

class LexerReader
{
	virtual char peekChar() = 0;
	virtual char popChar( int c = 1 ) = 0;
	virtual char operator[]( int i ) = 0;
	virtual bool isEOF() = 0;
};

class Lexer
{
public:
	Lexer( LexerReader *reader );
	
	int getSymbol();
	
	std::string getSymbolText();
	
protected:
	bool match( const char * );
	void readSymbol();
	void readStringConst();
	void readCharacterConst();
	void readConst();
	void handleComment( bool singleLine );
	
private:
	LexerReader *mReader;
	std::string	mMatchString;
	int lineno;
	int charPos;
};



#endif // LEXER_H_
