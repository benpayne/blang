#include "Server.h"

#include <cctype>
#include <cstdio>
#include <istream>
#include <ostream>

#include "Compile.h"
#include "Transport.h"

namespace lsp
{

// ---------------------------------------------------------------------------
// file:// URI conversion

std::string uriToPath( const std::string &uri )
{
	const std::string scheme = "file://";
	if ( uri.compare( 0, scheme.size(), scheme ) != 0 )
		return uri; // not a file URI; use verbatim
	std::string rest = uri.substr( scheme.size() );
	// Strip an authority component (file://host/path — usually empty: file:///path).
	std::size_t slash = rest.find( '/' );
	if ( slash == std::string::npos )
		return "/";
	rest = rest.substr( slash );

	// Percent-decode.
	std::string path;
	for ( std::size_t i = 0; i < rest.size(); i++ )
	{
		if ( rest[ i ] == '%' && i + 2 < rest.size() &&
		     isxdigit( (unsigned char)rest[ i + 1 ] ) &&
		     isxdigit( (unsigned char)rest[ i + 2 ] ) )
		{
			auto hex = []( char c ) -> int
			{
				if ( c >= '0' && c <= '9' ) return c - '0';
				if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
				return c - 'A' + 10;
			};
			path += (char)( hex( rest[ i + 1 ] ) * 16 + hex( rest[ i + 2 ] ) );
			i += 2;
		}
		else
		{
			path += rest[ i ];
		}
	}
	return path;
}

std::string pathToUri( const std::string &path )
{
	// Escape everything outside the RFC 3986 unreserved set and '/'.
	std::string uri = "file://";
	for ( unsigned char c : path )
	{
		if ( isalnum( c ) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~' )
		{
			uri += (char)c;
		}
		else
		{
			char buf[ 8 ];
			snprintf( buf, sizeof( buf ), "%%%02X", c );
			uri += buf;
		}
	}
	return uri;
}

// ---------------------------------------------------------------------------
// Main loop

int Server::run()
{
	std::string payload;
	while ( !mExitRequested )
	{
		try
		{
			if ( !readFrame( mIn, payload ) )
				break; // clean EOF: client went away
		}
		catch ( const TransportError & )
		{
			// The byte stream is unrecoverable after a framing error.
			return 1;
		}

		Json msg;
		std::string parseErr;
		if ( !Json::parse( payload, msg, parseErr ) )
		{
			sendError( Json(), kParseError, "parse error: " + parseErr );
			continue;
		}
		handleMessage( msg );
	}

	if ( mExitRequested )
		return mExitCode;
	// EOF without `exit`: treat like an orderly disconnect.
	return mShutdownRequested ? 0 : 1;
}

void Server::handleMessage( const Json &msg )
{
	if ( !msg.isObject() )
	{
		sendError( Json(), kInvalidRequest, "message is not an object" );
		return;
	}

	const std::string &method = msg.get( "method" ).asString();
	const Json &params = msg.get( "params" );

	if ( msg.has( "id" ) )
	{
		if ( method.empty() )
			return; // a response to a server->client request; none are sent yet
		handleRequest( msg.get( "id" ), method, params );
	}
	else
	{
		handleNotification( method, params );
	}
}

void Server::handleRequest( const Json &id, const std::string &method, const Json &params )
{
	(void)params;

	if ( method == "initialize" )
	{
		mInitialized = true;
		sendResult( id, initializeResult() );
		return;
	}

	if ( !mInitialized )
	{
		sendError( id, kServerNotInitialized, "server not initialized" );
		return;
	}

	if ( method == "shutdown" )
	{
		mShutdownRequested = true;
		sendResult( id, Json() );
		return;
	}

	sendError( id, kMethodNotFound, "method not found: " + method );
}

void Server::handleNotification( const std::string &method, const Json &params )
{
	if ( method == "exit" )
	{
		// Exit code 0 only if shutdown was requested first (LSP lifecycle).
		mExitRequested = true;
		mExitCode = mShutdownRequested ? 0 : 1;
		return;
	}

	// Per spec, notifications other than exit are dropped before initialize
	// (and after shutdown).
	if ( !mInitialized || mShutdownRequested )
		return;

	if ( method == "initialized" )
		return; // handshake complete; nothing to do
	if ( method == "textDocument/didOpen" )
	{
		openDocument( params );
		return;
	}
	if ( method == "textDocument/didChange" )
	{
		changeDocument( params );
		return;
	}
	if ( method == "textDocument/didClose" )
	{
		closeDocument( params );
		return;
	}
	// Unknown notifications are ignored (spec: MUST NOT be answered).
}

Json Server::initializeResult() const
{
	Json sync = Json::object();
	sync.set( "openClose", true );
	sync.set( "change", 1 ); // TextDocumentSyncKind.Full

	Json caps = Json::object();
	// utf-8 keeps server columns byte-accurate (LSP 3.17; fixtures stay
	// ASCII so utf-16 clients see identical positions).
	caps.set( "positionEncoding", "utf-8" );
	caps.set( "textDocumentSync", sync );

	Json info = Json::object();
	info.set( "name", "blangd" );
	info.set( "version", "0.1" );

	Json result = Json::object();
	result.set( "capabilities", caps );
	result.set( "serverInfo", info );
	return result;
}

// ---------------------------------------------------------------------------
// Documents + diagnostics

void Server::openDocument( const Json &params )
{
	const Json &doc = params.get( "textDocument" );
	const std::string &uri = doc.get( "uri" ).asString();
	if ( uri.empty() )
		return;
	mDocs.open( uri, doc.get( "text" ).asString() );
	publishDiagnostics( uri, mDocs.text( uri ) );
}

void Server::changeDocument( const Json &params )
{
	const std::string &uri = params.get( "textDocument" ).get( "uri" ).asString();
	if ( uri.empty() || !mDocs.has( uri ) )
		return;
	// Full sync: the last change entry carries the complete new text.
	const Json &changes = params.get( "contentChanges" );
	if ( !changes.isArray() || changes.size() == 0 )
		return;
	mDocs.change( uri, changes.at( changes.size() - 1 ).get( "text" ).asString() );
	publishDiagnostics( uri, mDocs.text( uri ) );
}

void Server::closeDocument( const Json &params )
{
	const std::string &uri = params.get( "textDocument" ).get( "uri" ).asString();
	if ( uri.empty() )
		return;
	mDocs.close( uri );
	// Clear the client's stale squiggles for the closed document.
	Json cleared = Json::object();
	cleared.set( "uri", uri );
	cleared.set( "diagnostics", Json::array() );
	sendNotification( "textDocument/publishDiagnostics", cleared );
}

void Server::publishDiagnostics( const std::string &uri, const std::string &text )
{
	std::vector<QLang::Diagnostic> diags = compileDocument( uriToPath( uri ), text );

	Json list = Json::array();
	for ( const auto &d : diags )
	{
		// SourceLocation is 1-based; LSP is 0-based. An unset location (0:0)
		// clamps to 0:0. Ranges are zero-length: SourceLocation has no end
		// position (documented v1 limitation, pinned by the goldens).
		int line = d.location.line > 0 ? (int)d.location.line - 1 : 0;
		int col = d.location.col > 0 ? (int)d.location.col - 1 : 0;
		Json pos = Json::object();
		pos.set( "line", line );
		pos.set( "character", col );
		Json range = Json::object();
		range.set( "start", pos );
		range.set( "end", pos );

		Json diag = Json::object();
		diag.set( "range", range );
		diag.set( "severity", d.severity == QLang::Severity::Error ? 1 : 2 );
		diag.set( "code", d.code );
		diag.set( "source", "blang" );
		diag.set( "message", d.message );

		if ( !d.notes.empty() )
		{
			Json related = Json::array();
			for ( const auto &note : d.notes )
			{
				int nline = note.location.line > 0 ? (int)note.location.line - 1 : 0;
				int ncol = note.location.col > 0 ? (int)note.location.col - 1 : 0;
				Json npos = Json::object();
				npos.set( "line", nline );
				npos.set( "character", ncol );
				Json nrange = Json::object();
				nrange.set( "start", npos );
				nrange.set( "end", npos );
				Json nloc = Json::object();
				nloc.set( "uri", note.location.file.empty()
					? uri : pathToUri( note.location.file ) );
				nloc.set( "range", nrange );
				Json entry = Json::object();
				entry.set( "location", nloc );
				entry.set( "message", note.message );
				related.push( entry );
			}
			diag.set( "relatedInformation", related );
		}

		list.push( diag );
	}

	Json params = Json::object();
	params.set( "uri", uri );
	params.set( "diagnostics", list );
	sendNotification( "textDocument/publishDiagnostics", params );
}

// ---------------------------------------------------------------------------
// Message senders — the single funnel onto the output stream.

void Server::sendResult( const Json &id, Json result )
{
	Json msg = Json::object();
	msg.set( "jsonrpc", "2.0" );
	msg.set( "id", id );
	msg.set( "result", std::move( result ) );
	sendMessage( std::move( msg ) );
}

void Server::sendError( const Json &id, int code, const std::string &message )
{
	Json err = Json::object();
	err.set( "code", code );
	err.set( "message", message );
	Json msg = Json::object();
	msg.set( "jsonrpc", "2.0" );
	msg.set( "id", id );
	msg.set( "error", std::move( err ) );
	sendMessage( std::move( msg ) );
}

void Server::sendNotification( const std::string &method, Json params )
{
	Json msg = Json::object();
	msg.set( "jsonrpc", "2.0" );
	msg.set( "method", method );
	msg.set( "params", std::move( params ) );
	sendMessage( std::move( msg ) );
}

void Server::sendMessage( Json msg )
{
	writeFrame( mOut, msg.serialize() );
}

} // namespace lsp
