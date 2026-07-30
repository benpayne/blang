// libFuzzer harness for the BLang lexer + recursive-descent parser (U5, REQ-006).
//
// Drives arbitrary input bytes through the SAME path qcc uses:
//   LexerReader(file) -> Lexer -> QLang::Module::Parse(lexer, scope)
// A controlled CompileError (bad program) is a normal outcome, not a crash; only
// a signal / ASan / UBSan fault is a libFuzzer crash. This is what proves the
// parser is memory-safe on malformed input.
//
// qcc.cpp is compiled into this target with its main() removed (BLANG_FUZZ_HARNESS)
// so Module::Parse and the gScope/gDiag globals (Frontend.h) are reused.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <ostream>
#include <string>
#include <unistd.h>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"
#include "CompilerHelpers.h"
#include "DiagnosticEngine.h"
#include "Frontend.h"

using namespace QLang;

// This target hunts parser CRASHES / undefined behavior, not leaks (memory-leak
// verification is test_codegen.sh --leak-check, U4). Disable LeakSanitizer so
// per-input AST allocations (and the one-time global scope) do not fail the
// crash-free corpus replay; ASan's memory-error detection stays on.
extern "C" const char *__asan_default_options( void )
{
	return "detect_leaks=0";
}

// Defined at file scope in qcc.cpp (its main() is compiled out here).

static std::string g_tmp_path;

static void fuzz_init_once()
{
	static bool done = false;
	if ( done )
		return;
	done = true;

	// Silence parser diagnostics by routing them to a null stream — WITHOUT
	// touching process stderr (which must stay open so libFuzzer/ASan crash
	// reports remain visible).
	static std::ostream null_out( nullptr );
	static DiagnosticEngine diag( null_out );
	gDiag = &diag;

	// The parser also emits some legacy debug lines to stdout on certain inputs;
	// send stdout to /dev/null so the campaign runs clean and fast. libFuzzer and
	// ASan write their reports to stderr, which stays open.
	if ( freopen( "/dev/null", "w", stdout ) == nullptr ) { /* ignore */ }

	// The full builtin global scope, shared with qcc via createGlobalScope().
	// The static SmartPtr OWNS it (see Frontend.h): per-input file scopes
	// retain/release their parent, so an unowned scope would be freed after
	// the first input.
	static SmartPtr<Scope> g_global_scope = createGlobalScope();
	gScope = (Scope *)g_global_scope;

	char tmpl[] = "/tmp/blang_fuzz_XXXXXX";
	int fd = mkstemp( tmpl );
	if ( fd >= 0 )
		close( fd );
	g_tmp_path = tmpl;
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t *data, size_t size )
{
	fuzz_init_once();
	if ( g_tmp_path.empty() )
		return 0;

	// The lexer reads from a file path; stage the input bytes.
	FILE *f = fopen( g_tmp_path.c_str(), "wb" );
	if ( f == nullptr )
		return 0;
	if ( size > 0 )
		fwrite( data, 1, size, f );
	fclose( f );

	try
	{
		LexerReader reader( g_tmp_path );
		Lexer l( &reader );
		// Fresh module scope per input; SmartPtr frees the whole AST on return
		// (Module/Scope are RefCount), so corpus replay stays leak-clean.
		SmartPtr<Scope> fileScope = new Scope( Scope::kScope_Module );
		fileScope->setParent( gScope );
		SmartPtr<Module> mod = Module::Parse( l, (Scope *)fileScope );
		(void)mod;
	}
	catch ( const CompileError & )
	{
		// Located parse error on a bad program — expected, not a crash.
	}
	catch ( ... )
	{
		// Any other controlled C++ exception is also not a memory fault.
	}
	return 0;
}
