// modules-v2-graph U4 — ResolverReuseTest (done-condition 5).
//
// Proves the Epic C seam: the resolver is ONE shared component. This test builds
// the resolver EXACTLY as lsp/Compile.cpp does (a Resolver + newModuleScope) and
// asserts a fixture resolves IDENTICALLY to the qcc single-file path — so the
// editor and the compiler share one resolution truth. It also calls the real
// lsp::compileDocument entry point on the same source and checks its resolution
// agrees, so both call sites are exercised, not just re-implemented here.
//
// Lifetime note: each Resolver's module scope is held in a SmartPtr and released
// BEFORE that Resolver is destroyed (the caller owns a module scope, and its
// non-owning parent link must not outlive the Resolver's global scope — KG-7). So the "qcc path" captures its resolution RESULTS inside the block
// rather than returning a scope that outlives its Resolver.

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

// The set of resolutions we compare across the two paths.
struct Resolutions
{
	bool parsed = false;
	bool type_int = false, type_Point = false, type_string = false, type_bool = false;
	bool type_absent = false;   // a name that must NOT resolve
	bool sym_Point = false;     // the struct is also a symbol
	bool sym_add = false;       // a function (with a body) is a symbol
	bool sym_absent = false;

	bool operator==( const Resolutions &o ) const
	{
		return parsed == o.parsed && type_int == o.type_int &&
			type_Point == o.type_Point && type_string == o.type_string &&
			type_bool == o.type_bool && type_absent == o.type_absent &&
			sym_Point == o.sym_Point && sym_add == o.sym_add && sym_absent == o.sym_absent;
	}
};

static Resolutions probe( Scope *s, bool parsed )
{
	Resolutions r;
	r.parsed = parsed;
	r.type_int = s->findType( "int" ) != nullptr;         // builtin via parent chain
	r.type_Point = s->findType( "Point" ) != nullptr;      // fixture-declared type
	r.type_string = s->findType( "string" ) != nullptr;
	r.type_bool = s->findType( "bool" ) != nullptr;
	r.type_absent = s->findType( "NoSuchType" ) != nullptr;
	r.sym_Point = s->findSymbol( "Point" ) != nullptr;     // a struct is also a symbol
	r.sym_add = s->findSymbol( "add" ) != nullptr;         // function symbol
	r.sym_absent = s->findSymbol( "no_such_symbol" ) != nullptr;
	return r;
}

int main()
{
	const std::string path = "reuse_fixture.b";
	// Fixture: a struct and a function WITH A BODY. A struct is registered as
	// BOTH a type (findType) and a symbol (findSymbol), so it proves type AND
	// symbol resolution; the function proves a function symbol resolves too.
	// (Until KG-7 was fixed this fixture had to be struct-only: parsing a
	// function body leaked the parser's AST/scopes under ASan because a child
	// scope retained its parent, forming refcount cycles. Scopes are now owned
	// top-down, so this test is LSan-clean with a body.)
	const std::string src =
		"struct Point { int x; int y; }\n"
		"fn add( int a, int b ) -> int { int c = a + b; return c; }\n";

	// (1) The qcc-style path, built through the shared Resolver EXACTLY as
	// lsp/Compile.cpp does (Resolver + newModuleScope). Everything lives and dies
	// inside this block: the module scope (SmartPtr) and the Module release first,
	// then ~Resolver frees the global scope — no leak, no scope outlives it.
	Resolutions qccPath;
	{
		Resolver resolver;
		SmartPtr<Scope> fileScope = resolver.newModuleScope();

		DiagnosticEngine engine;
		DiagnosticEngine *prev = gDiag;
		gDiag = &engine;

		lsp::StringLexerReader reader( src, path );
		Lexer lexer( &reader );
		SmartPtr<Module> mod = Module::Parse( lexer, (Scope *)fileScope );
		bool parsed = ( (Module *)mod != nullptr );
		if ( parsed )
		{
			stampDefiningOrigin( (Module *)mod, path );
			Sema::analyze( (Module *)mod, (Scope *)fileScope, engine );
		}
		gDiag = prev;

		qccPath = probe( (Scope *)fileScope, parsed );
	}
	assert( qccPath.parsed && "fixture parses through the resolver-built scope" );
	assert( qccPath.type_int && qccPath.type_Point && qccPath.sym_Point && qccPath.sym_add &&
		"builtin + fixture type + fixture symbol resolve on the qcc path" );
	assert( !qccPath.type_absent && !qccPath.sym_absent &&
		"absent names do not resolve (negative resolution)" );

	// (2) The REAL blangd entry point (lsp/Compile.cpp), which now constructs the
	// SAME Resolver internally. Its fileScope is owned by the CompileResult
	// SmartPtr; its parent is the process-static Resolver's global scope (a
	// reachable root — LSan-clean).
	Resolutions lspPath;
	{
		lsp::CompileResult res = lsp::compileDocument( path, src );
		lspPath = probe( (Scope *)res.fileScope, res.module != nullptr );
	}
	assert( lspPath.parsed && lspPath.type_int && lspPath.type_Point &&
		lspPath.sym_Point && lspPath.sym_add && "builtin + fixture type + fixture symbol resolve (lsp path)" );

	// Identical resolution: the shared component yields the SAME result on both the
	// qcc path and the real blangd entry point.
	assert( qccPath == lspPath &&
		"resolution is identical across the qcc and lsp paths (one shared resolver)" );

	printf( "ResolverReuseTest: OK\n" );
	return 0;
}
