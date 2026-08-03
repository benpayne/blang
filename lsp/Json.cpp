#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace lsp
{

// ---------------------------------------------------------------------------
// Object access

void Json::set( const std::string &key, Json v )
{
	if ( mKind != Kind::Object )
		return;
	for ( auto &member : mObject )
	{
		if ( member.first == key )
		{
			member.second = std::move( v );
			return;
		}
	}
	mObject.emplace_back( key, std::move( v ) );
}

bool Json::has( const std::string &key ) const
{
	if ( mKind != Kind::Object )
		return false;
	for ( const auto &member : mObject )
		if ( member.first == key )
			return true;
	return false;
}

const Json &Json::get( const std::string &key ) const
{
	static const Json null;
	if ( mKind != Kind::Object )
		return null;
	for ( const auto &member : mObject )
		if ( member.first == key )
			return member.second;
	return null;
}

// ---------------------------------------------------------------------------
// Serialization

void Json::appendEscaped( std::string &out, const std::string &s )
{
	out += '"';
	for ( unsigned char c : s )
	{
		switch ( c )
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ( c < 0x20 )
				{
					char buf[ 8 ];
					snprintf( buf, sizeof( buf ), "\\u%04x", c );
					out += buf;
				}
				else
				{
					out += (char)c;
				}
		}
	}
	out += '"';
}

void Json::serializeTo( std::string &out ) const
{
	switch ( mKind )
	{
		case Kind::Null:
			out += "null";
			break;
		case Kind::Bool:
			out += mBool ? "true" : "false";
			break;
		case Kind::Number:
		{
			double intpart;
			if ( std::modf( mNumber, &intpart ) == 0.0 &&
			     std::fabs( mNumber ) < 9.007199254740992e15 )
			{
				char buf[ 32 ];
				snprintf( buf, sizeof( buf ), "%lld", (long long)mNumber );
				out += buf;
			}
			else
			{
				char buf[ 32 ];
				snprintf( buf, sizeof( buf ), "%.17g", mNumber );
				out += buf;
			}
			break;
		}
		case Kind::String:
			appendEscaped( out, mString );
			break;
		case Kind::Array:
		{
			out += '[';
			bool first = true;
			for ( const auto &item : mArray )
			{
				if ( !first )
					out += ',';
				first = false;
				item.serializeTo( out );
			}
			out += ']';
			break;
		}
		case Kind::Object:
		{
			out += '{';
			bool first = true;
			for ( const auto &member : mObject )
			{
				if ( !first )
					out += ',';
				first = false;
				appendEscaped( out, member.first );
				out += ':';
				member.second.serializeTo( out );
			}
			out += '}';
			break;
		}
	}
}

std::string Json::serialize() const
{
	std::string out;
	serializeTo( out );
	return out;
}

// ---------------------------------------------------------------------------
// Parsing

namespace
{

struct Parser
{
	const std::string &text;
	std::size_t pos = 0;
	std::string error;

	explicit Parser( const std::string &t ) : text( t ) {}

	bool fail( const std::string &message )
	{
		if ( error.empty() )
			error = message + " at byte " + std::to_string( pos );
		return false;
	}

	void skipWhitespace()
	{
		while ( pos < text.size() &&
		        ( text[ pos ] == ' ' || text[ pos ] == '\t' ||
		          text[ pos ] == '\n' || text[ pos ] == '\r' ) )
			pos++;
	}

	bool consume( char c )
	{
		if ( pos < text.size() && text[ pos ] == c )
		{
			pos++;
			return true;
		}
		return false;
	}

	bool literal( const char *word, std::size_t len )
	{
		if ( text.compare( pos, len, word ) != 0 )
			return fail( "invalid literal" );
		pos += len;
		return true;
	}

