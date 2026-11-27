#ifndef BLANG_LSP_COMPILE_H_
#define BLANG_LSP_COMPILE_H_

// The LSP reparse pipeline: compile one in-memory document through the shared
// project-compile entry point (ProjectCompile::run — the same call qcc makes)
// and return the buffered diagnostics plus the parsed module (retained for
// navigation requests: definition, hover, documentSymbol). Since modules-v2-lsp
// U2 the server compiles each document in the stdlib/prelude context `bcc
// file.b` uses (the three-argument overload); U3 adds project context through
// the same entry point.

#include <memory>
#include <string>
#include <vector>

#include "../DiagnosticEngine.h"
#include "../Type.h"
#include "../Expression.h"   // completes the AST types SmartPtr instantiates
#include "../ProjectCompile.h"
#include "StdlibLocation.h"

namespace lsp
{

struct CompileResult
{
	// The shared compile's context: it owns EVERY module, scope and AST the
	// compile built — the document's AND, in stdlib context, every stdlib module
	// (prelude/combine/namespace scopes included). shared_ptr because the Server
	// stores results by value.
	//
	// KG-7 invariant (specs/041-shared-project-compile/checklists/
	// kg7-resolution-refs.md, U2 plan D-4): an expression's link to the
	// definition it resolved to is NON-owning, so a user AST that resolves `Map`
	// points into a stdlib module owned ONLY here. Every stdlib module, scope and
	// AST a document's AST can resolve into MUST therefore be owned by that
	// document's CompileResult and outlive it. Declared FIRST so it is destroyed
	// LAST — after the `module`/`fileScope` co-owners below (analyze F4). Any
	// future reuse of parsed stdlib state across reparses must be held through a
	// shared_ptr that every result built against it also holds; U2 reuses none.
	std::shared_ptr<QLang::CompileContext> context;
	// The parsed user AST (non-null even for documents with errors — the parser
	// recovers per declaration) and the scope it was parsed in.
	SmartPtr<QLang::Module> module;
	SmartPtr<QLang::Scope> fileScope;
	std::vector<QLang::Diagnostic> diagnostics;
};

// Compile `text` as the document at `path` ALONE (single-file, no stdlib) —
// the pre-U2 behavior, kept for tests that exercise the bare frontend.
CompileResult compileDocument( const std::string &path, const std::string &text );

// Compile `text` as the document at `path` in the context `bcc <path>` uses
// (modules-v2-lsp U2): when `stdlib` is usable, the stdlib set the shared
// selection picks for the document's in-memory imports + the prelude, combined
// with the document through the shared ProjectCompile::run. Only diagnostics
// located in the document are returned. When `stdlib` is not usable, the
// document compiles alone and a stdlib-not-found Warning at 1:1 comes first.
// Never prints; never throws for source-level errors; never spawns a process.
CompileResult compileDocument( const std::string &path, const std::string &text,
	const StdlibLocation &stdlib );

} // namespace lsp

#endif // BLANG_LSP_COMPILE_H_
