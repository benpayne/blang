#include "Compile.h"

#include "../FileLexer.h"
#include "../Type.h"
#include "../Expression.h"
#include "../Frontend.h"
#include "../Resolver.h"
#include "../ProjectCompile.h"

namespace lsp
{

CompileResult compileDocument( const std::string &path, const std::string &text )
{
	using namespace QLang;

	// Resolution runs through the standalone Resolver — the SAME component
	// qcc.cpp constructs. One process-lifetime Resolver owns the global builtin
	// scope (Frontend.h's contract; a file scope's parent link is non-owning —
	// KG-7 — so the global must outlive every CompileResult), so repeated
	// compiles share builtins but never leak symbols into each other.
	//
	// modules-v2-lsp U1 (REQ-001): the compile itself goes through the ONE shared
	// entry point ProjectCompile::run (grep it in qcc.cpp too), so every rule
	// blangd applies is the compiler's own. Single-file in U1: one in-memory
	// input, no combine, IdentityPolicy::kNone (no realpath, no digest — exactly
	// the pre-unit blangd behavior); U2/U3 add stdlib and project context.
	static Resolver sResolver;
	// The CALLER installs the gScope alias (run asserts it): a test-local
	// Resolver (resolver_reuse, parser_no_leak) restores gScope on destruction,
	// so re-install it on every call.
	gScope = sResolver.globalScope();

	// Fresh collector per reparse: DiagnosticEngine has no reset by design;
	// finish() is never called (nothing may render — the LSP publishes the
	// buffered list itself). run() installs/restores the gDiag alias.
	DiagnosticEngine engine;

	CompilePlan plan;
	CompileInput input;
	input.path = path;
	input.kind = CompileInput::Kind::kSource;
	input.inMemory = true;
	input.text = text;
	plan.inputs.push_back( input );
	plan.combine = false;
	plan.identity = IdentityPolicy::kNone;

	CompileResult result;
	result.context = ProjectCompile::run( plan, sResolver, engine );
	// The module is null only on a catastrophic parse (as before); the routed
	// file scope is always non-null.
	result.module = result.context->record( 0 ).module;
	result.fileScope = result.context->record( 0 ).scope;
	result.diagnostics = engine.diagnostics();
	return result;
}

} // namespace lsp