	// Append a Unicode code point as UTF-8.
	static void appendCodepoint( std::string &out, unsigned int cp )
	{
		if ( cp < 0x80 )
		{
			out += (char)cp;
		}
		else if ( cp < 0x800 )
		{
			out += (char)( 0xC0 | ( cp >> 6 ) );
			out += (char)( 0x80 | ( cp & 0x3F ) );
		}
		else if ( cp < 0x10000 )
		{
			out += (char)( 0xE0 | ( cp >> 12 ) );
			out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
			out += (char)( 0x80 | ( cp & 0x3F ) );
		}
		else
		{
			out += (char)( 0xF0 | ( cp >> 18 ) );
			out += (char)( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
			out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
			out += (char)( 0x80 | ( cp & 0x3F ) );
		}
	}

	bool parseHex4( unsigned int &out )
	{
		if ( pos + 4 > text.size() )
			return fail( "truncated \\u escape" );
		out = 0;
		for ( int i = 0; i < 4; i++ )
		{
			char c = text[ pos + i ];
			out <<= 4;
			if ( c >= '0' && c <= '9' )
				out |= (unsigned int)( c - '0' );
			else if ( c >= 'a' && c <= 'f' )
				out |= (unsigned int)( c - 'a' + 10 );
			else if ( c >= 'A' && c <= 'F' )
				out |= (unsigned int)( c - 'A' + 10 );
			else
				return fail( "invalid \\u escape" );
		}
		pos += 4;
		return true;
	}

	bool parseString( std::string &out )
	{
		if ( !consume( '"' ) )
			return fail( "expected string" );
		while ( pos < text.size() )
		{
			unsigned char c = (unsigned char)text[ pos ];
			if ( c == '"' )
			{
				pos++;
				return true;
			}
			if ( c == '\\' )
			{
				pos++;
				if ( pos >= text.size() )
					return fail( "truncated escape" );
				char e = text[ pos++ ];
				switch ( e )
				{
					case '"':  out += '"'; break;
					case '\\': out += '\\'; break;
					case '/':  out += '/'; break;
					case 'b':  out += '\b'; break;
					case 'f':  out += '\f'; break;
					case 'n':  out += '\n'; break;
					case 'r':  out += '\r'; break;
					case 't':  out += '\t'; break;
					case 'u':
					{
						unsigned int cp;
						if ( !parseHex4( cp ) )
							return false;
						// Surrogate pair: a high surrogate must be followed
						// by \uDC00-\uDFFF; combine into one code point.
						if ( cp >= 0xD800 && cp <= 0xDBFF )
						{
							if ( pos + 1 < text.size() && text[ pos ] == '\\' &&
							     text[ pos + 1 ] == 'u' )
							{
								pos += 2;
								unsigned int low;
								if ( !parseHex4( low ) )
									return false;
								if ( low < 0xDC00 || low > 0xDFFF )
									return fail( "invalid low surrogate" );
								cp = 0x10000 + ( ( cp - 0xD800 ) << 10 ) + ( low - 0xDC00 );
							}
							else
							{
								return fail( "unpaired high surrogate" );
							}
						}
						else if ( cp >= 0xDC00 && cp <= 0xDFFF )
						{
							return fail( "unpaired low surrogate" );
						}
						appendCodepoint( out, cp );
						break;
					}
					default:
						return fail( "invalid escape" );
				}
			}
			else if ( c < 0x20 )
			{
				return fail( "unescaped control character in string" );
			}
			else
			{
				out += (char)c;
				pos++;
			}
		}
		return fail( "unterminated string" );
	}

	bool parseNumber( Json &out )
	{
		std::size_t start = pos;
		if ( pos < text.size() && text[ pos ] == '-' )
			pos++;
		while ( pos < text.size() &&
		        ( isdigit( (unsigned char)text[ pos ] ) || text[ pos ] == '.' ||
		          text[ pos ] == 'e' || text[ pos ] == 'E' ||
		          text[ pos ] == '+' || text[ pos ] == '-' ) )
			pos++;
		if ( pos == start )
			return fail( "expected number" );
		try
		{
			std::size_t used = 0;
			std::string slice = text.substr( start, pos - start );
			double v = std::stod( slice, &used );
			if ( used != slice.size() )
			{
				pos = start;
				return fail( "invalid number" );
			}
			out = Json( v );
			return true;
		}
		catch ( ... )
		{
			pos = start;
			return fail( "invalid number" );
		}
	}

	bool parseValue( Json &out, int depth )
	{
		if ( depth > Json::kMaxDepth )
			return fail( "nesting too deep" );
		skipWhitespace();
		if ( pos >= text.size() )
			return fail( "unexpected end of input" );

		char c = text[ pos ];
		if ( c == '{' )
		{
			pos++;
			out = Json::object();
			skipWhitespace();
			if ( consume( '}' ) )
				return true;
			while ( true )
			{
				skipWhitespace();
				std::string key;
				if ( !parseString( key ) )
					return false;
				skipWhitespace();
				if ( !consume( ':' ) )
					return fail( "expected ':'" );
				Json value;
				if ( !parseValue( value, depth + 1 ) )
					return false;
				out.set( key, std::move( value ) );
				skipWhitespace();
				if ( consume( ',' ) )
					continue;
				if ( consume( '}' ) )
					return true;
				return fail( "expected ',' or '}'" );
			}
		}
		if ( c == '[' )
		{
			pos++;
			out = Json::array();
			skipWhitespace();
			if ( consume( ']' ) )
				return true;
			while ( true )
			{
				Json value;
				if ( !parseValue( value, depth + 1 ) )
					return false;
				out.push( std::move( value ) );
				skipWhitespace();
				if ( consume( ',' ) )
					continue;
				if ( consume( ']' ) )
					return true;
				return fail( "expected ',' or ']'" );
			}
		}
		if ( c == '"' )
		{
			std::string s;
			if ( !parseString( s ) )
				return false;
			out = Json( s );
			return true;
		}
		if ( c == 't' )
		{
			if ( !literal( "true", 4 ) )
				return false;
			out = Json( true );
			return true;
		}
		if ( c == 'f' )
		{
			if ( !literal( "false", 5 ) )
				return false;
			out = Json( false );
			return true;
		}
		if ( c == 'n' )
		{
			if ( !literal( "null", 4 ) )
				return false;
			out = Json();
			return true;
		}
		return parseNumber( out );
	}
};

} // namespace

bool Json::parse( const std::string &text, Json &out, std::string &error )
{
	Parser p( text );
	Json result;
	if ( !p.parseValue( result, 0 ) )
	{
		out = Json();
		error = p.error;
		return false;
	}
	p.skipWhitespace();
	if ( p.pos != text.size() )
	{
		out = Json();
		p.fail( "trailing garbage" );
		error = p.error;
		return false;
	}
	out = std::move( result );
	error.clear();
	return true;
}

} // namespace lsp
