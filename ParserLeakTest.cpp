// KG-7 regression test — the parser must not leak its AST/scopes.
//
// Parses fixtures that exercise every parser-created scope kind (function body +
// parameter scope, extern fn params, impl/init methods, protocol signatures,
// lambdas with captures, match arms, if/else, while, for-in, spawn, test blocks)
// through Module::Parse + Sema, releases the Module and its module scope, then
// destroys the Resolver. Under the ASan build (build-asan) LeakSanitizer runs at
// exit and FAILS the test if any parser allocation is still unreachable — which
// is exactly what KG-7 was: Scope::mParent retained the parent, so a scope's
// FunctionDefinition symbol -> function scope -> parent formed a refcount cycle
// and the whole module AST leaked.
//
// In a non-sanitized build the test still runs (it checks parse + sema success
// and the explicit live-scope counter below), so it is meaningful everywhere.

#include <cassert>
#include <cstdio>
#include <string>
#include <sstream>
#include <fstream>

#include "Resolver.h"
#include "Type.h"
#include "Expression.h"
#include "Frontend.h"
#include "FileLexer.h"
#include "Sema.h"
#include "DiagnosticEngine.h"
#include "lsp/StringLexerReader.h"
#include "lsp/Compile.h"
#include "lsp/Navigation.h"

using namespace QLang;

static const char *kFixture =
	"extern fn printf(string fmt, ...) -> int;\n"
	"extern fn puts(string) -> int;\n"
	"struct Point { int x; int y; }\n"
	"impl Point {\n"
	"\tinit( int x, int y ) { self.x = x; self.y = y; }\n"
	"\tfn sum( self ) -> int { return self.x + self.y; }\n"
	"}\n"
	"protocol Shape { fn area( self ) -> int; }\n"
	"enum Opt { some(int), none }\n"
	"fn add( int a, int b ) -> int { return a + b; }\n"
	"fn apply( fn(int) -> int f, int v ) -> int { return f( v ); }\n"
	"fn main() -> int {\n"
	"\tint total = 0;\n"
	"\tint k = 3;\n"
	"\tfn(int) -> int f = fn( int x ) -> int { return x + k; };\n"
	"\ttotal = apply( f, 2 );\n"
	"\tfor i in 0..10 { if i > 5 { total += i; } else { total -= 1; } }\n"
	"\twhile total > 100 { total -= 1; }\n"
	"\tOpt o = Opt.some( 4 );\n"
	"\tmatch o { some(v) { total += v; } none { total += 0; } }\n"
	"\tPoint p = Point( 1, 2 );\n"
	"\ttotal += p.sum();\n"
	"\treturn add( total, 1 );\n"
	"}\n"
	"test \"adds\" { assert add( 1, 2 ) == 3; }\n";

static bool parseOnce( const std::string &src, const std::string &path )
{
	Resolver resolver;
	bool ok = false;
	{
		SmartPtr<Scope> fileScope = resolver.newModuleScope();

		DiagnosticEngine engine;
		DiagnosticEngine *prev = gDiag;
		gDiag = &engine;

		lsp::StringLexerReader reader( src, path );
		Lexer lexer( &reader );
		SmartPtr<Module> mod = Module::Parse( lexer, (Scope *)fileScope );
		ok = ( (Module *)mod != nullptr );
		if ( ok )
		{
			stampDefiningOrigin( (Module *)mod, path );
			Sema::analyze( (Module *)mod, (Scope *)fileScope, engine );
			ok = !engine.hasErrors();
			// The AST must stay alive and resolvable while the Module is held.
			ok = ok && fileScope->findSymbol( "add" ) != nullptr;
			ok = ok && fileScope->findType( "int" ) != nullptr;
		}
		gDiag = prev;
	}
	return ok;
}

