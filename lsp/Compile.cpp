#include "Compile.h"

#include "../FileLexer.h"
#include "../Type.h"
#include "../Expression.h"
#include "../Frontend.h"
#include "../Resolver.h"
#include "../Sema.h"
#include "StringLexerReader.h"

namespace lsp
{

CompileResult compileDocument( const std::string &path, const std::string &text )
{
	using namespace QLang;

	// modules-v2-graph U4: resolution runs through the standalone Resolver — the
	// SAME component qcc.cpp constructs (the Epic C seam; grep `Resolver` in both).
	// One process-lifetime Resolver owns the global builtin scope (a refcount-zero
	// raw gScope would be freed when the first file scope's parent ref dropped —
	// Frontend.h's contract), so repeated compiles share builtins but never leak
	// symbols into each other. blangd stays SINGLE-FILE this epic: it constructs
	// the resolver and calls only globalScope()/newModuleScope(); the module-graph
	// methods are present-but-dormant until Epic C wires cross-module resolution.
	static Resolver sResolver;
	gScope = sResolver.globalScope();

	// Fresh collector per reparse: DiagnosticEngine has no reset by design;
	// finish() is never called (nothing may render — the LSP publishes the
	// buffered list itself).
	DiagnosticEngine engine;
	DiagnosticEngine *prevDiag = gDiag;
	gDiag = &engine;

	CompileResult result;
	result.fileScope = sResolver.newModuleScope();

	StringLexerReader reader( text, path );
	Lexer lexer( &reader );

	// Module::Parse catches CompileError internally, buffers it through
	// gDiag, and returns null on a catastrophic (unrecoverable) failure —
	// the diagnostics are already in the engine either way.
	result.module = Module::Parse( lexer, (Scope *)result.fileScope );
	if ( result.module != nullptr )
	{
		// Same origin stamping the compiler does (M-3). Without this a Sema rule
		// keyed on a definition's origin would see an empty value here and a
		// populated one in qcc — the same rule reporting different diagnostics
		// in the editor and at the command line.
		stampDefiningOrigin( (Module *)result.module, path );
		Sema::analyze( (Module *)result.module, (Scope *)result.fileScope, engine );
	}

	gDiag = prevDiag;
	result.diagnostics = engine.diagnostics();
	return result;
}

} // namespace lsp
