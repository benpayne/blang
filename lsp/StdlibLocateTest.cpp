// modules-v2-lsp U2 — StdlibLocateTest: blangd's stdlib discovery rule
// (lsp/StdlibLocation), independent of the server. Usage:
//   stdlibLocateTest <source stdlib dir> <existing dir without sys.b>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <unistd.h>

#include "StdlibLocation.h"

using lsp::StdlibLocation;
using lsp::locateStdlib;

static int gFailures = 0;
#define CHECK( cond ) \
	do { if ( !( cond ) ) { std::fprintf( stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); gFailures++; } } while ( false )

int main( int argc, char *argv[] )
{
	if ( argc < 3 )
	{
		std::fprintf( stderr, "FAIL usage: stdlibLocateTest <stdlib> <dir-without-sys.b>\n" );
		return 1;
	}
	const std::string good = argv[1];
	const std::string noSys = argv[2];
	const std::string exeDir = "/nonexistent-exe-dir";

	// (a) precedence
	{
		StdlibLocation l = locateStdlib( good, noSys, exeDir );
		CHECK( l.route == StdlibLocation::Route::kInitOptions && l.path == good && l.usable );
		l = locateStdlib( "", good, exeDir );
		CHECK( l.route == StdlibLocation::Route::kEnv && l.path == good && l.usable );
		l = locateStdlib( "", "", exeDir );
		CHECK( l.route == StdlibLocation::Route::kRelative && l.path == exeDir + "/stdlib" );
		CHECK( !l.usable && l.missing == "directory" );
	}
	// (b) no fall-through: a set-but-unusable route stays selected
	{
		StdlibLocation l = locateStdlib( noSys, good, exeDir );
		CHECK( l.route == StdlibLocation::Route::kInitOptions && !l.usable );
		l = locateStdlib( "", noSys, exeDir );
		CHECK( l.route == StdlibLocation::Route::kEnv && !l.usable );
	}
	// (c) usability
	{
		CHECK( locateStdlib( good, "", "" ).missing.empty() );
		CHECK( locateStdlib( noSys, "", "" ).missing == "sys.b" );
		CHECK( locateStdlib( "/nonexistent/u2/stdlib", "", "" ).missing == "directory" );
		CHECK( locateStdlib( good + "/sys.b", "", "" ).missing == "directory" );   // a regular file
	}
	// (d) first missing in base order
	{
		char tmpl[] = "/tmp/u2_stdlib_XXXXXX";
		char *dir = mkdtemp( tmpl );
		CHECK( dir != nullptr );
		if ( dir != nullptr )
		{
			std::string d = dir;
			std::ofstream( d + "/sys.b" ) << "\n";
			std::ofstream( d + "/buffer.b" ) << "\n";
			StdlibLocation l = locateStdlib( d, "", "" );
			CHECK( !l.usable && l.missing == "collections.b" );
			unlink( ( d + "/sys.b" ).c_str() );
			unlink( ( d + "/buffer.b" ).c_str() );
			rmdir( d.c_str() );
		}
	}
	// (e) the exact warning text
	{
		StdlibLocation l = locateStdlib( "", "/nonexistent/u2/stdlib", "" );
		CHECK( lsp::stdlibNotFoundMessage( l ) ==
			"stdlib not found: BLANG_STDLIB_DIR '/nonexistent/u2/stdlib' is not a usable stdlib "
			"directory (missing directory); set initializationOptions.stdlibPath or "
			"BLANG_STDLIB_DIR, or install stdlib/ next to blangd — this file is checked "
			"without the stdlib" );
	}

	if ( gFailures != 0 )
	{
		std::fprintf( stderr, "StdlibLocateTest: %d FAILURE(S)\n", gFailures );
		return 1;
	}
	std::printf( "StdlibLocateTest: OK\n" );
	return 0;
}
