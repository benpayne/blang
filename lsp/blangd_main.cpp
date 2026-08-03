// blangd — the BLang language server. JSON-RPC over stdio; every protocol
// byte on stdout goes through the framing layer (the compiler frontend is
// quiet by default and traces to stderr — see Frontend.h).

#include <iostream>

#include "Server.h"

int main()
{
	// stdout is the protocol channel; keep it un-tied from cin (writeFrame
	// flushes explicitly after every frame).
	std::ios::sync_with_stdio( false );
	std::cin.tie( nullptr );

	lsp::Server server( std::cin, std::cout );
	return server.run();
}
