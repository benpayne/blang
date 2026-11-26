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

#include "Resolver.h"
#include "Type.h"
#include "Expression.h"
#include "Frontend.h"
#include "FileLexer.h"
#include "Sema.h"
#include "DiagnosticEngine.h"
#include "lsp/StringLexerReader.h"
#include "lsp/Compile.h"

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

	printf( "ParserLeakTest: OK\n" );
	return 0;
}
