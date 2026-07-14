#ifndef BLANG_SOURCE_LOCATION_H_
#define BLANG_SOURCE_LOCATION_H_

#include <cstdint>
#include <string>

// A source position: file name plus 1-based line and column of a token's
// first character. Default-constructed state (line == 0, col == 0) means
// "unset" and must never escape the parser onto a reachable AST node
// (see spec FR-004). Plain value type — freely copyable, no RefCount.
struct SourceLocation
{
	std::string file;
	uint32_t line = 0;
	uint32_t col = 0;

	SourceLocation() {}
	SourceLocation( const std::string &f, uint32_t l, uint32_t c ) :
		file( f ), line( l ), col( c ) {}

	bool isSet() const { return line != 0 && col != 0; }
};

#endif // BLANG_SOURCE_LOCATION_H_
