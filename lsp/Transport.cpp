#include "Transport.h"

#include <cctype>
#include <istream>
#include <ostream>

namespace lsp
{

const std::size_t kMaxFrameBytes = 16u * 1024u * 1024u;

// Read a header line terminated by \r\n (tolerates bare \n). Returns false
// on EOF with nothing read.
static bool readHeaderLine( std::istream &in, std::string &line )
{
	line.clear();
	char c;
	while ( in.get( c ) )
	{
		if ( c == '\n' )
		{
			if ( !line.empty() && line.back() == '\r' )
				line.pop_back();
			return true;
		}
		line += c;
	}
	return !line.empty();
}

bool readFrame( std::istream &in, std::string &payload )
{
	payload.clear();

	// --- Headers ---
	long contentLength = -1;
	std::string line;
	bool sawHeader = false;
	while ( true )
	{
		if ( !readHeaderLine( in, line ) )
		{
			if ( sawHeader )
				throw TransportError{ "unexpected EOF inside frame headers" };
			return false; // clean EOF between messages
		}
		if ( line.empty() )
			break; // blank line ends the header block
		sawHeader = true;

		const std::string prefix = "Content-Length:";
		if ( line.compare( 0, prefix.size(), prefix ) == 0 )
		{
			std::size_t i = prefix.size();
			while ( i < line.size() && line[ i ] == ' ' )
				i++;
			if ( i >= line.size() || !isdigit( (unsigned char)line[ i ] ) )
				throw TransportError{ "malformed Content-Length header: " + line };
			contentLength = 0;
			for ( ; i < line.size() && isdigit( (unsigned char)line[ i ] ); i++ )
			{
				contentLength = contentLength * 10 + ( line[ i ] - '0' );
				if ( (std::size_t)contentLength > kMaxFrameBytes )
					throw TransportError{ "Content-Length exceeds frame cap" };
			}
			if ( i != line.size() )
				throw TransportError{ "malformed Content-Length header: " + line };
		}
		// Other headers (Content-Type) are ignored per the LSP spec.
	}

	if ( contentLength < 0 )
		throw TransportError{ "frame headers missing Content-Length" };

	// --- Body ---
	payload.resize( (std::size_t)contentLength );
	in.read( &payload[ 0 ], contentLength );
	if ( in.gcount() != contentLength )
		throw TransportError{ "truncated frame payload" };
	return true;
}

void writeFrame( std::ostream &out, const std::string &payload )
{
	out << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
	out.flush();
}

} // namespace lsp
