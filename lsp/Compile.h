#ifndef BLANG_LSP_COMPILE_H_
#define BLANG_LSP_COMPILE_H_

// The LSP reparse pipeline: compile one in-memory document through the shared
// project-compile entry point (ProjectCompile::run — the same call qcc makes)
// and return the buffered diagnostics plus the parsed module (retained for
// navigation requests: definition, hover, documentSymbol). Since modules-v2-lsp
// U2 the server compiles each document in the stdlib/prelude context `bcc
// file.b` uses (the three-argument overload); U3 adds project context through
// the same entry point.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../DiagnosticEngine.h"
#include "../Type.h"
#include "../Expression.h"   // completes the AST types SmartPtr instantiates
#include "../ProjectCompile.h"
#include "../Resolver.h"
#include "StdlibLocation.h"
#include "DocumentStore.h"

namespace lsp
{

struct Run;   // lsp/Run.h — one isolated compile (fresh resolver + context)

// modules-v2-lsp U4 (research R2): the document compile's dependency interfaces
// (each at its nominal `<depDir>/<name>.bmod` CompileInput path) -> that
// dependency's kept SOURCE compile. Navigation maps an interface declaration back
// to the source declaration it was exported from (lsp/Navigation.cpp) so a
// definition never lands in a generated .bmod. Shares the Runs `deps` owns.
struct InterfaceSourceMap
{
	std::map<std::string, std::shared_ptr<Run>> byInterfacePath;

	bool isInterfacePath( const std::string &file ) const
	{
		return byInterfacePath.count( file ) > 0;
	}
	// The unique source declaration (same kind, name and owning type) in the
	// dependency compile for `file`, else in any dependency compile; null when
	// none or more than one matches — never a guess, never the interface decl.
	// `owner` is the owning struct's name for a method/init ('' for top level).
	const QLang::Symbol *toSource( const QLang::Symbol *interfaceDecl, const std::string &file,
		const std::string &owner ) const;
};

// One document compile. Members are destroyed in REVERSE declaration order, and
// the order is the KG-7 invariant (specs/041-shared-project-compile/checklists/
// kg7-resolution-refs.md; U3 plan D-4):
//   - `resolver` FIRST => destroyed LAST: every scope of `context` parents
//     (non-owningly) into its global builtin scope (U3 audit R2: one fresh
//     resolver per compile — nothing is shared across compiles).
//   - `context` owns EVERY module, scope and AST of the document's compile —
//     the document's AND every stdlib module and dependency interface. An
//     expression's link to the definition it resolved to is NON-owning, so a user
//     AST that resolves `Map` or `mathlib.add` points into modules owned ONLY
//     here; `context` must therefore outlive `module`/`fileScope`.
//   - `deps` are the dependency SOURCE compiles (U3), kept for navigation (U4);
//     nothing in `context` points into them (interfaces are re-parsed text).
//   - `interfaces` (U4) shares those same Runs, keyed by interface path.
//   - `module`/`fileScope` co-own the document's own module and scope.
// No state is reused across reparses; any future cache must be held through a
// shared_ptr that every result built against it also holds. Replace a stored
// result by destroying it (map erase + emplace, as Server::publishDiagnostics
// does), never by memberwise move-assignment, which would release `resolver`
// and `context` first.
struct CompileResult
{
	std::shared_ptr<QLang::Resolver> resolver;
	std::shared_ptr<QLang::CompileContext> context;
	std::vector<std::shared_ptr<Run>> deps;
	InterfaceSourceMap interfaces;   // U4: shares `deps`' Runs (declared after them)
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

// modules-v2-lsp U3: the server's entry point. A member of a blang.toml project
// is compiled with the plan `bcc build`'s final qcc invocation uses — the
// project's sources, its path/already-fetched git dependencies resolved FROM
// SOURCE (interfaces derived by the one exporter), the stdlib for a binary — with
// every open buffer in `overlay` taking precedence over the disk copy. Anything
// else falls back to the stdlib overload above. Setup problems (missing path,
// unfetched/unpinned git dep, a failing dependency, a cycle, blang.toml lines)
// are located diagnostics; never a crash, never a process, never the network.
CompileResult compileDocument( const std::string &path, const std::string &text,
	const StdlibLocation &stdlib, const Overlay &overlay );

} // namespace lsp

#endif // BLANG_LSP_COMPILE_H_
