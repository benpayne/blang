#include "FormatString.h"

using namespace QLang;
using namespace std;

ParsedFormatString ParsedFormatString::parse( const std::string &fmt )
{
	ParsedFormatString result;
	string current;
	int argIndex = 0;

	size_t i = 0;
	while ( i < fmt.size() )
	{
		char c = fmt[i];

		// Escape sequences: {{ -> {, }} -> }
		if ( c == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{' )
		{
			current += '{';
			i += 2;
			continue;
		}
		if ( c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}' )
		{
			current += '}';
			i += 2;
			continue;
		}

		if ( c == '{' )
		{
			// Start of placeholder
			result.literals.push_back( current );
			current.clear();

			FormatPlaceholder ph;
			ph.argIndex = argIndex++;
			i++; // skip '{'

			// Check for specifier after ':'
			if ( i < fmt.size() && fmt[i] == ':' )
			{
				i++; // skip ':'
				string spec;
				while ( i < fmt.size() && fmt[i] != '}' )
				{
					spec += fmt[i];
					i++;
				}
				if ( spec.empty() )
				{
					result.error = "empty format specifier after ':'";
					return result;
				}

				ph.specifier = spec;

				// Parse the specifier
				char last = spec.back();
				if ( last == 'x' || last == 'X' || last == 'o' || last == 'b' )
				{
					ph.type = last;
				}
				else if ( last == 'f' )
				{
					ph.type = 'f';
					if ( spec.size() >= 2 && spec[0] == '.' )
					{
						string digits = spec.substr( 1, spec.size() - 2 );
						ph.precision = 0;
						for ( char d : digits )
						{
							if ( d < '0' || d > '9' )
							{
								result.error = "invalid precision in format specifier: '" + spec + "'";
								return result;
							}
							ph.precision = ph.precision * 10 + ( d - '0' );
						}
					}
				}
				else if ( last == 'e' )
				{
					ph.type = 'e';
				}
				else
				{
					result.error = "unknown format specifier: '" + spec + "'";
					return result;
				}
			}

			if ( i >= fmt.size() || fmt[i] != '}' )
			{
				result.error = "unterminated format placeholder, expected '}'";
				return result;
			}
			i++; // skip '}'

			result.placeholders.push_back( ph );
		}
		else if ( c == '}' )
		{
			result.error = "unexpected '}' in format string (use '}}' for literal '}')";
			return result;
		}
		else
		{
			current += c;
			i++;
		}
	}

	result.literals.push_back( current );
	return result;
}
