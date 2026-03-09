#ifndef BLANG_FORMAT_STRING_H_
#define BLANG_FORMAT_STRING_H_

#include <string>
#include <vector>
#include <stdexcept>

namespace QLang {

struct FormatPlaceholder {
	int argIndex;           // 0-based positional index
	std::string specifier;  // text after ':', empty for plain {}
	char type = '\0';       // parsed: 'x','X','o','b','e','f', or '\0' (default)
	int precision = -1;     // parsed: N from .Nf, or -1
};

struct ParsedFormatString {
	std::vector<std::string> literals;          // N+1 literal segments
	std::vector<FormatPlaceholder> placeholders; // N placeholders
	std::string error;                          // non-empty if parse failed

	static ParsedFormatString parse( const std::string &fmt );
};

} // namespace QLang

#endif // BLANG_FORMAT_STRING_H_
