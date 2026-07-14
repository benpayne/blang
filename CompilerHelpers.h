#ifndef COMPILER_HELPERS_H_
#define COMPILER_HELPERS_H_

#include <iostream>
#include <string>

#include "SourceLocation.h"

class CompileError
{
public:
	CompileError( const SourceLocation &loc, std::string message,
		const char *filename, int lineno ) :
		mLocation( loc ), mMessage( message ),
		mFilename( filename ), mLineno( lineno )
	{}

	std::string getMessage() const;

	// Source location of the offending token, snapshotted at throw time.
	// Reporting reads this instead of interrogating the live lexer.
	const SourceLocation &getLocation() const { return mLocation; }

	// Compiler-internal C++ __FILE__:__LINE__ of the throw site — retained
	// for a future --debug-compiler mode (U2); never part of normal output.
	const char *getInternalFile() const { return mFilename; }
	int getInternalLine() const { return mLineno; }

private:
	SourceLocation mLocation;
	std::string mMessage;
	const char *mFilename;
	int mLineno;
};

// Throw a located compile error. `l` must be a Lexer (its full definition
// is available at every call site); the token location is snapshotted now.
#define COMPILE_ERROR( l, message ) \
	throw CompileError( (l).getTokenLocation(), message, __FILE__, __LINE__ )

template <typename T>
std::ostream &operator<<(std::ostream &out, const SmartPtr<T> &ptr)
{
	out << *(ptr);
	return out;
}

#endif // COMPILER_HELPERS_H_
