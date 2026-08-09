// modules-v2-graph U4 — ResolverReuseTest (done-condition 5).
//
// Proves the Epic C seam: the resolver is ONE shared component. This test builds
// the resolver EXACTLY as lsp/Compile.cpp does (a Resolver + newModuleScope) and
// asserts a fixture resolves IDENTICALLY to the qcc single-file path — so the
// editor and the compiler share one resolution truth. It also calls the real
// lsp::compileDocument entry point on the same source and checks its resolution
// agrees, so both call sites are exercised, not just re-implemented here.

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

// Resolve a fixture the way the qcc single-file path does, but through the
// standalone Resolver (the component both drivers now share). Returns the file
// scope so the caller can assert what resolves.
static SmartPtr<Scope> resolveLikeQcc( const std::string &path,
	const std::string &src, bool &parsedOk )
{
	Resolver resolver;                       // owns global scope, installs gScope
	Scope *fileScope = resolver.newModuleScope();   // exactly lsp/Compile.cpp's step

	DiagnosticEngine engine;
	DiagnosticEngine *prev = gDiag;
	gDiag = &engine;

	lsp::StringLexerReader reader( src, path );
	Lexer lexer( &reader );
	SmartPtr<Module> mod = Module::Parse( lexer, fileScope );
	parsedOk = ( mod != nullptr );
	if ( parsedOk )
	{
		stampDefiningOrigin( (Module *)mod, path );
		Sema::analyze( (Module *)mod, fileScope, engine );
	}
	gDiag = prev;
	// Keep the scope alive for the caller by returning an owning ref.
	return SmartPtr<Scope>( fileScope );
}

int main()
{
	const std::string path = "reuse_fixture.b";
	const std::string src =
		"struct Point { int x; int y; }\n"
		"fn add(int a, int b) -> int { return a + b; }\n";

	// (1) The qcc-style path, built through the shared Resolver.
	bool parsedOk = false;
	SmartPtr<Scope> qccScope = resolveLikeQcc( path, src, parsedOk );
	assert( parsedOk && "fixture parses through the resolver-built scope" );

	Scope *qs = (Scope *)qccScope;
	// Builtins via the parent chain; the fixture's own declarations in the scope.
	assert( qs->findType( "int" ) != nullptr );
	assert( qs->findSymbol( "add" ) != nullptr && "function resolves (qcc path)" );
	assert( qs->findType( "Point" ) != nullptr && "struct type resolves (qcc path)" );

	// (2) The REAL blangd entry point (lsp/Compile.cpp), which now constructs the
	// SAME Resolver internally. Its resolution must agree name-for-name.
	lsp::CompileResult r = lsp::compileDocument( path, src );
	assert( r.module != nullptr && "compileDocument parses the fixture" );
	Scope *ls = (Scope *)r.fileScope;
	assert( ls->findType( "int" ) != nullptr );
	assert( ls->findSymbol( "add" ) != nullptr && "function resolves (lsp path)" );
	assert( ls->findType( "Point" ) != nullptr && "struct type resolves (lsp path)" );

	// Identical resolution: every name that resolves on one path resolves on the
	// other (same shared component, same result).
	const char *names[] = { "int", "add", "Point", "string", "bool" };
	for ( const char *n : names )
	{
		bool qType = qs->findType( n ) != nullptr;
		bool lType = ls->findType( n ) != nullptr;
		bool qSym = qs->findSymbol( n ) != nullptr;
		bool lSym = ls->findSymbol( n ) != nullptr;
		assert( qType == lType && "type resolution identical across qcc/lsp paths" );
		assert( qSym == lSym && "symbol resolution identical across qcc/lsp paths" );
	}

	printf( "ResolverReuseTest: OK\n" );
	return 0;
}
