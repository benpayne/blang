#include "Server.h"

#include <cctype>
#include <cstdio>
#include <istream>
#include <ostream>

#include "AstLocator.h"
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

	if ( method == "textDocument/definition" )
	{
		definition( id, params );
		return;
	}

	if ( method == "textDocument/hover" )
	{
		hover( id, params );
		return;
	}

	if ( method == "textDocument/documentSymbol" )
	{
		documentSymbol( id, params );
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
	caps.set( "definitionProvider", true );
	caps.set( "hoverProvider", true );
	caps.set( "documentSymbolProvider", true );

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
	mCompiles.erase( uri );
	// Clear the client's stale squiggles for the closed document.
	Json cleared = Json::object();
	cleared.set( "uri", uri );
	cleared.set( "diagnostics", Json::array() );
	sendNotification( "textDocument/publishDiagnostics", cleared );
}

void Server::publishDiagnostics( const std::string &uri, const std::string &text )
{
	mCompiles[ uri ] = compileDocument( uriToPath( uri ), text );
	const std::vector<QLang::Diagnostic> &diags = mCompiles[ uri ].diagnostics;

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
// Navigation

const QLang::Statement *Server::nodeAt( const Json &params )
{
	const std::string &uri = params.get( "textDocument" ).get( "uri" ).asString();
	auto it = mCompiles.find( uri );
	if ( it == mCompiles.end() || it->second.module == nullptr )
		return nullptr; // unopened document

	// LSP positions are 0-based; SourceLocation is 1-based.
	const Json &pos = params.get( "position" );
	uint32_t line = (uint32_t)pos.get( "line" ).asInt() + 1;
	uint32_t col = (uint32_t)pos.get( "character" ).asInt() + 1;
	return QLang::AstLocator::locate( (QLang::Module *)it->second.module, line, col );
}

void Server::definition( const Json &id, const Json &params )
{
	const QLang::Statement *node = nodeAt( params );
	SourceLocation target;
	if ( !QLang::AstLocator::definitionLocation( node, target ) )
	{
		sendResult( id, Json() ); // no definition pointer / builtin target
		return;
	}

	// Single-file semantics: the definition is in this document (target.file
	// equals the compiled path). Zero-length range at the definition site.
	Json start = Json::object();
	start.set( "line", (int)target.line - 1 );
	start.set( "character", (int)target.col - 1 );
	Json range = Json::object();
	range.set( "start", start );
	range.set( "end", start );
	Json location = Json::object();
	location.set( "uri", pathToUri( target.file ) );
	location.set( "range", range );
	sendResult( id, location );
}

// LSP SymbolKind values used below.
namespace symbolkind
{
	const int kMethod = 6;
	const int kField = 8;
	const int kEnum = 10;
	const int kInterface = 11;
	const int kFunction = 12;
	const int kEnumMember = 22;
	const int kStruct = 23;
}

// One DocumentSymbol node: range == selectionRange, zero-length at the
// declaration's location (SourceLocation has no end position — v1).
static Json makeSymbol( const std::string &name, int kind, const SourceLocation &loc )
{
	Json start = Json::object();
	start.set( "line", loc.line > 0 ? (int)loc.line - 1 : 0 );
	start.set( "character", loc.col > 0 ? (int)loc.col - 1 : 0 );
	Json range = Json::object();
	range.set( "start", start );
	range.set( "end", start );

	Json sym = Json::object();
	sym.set( "name", name );
	sym.set( "kind", kind );
	sym.set( "range", range );
	sym.set( "selectionRange", range );
	return sym;
}

static void addChild( Json &parent, Json child )
{
	if ( !parent.has( "children" ) )
		parent.set( "children", Json::array() );
	Json children = parent.get( "children" );
	children.push( std::move( child ) );
	parent.set( "children", std::move( children ) );
}

void Server::documentSymbol( const Json &id, const Json &params )
{
	const std::string &uri = params.get( "textDocument" ).get( "uri" ).asString();
	auto it = mCompiles.find( uri );
	if ( it == mCompiles.end() || it->second.module == nullptr )
	{
		sendResult( id, Json() );
		return;
	}
	const QLang::Module *mod = (QLang::Module *)it->second.module;

	// Same category order as --dump-locations: structs, enums, protocols,
	// functions, tests; source order within each.
	Json list = Json::array();
	for ( const auto &s : mod->getStructList() )
	{
		Json node = makeSymbol( s->getName(), symbolkind::kStruct, s->getLocation() );
		for ( const auto &field : s->getFields() )
			addChild( node, makeSymbol( field->getName(), symbolkind::kField,
				field->getLocation() ) );
		for ( const auto &method : s->getMethods() )
			addChild( node, makeSymbol( method->getName(), symbolkind::kMethod,
				method->getLocation() ) );
		list.push( std::move( node ) );
	}
	for ( const auto &e : mod->getEnumList() )
	{
		Json node = makeSymbol( e->getName(), symbolkind::kEnum, e->getLocation() );
		for ( const auto &variant : e->getVariants() )
			addChild( node, makeSymbol( variant.mName, symbolkind::kEnumMember,
				variant.mLocation ) );
		list.push( std::move( node ) );
	}
	for ( const auto &p : mod->getProtocolList() )
	{
		Json node = makeSymbol( p->getName(), symbolkind::kInterface, p->getLocation() );
		for ( const auto &method : p->getRequiredMethods() )
			addChild( node, makeSymbol( method->getName(), symbolkind::kMethod,
				method->getLocation() ) );
		list.push( std::move( node ) );
	}
	for ( const auto &f : mod->getFunctionList() )
		list.push( makeSymbol( f->getName(), symbolkind::kFunction, f->getLocation() ) );
	for ( const auto &t : mod->getTestBlocks() )
		list.push( makeSymbol( "test \"" + t->getName() + "\"",
			symbolkind::kFunction, t->getLocation() ) );

	sendResult( id, list );
}

void Server::hover( const Json &id, const Json &params )
{
	std::string text = QLang::AstLocator::hoverText( nodeAt( params ) );
	if ( text.empty() )
	{
		sendResult( id, Json() ); // nothing known under the cursor
		return;
	}
	Json contents = Json::object();
	contents.set( "kind", "markdown" );
	contents.set( "value", "```blang\n" + text + "\n```" );
	Json result = Json::object();
	result.set( "contents", contents );
	sendResult( id, result );
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
