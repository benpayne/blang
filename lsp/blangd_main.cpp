// blangd — the BLang language server. JSON-RPC over stdio; every protocol
// byte on stdout goes through the framing layer (the compiler frontend is
// quiet by default and traces to stderr — see Frontend.h).

#include <iostream>

#include "Server.h"
#include "../StdlibSelect.h"

int main( int argc, char *argv[] )
{
	// stdout is the protocol channel; keep it un-tied from cin (writeFrame
	// flushes explicitly after every frame).
	std::ios::sync_with_stdio( false );
	std::cin.tie( nullptr );

	// The blangd-relative stdlib route (<blangd-dir>/stdlib) locates the binary
	// with bcc's own rule (modules-v2-lsp U2).
	lsp::Server server( std::cin, std::cout,
		BlangStdlib::executableDir( argc > 0 ? argv[0] : "" ) );
	return server.run();
}