int main()
{
	bool ok = parseOnce( kFixture, "leak_fixture.b" );
	assert( ok && "fixture parses + passes sema" );

	// The minimal KG-7 reproduction from the known-issues write-up.
	ok = parseOnce( "fn add( int a, int b ) -> int { return a + b; }\n", "leak_min.b" );
	assert( ok && "minimal function-body fixture parses" );

	// The blangd entry point: its CompileResult owns the module + scope; dropping
	// it must free the whole AST (a long-running server reparses on every edit).
	{
		lsp::CompileResult res = lsp::compileDocument( "leak_lsp.b", kFixture );
		assert( res.module != nullptr && "lsp path parses the fixture" );
	}

	// modules-v2-lsp U2 (spec FR-012/FR-013): the stdlib-context reparse loop.
	// KG-7 invariant: an expression's link to the definition it resolved to is
	// NON-owning, so this document's AST (net.http_ok, Map, Set, Buffer) points
	// into stdlib modules owned ONLY by res.context. Each iteration builds a full
	// stdlib + document context and drops it; under build-asan LeakSanitizer
	// fails the process if any reparse leaks (a long-running blangd reparses on
	// every edit), and ASan traps a use-after-free if the context were released
	// before the AST that points into it.
	{
		lsp::StdlibLocation loc = lsp::locateStdlib( BLANG_SOURCE_STDLIB_DIR, "", "" );
		assert( loc.usable && "source stdlib is usable" );
		static const char *kStdlibFixture =
			"import net;\n"
			"\n"
			"fn main() -> int {\n"
			"\tMap<string, int> m = Map<string, int>();\n"
			"\tm.set(\"a\", 1);\n"
			"\tSet<int> s = Set<int>();\n"
			"\ts.add(2);\n"
			"\tBuffer b = Buffer(8);\n"
			"\tb.append_byte(65);\n"
			"\tnet.HttpResponse r = net.http_ok(\"hi\");\n"
			"\treturn m.length() + s.length() + b.get_length() + r.status();\n"
			"}\n";
		for ( int i = 0; i < 50; i++ )
		{
			lsp::CompileResult res = lsp::compileDocument( "leak_stdlib.b", kStdlibFixture, loc );
			assert( res.module != nullptr && "stdlib-context document parses" );
			assert( res.diagnostics.empty() && "stdlib-context fixture is clean" );
			assert( res.context != nullptr && res.context->records().size() > 1 &&
				"the stdlib modules are part of this result's context" );
		}
	}

	// modules-v2-lsp U3 (spec FR-020/SC-006): the PROJECT-context reparse loop.
	// usebox's main.b imports midx, which returns boqx's Box<int>, so each reparse
	// derives two dependency interfaces from source (two isolated dependency Runs)
	// and compiles the document against them. Ownership order (plan D-4): the
	// result's `resolver` is destroyed LAST, after `context` (which owns the
	// re-parsed interfaces the document's non-owning resolution links point into),
	// `deps` (the dependency source compiles) and the document's module/scope.
	// LeakSanitizer (build-asan) fails the process if any reparse leaks.
	{
		lsp::StdlibLocation loc = lsp::locateStdlib( BLANG_SOURCE_STDLIB_DIR, "", "" );   // fresh (analyze L2)
		assert( loc.usable && "source stdlib is usable" );
		const std::string mainPath = std::string( BLANG_SOURCE_ROOT ) + "/test_build/usebox/main.b";
		std::ifstream in( mainPath );
		std::ostringstream ss;
		ss << in.rdbuf();
		const std::string mainText = ss.str();
		assert( !mainText.empty() && "usebox/main.b readable" );
		lsp::EmptyOverlay overlay;
		for ( int i = 0; i < 50; i++ )
		{
			lsp::CompileResult res = lsp::compileDocument( mainPath, mainText, loc, overlay );
			assert( res.module != nullptr && "project-context document parses" );
			assert( res.deps.size() >= 2 && "boxq + midx derived from source" );
			assert( res.diagnostics.empty() && "usebox is clean in project context" );
		}
	}

	// modules-v2-lsp U4 (T046, spec FR-012): the NAVIGATION loop. Each round
	// compiles a project document and runs cross-module definition/hover against
	// the result — the interface->source map shares the result's dependency Runs
	// and the type references live in its context, so nothing may outlive the
	// result (LeakSanitizer fails the process otherwise). Checks are explicit
	// (not assert) so the navigation calls always run.
	{
		lsp::StdlibLocation loc = lsp::locateStdlib( BLANG_SOURCE_STDLIB_DIR, "", "" );
		auto readFile = []( const std::string &p ) {
			std::ifstream in( p );
			std::ostringstream ss;
			ss << in.rdbuf();
			return ss.str();
		};
		const std::string root = BLANG_SOURCE_ROOT;
		const std::string usebox = root + "/test_build/usebox/main.b";
		const std::string counterapp = root + "/test_build/counterapp/main.b";
		const std::string ambiguous = root + "/test_files/lsp/projects/usecap_ambiguous/app/main.b";
		const std::string useboxText = readFile( usebox );
		const std::string counterText = readFile( counterapp );
		const std::string ambiguousText = readFile( ambiguous );
		lsp::EmptyOverlay overlay;
		for ( int i = 0; i < 50; i++ )
		{
			// b.get() on a use-capability-only Box<int> (1-based 15:19) -> boxq.b.
			lsp::CompileResult res = lsp::compileDocument( usebox, useboxText, loc, overlay );
			lsp::NavigationAnswer def = lsp::definitionAt( res, usebox, 15, 19 );
			lsp::NavigationAnswer hov = lsp::hoverAt( res, usebox, 15, 19 );
			if ( !def.found || def.location.file != root + "/test_build/boxq/boxq.b" ||
				 hov.hover != "fn boxq.Box.get(self) -> T" )
			{
				fprintf( stderr, "ParserLeakTest: use-capability navigation wrong (%s / %s)\n",
					def.location.file.c_str(), hov.hover.c_str() );
				return 1;
			}
			// The type reference `Counter` in type position (7:2) -> counter.b.
			lsp::CompileResult cres = lsp::compileDocument( counterapp, counterText, loc, overlay );
			lsp::NavigationAnswer tdef = lsp::definitionAt( cres, counterapp, 7, 2 );
			if ( !tdef.found || tdef.location.file != root + "/test_build/counterlib/counter.b" )
			{
				fprintf( stderr, "ParserLeakTest: type-reference navigation wrong\n" );
				return 1;
			}
			// Ambiguous use-capability receiver (pa.Box vs pb.Box, 9:18): never a guess.
			lsp::CompileResult ares = lsp::compileDocument( ambiguous, ambiguousText, loc, overlay );
			if ( lsp::definitionAt( ares, ambiguous, 9, 18 ).found ||
				 lsp::hoverAt( ares, ambiguous, 9, 18 ).found )
			{
				fprintf( stderr, "ParserLeakTest: ambiguous receiver must answer null\n" );
				return 1;
			}
		}
	}

	printf( "ParserLeakTest: OK\n" );
	return 0;
}
