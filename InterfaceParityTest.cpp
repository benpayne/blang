// modules-v2-lsp U3 (FR-005; plan D-3 parity argument) — InterfaceParityTest:
// the dependency interface blangd derives IN MEMORY (lsp::buildProjectContext:
// the dep's own bcc plan through ProjectCompile::run + BmodEmitter::emit with a
// source-text provider) must be BYTE-IDENTICAL to the .bmod a cold `bcc build`
// wrote for that dependency. Usage (run by tools/lsp_diag_corpus.py --corpus
// project after it has cold-built the scratch tree):
//   interfaceParityTest <built test_build dir>

#include <cstdio>
#include <fstream>
#include <glob.h>
#include <set>
#include <sstream>
#include <string>

#include "lsp/DocumentStore.h"
#include "lsp/ProjectContext.h"

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::fprintf( stderr, "usage: interfaceParityTest <built test_build dir>\n" );
		return 2;
	}
	glob_t g;
	std::string pat = std::string( argv[1] ) + "/*/main.b";
	if ( glob( pat.c_str(), 0, nullptr, &g ) != 0 )
	{
		std::fprintf( stderr, "FAIL no projects under %s\n", argv[1] );
		return 1;
	}
	lsp::EmptyOverlay overlay;
	std::set<std::string> checked;
	int failures = 0;
	for ( size_t i = 0; i < g.gl_pathc; i++ )
	{
		std::string main = g.gl_pathv[i];
		std::ifstream in( main );
		std::ostringstream ss;
		ss << in.rdbuf();
		lsp::ProjectContextResult pc = lsp::buildProjectContext( main, ss.str(), overlay );
		for ( const auto &iface : pc.interfaces )
		{
			if ( !checked.insert( iface.path ).second )
				continue;
			std::ifstream b( iface.path, std::ios::binary );
			if ( !b.is_open() )
			{
				std::printf( "FAIL %s: bcc wrote no .bmod there\n", iface.path.c_str() );
				failures++;
				continue;
			}
			std::ostringstream bs;
			bs << b.rdbuf();
			if ( bs.str() == iface.text )
				std::printf( "PASS %s (%zu bytes)\n", iface.path.c_str(), iface.text.size() );
			else
			{
				std::printf( "FAIL %s: derived interface differs from bcc's .bmod\n", iface.path.c_str() );
				failures++;
			}
		}
	}
	globfree( &g );
	std::printf( "interface parity: %zu interfaces, %d failed\n", checked.size(), failures );
	return ( failures == 0 && !checked.empty() ) ? 0 : 1;
}
