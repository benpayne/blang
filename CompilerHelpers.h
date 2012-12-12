#ifndef COMPILER_HELPERS_H_
#define COMPILER_HELPERS_H_

#include <iostream>
#include <string>

class CompileError
{
public:
	CompileError( Lexer &l, std::string message, const char *filename, int lineno ) : 
		mLexer( l ), mMessage( message ), 
		mFilename( filename ), mLineno( lineno )
	{}
	
	std::string getMessage() const;
	
private:
	Lexer	&mLexer;
	std::string mMessage;
	const char *mFilename;
	int mLineno;
};

#define COMPILE_ERROR( l, message ) \
	throw CompileError( l, message, __FILE__, __LINE__ )
	
#if 0
#define COMPILE_ERROR( l, message ) \
do { \
	cerr << "Compiler Error in " << __FILE__ << ":" << __LINE__ << endl; \
	cerr << message << " at line: " << l.getLineNumber() << endl; \
	exit( -1 ); \
} while( false )
#endif

template <typename T>
std::ostream &operator<<(std::ostream &out, const SmartPtr<T> &ptr)
{
	out << *(ptr);
	return out;
}

#endif // COMPILER_HELPERS_H_
