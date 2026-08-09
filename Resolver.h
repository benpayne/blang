#ifndef BLANG_RESOLVER_H_
#define BLANG_RESOLVER_H_

// modules-v2-graph U4: the standalone module-resolution component.
//
// Resolution used to be built INLINE in qcc.cpp main() (and, separately, in
// lsp/Compile.cpp). U4 extracts the resolution ENVIRONMENT into one named class
// that BOTH the compiler (qcc) and the editor (blangd) construct — the single
// shared seam Epic C (cross-module LSP) consumes without re-plumbing.
//
// U4 is BEHAVIOR-NEUTRAL: it owns the global builtin scope and hands out module
// scopes exactly as the inline code did. The up-front .bmod flat-merge injection
// and the combine routing POLICY stay in the driver this unit (their removal /
// generalization is done-condition 6 / U6). The module-graph methods
// (registerNamespace/resolveNamespace) are used by qcc's combine path and are
// present-but-dormant for blangd (which stays single-file this epic — Epic C wires
// it through them).

#include <map>
#include <string>
#include <vector>

#include "Type.h"       // QLang::Scope
#include "RefCount.h"

namespace QLang
{

class Resolver
{
public:
	// Builds and OWNS the global builtin scope (createGlobalScope()) and installs
	// it as the process `gScope` alias, saving the previous alias so nested
	// constructions (the LSP server, unit tests) restore cleanly on destruction.
	Resolver();
	~Resolver();

	Resolver( const Resolver & ) = delete;
	Resolver &operator=( const Resolver & ) = delete;

	// The shared global builtin scope. Same object `gScope` aliases.
	Scope *globalScope() const { return mGlobalRaw; }

	// A fresh module-level scope parented into the resolution environment (default
	// parent = the global scope). Replicates EXACTLY what the drivers built inline
	// (`new Scope(kScope_Module); setParent(parent)`) and returns the raw pointer —
	// lifetime is held by the returned scope's children / the caller / the parent
	// chain, precisely as before. The Resolver does NOT retain module scopes (doing
	// so would leak one per LSP reparse); it owns only the global scope.
	Scope *newModuleScope( Scope *parent = nullptr );

	// --- Module graph (behavior-neutral bookkeeping) ------------------------
	// The import/namespace registry the combine path keeps. Present for both
	// drivers; blangd does not yet resolve through it (Epic C).
	void registerNamespace( const std::string &name, Scope *ns );
	Scope *resolveNamespace( const std::string &name ) const;

private:
	SmartPtr<Scope> mGlobalScope;              // owns the builtin scope
	Scope *mGlobalRaw = nullptr;               // raw alias (const-friendly getter)
	Scope *mPrevGScope = nullptr;              // saved gScope alias (restored on ~)
	std::map<std::string, Scope *> mNamespaces;
};

} // namespace QLang

#endif // BLANG_RESOLVER_H_
