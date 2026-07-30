#include "Compile.h"

#include "../FileLexer.h"
#include "../Type.h"
#include "../Expression.h"
#include "../Frontend.h"
#include "../Sema.h"
#include "StringLexerReader.h"

namespace lsp
{

CompileResult compileDocument( const std::string &path, const std::string &text )
{
	using namespace QLang;

	// One global builtin scope for the whole server process, owned here (a
	// refcount-zero raw gScope would be freed when the first file scope's
	// parent reference dropped — Frontend.h documents the contract). File
	// scopes parent to it, so repeated compiles share builtins but never
	// leak symbols into each other.
	static SmartPtr<Scope> sGlobalScope = createGlobalScope();
	gScope = (Scope *)sGlobalScope;

	// Fresh collector per reparse: DiagnosticEngine has no reset by design;
	// finish() is never called (nothing may render — the LSP publishes the
	// buffered list itself).
	DiagnosticEngine engine;
	DiagnosticEngine *prevDiag = gDiag;
	gDiag = &engine;

	CompileResult result;
	result.fileScope = new Scope( Scope::kScope_Module );
	result.fileScope->setParent( gScope );

	StringLexerReader reader( text, path );
	Lexer lexer( &reader );

	// Module::Parse catches CompileError internally, buffers it through
	// gDiag, and returns null on a catastrophic (unrecoverable) failure —
	// the diagnostics are already in the engine either way.
	result.module = Module::Parse( lexer, (Scope *)result.fileScope );
	if ( result.module != nullptr )
		Sema::analyze( (Module *)result.module, (Scope *)result.fileScope, engine );

	gDiag = prevDiag;
	result.diagnostics = engine.diagnostics();
	return result;
}

} // namespace lsp
