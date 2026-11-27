#ifndef BLANG_LSP_RUN_H_
#define BLANG_LSP_RUN_H_

// modules-v2-lsp U3 (audit R2): one ISOLATED compile. Every blangd compile — a
// document, or a dependency whose interface is being derived — gets a fresh
// Resolver (its own global builtin scope), so .bmod-injected dependency
// namespaces (registered on the running resolver's global scope) never leak
// across compiles, documents or reparses (closes U1's known item I6).
//
// The Resolver is created in AliasMode::kManual: results die in arbitrary order
// (Server::mCompiles), so the `gScope` alias cannot be LIFO-owned by resolvers.
// GlobalAliasScope installs it for exactly one ProjectCompile::run (U1 I3: the
// CALLER installs the alias; run asserts it) and restores the previous value.
//
// Ownership (KG-7): `resolver` is declared FIRST so it is destroyed LAST — the
// context's scopes parent (non-owningly) into the resolver's global scope.

#include <memory>

#include "../DiagnosticEngine.h"
#include "../Frontend.h"
#include "../ProjectCompile.h"
#include "../Resolver.h"

namespace lsp
{

struct Run
{
	std::shared_ptr<QLang::Resolver> resolver;     // FIRST: destroyed last
	std::shared_ptr<QLang::CompileContext> ctx;
};

// Installs gScope = resolver.globalScope() for its lifetime; restores on exit.
class GlobalAliasScope
{
public:
	explicit GlobalAliasScope( QLang::Resolver &r ) : mPrev( gScope ) { gScope = r.globalScope(); }
	~GlobalAliasScope() { gScope = mPrev; }
	GlobalAliasScope( const GlobalAliasScope & ) = delete;
	GlobalAliasScope &operator=( const GlobalAliasScope & ) = delete;

private:
	QLang::Scope *mPrev;
};

// One isolated ProjectCompile::run (the ONE shared entry point) on a fresh
// manual-alias resolver. `during`, when given, runs inside the alias scope after
// the compile (the interface emit — analyze L3).
template <class During>
Run runPlan( const QLang::CompilePlan &plan, QLang::DiagnosticEngine &diag, During during )
{
	Run r;
	r.resolver = std::make_shared<QLang::Resolver>( QLang::Resolver::AliasMode::kManual );
	GlobalAliasScope alias( *r.resolver );
	r.ctx.reset( QLang::ProjectCompile::run( plan, *r.resolver, diag ).release() );
	during( r );
	return r;
}

inline Run runPlan( const QLang::CompilePlan &plan, QLang::DiagnosticEngine &diag )
{
	return runPlan( plan, diag, []( Run & ) {} );
}

} // namespace lsp

#endif // BLANG_LSP_RUN_H_
