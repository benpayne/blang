#ifndef BLANG_LSP_SERVER_H_
#define BLANG_LSP_SERVER_H_

// blangd's single-threaded JSON-RPC dispatch loop. Reads framed messages
// from `in`, writes framed replies/notifications to `out` (the ONLY writer
// of `out` — every protocol byte goes through sendMessage). Streams are
// caller-supplied so tests can drive the server over stringstreams.

#include <iosfwd>
#include <string>

#include "DocumentStore.h"
#include "Json.h"

namespace lsp
{

class Server
{
public:
	Server( std::istream &in, std::ostream &out ) : mIn( in ), mOut( out ) {}

	// Run until `exit` or EOF. Returns the process exit code: 0 when exit
	// followed a shutdown request (or clean EOF), 1 otherwise (LSP spec).
	int run();

private:
	// JSON-RPC error codes used here.
	static const int kParseError = -32700;
	static const int kInvalidRequest = -32600;
	static const int kMethodNotFound = -32601;
	static const int kServerNotInitialized = -32002;

	void handleMessage( const Json &msg );
	void handleRequest( const Json &id, const std::string &method, const Json &params );
	void handleNotification( const std::string &method, const Json &params );

	Json initializeResult() const;

	// Document lifecycle -> recompile -> publishDiagnostics.
	void openDocument( const Json &params );
	void changeDocument( const Json &params );
	void closeDocument( const Json &params );
	void publishDiagnostics( const std::string &uri, const std::string &text );

	void sendResult( const Json &id, Json result );
	void sendError( const Json &id, int code, const std::string &message );
	void sendNotification( const std::string &method, Json params );
	void sendMessage( Json msg );

	std::istream &mIn;
	std::ostream &mOut;
	DocumentStore mDocs;
	bool mInitialized = false;
	bool mShutdownRequested = false;
	bool mExitRequested = false;
	int mExitCode = 0;
};

// file:// URI <-> filesystem path (percent-decoding/encoding the path part).
// Exposed for the golden-harness fixtures and unit checks.
std::string uriToPath( const std::string &uri );
std::string pathToUri( const std::string &path );

} // namespace lsp

#endif // BLANG_LSP_SERVER_H_
