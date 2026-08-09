// modules-v2-graph U4: the standalone module-resolution component (see Resolver.h).
// Behavior-neutral extraction of the resolution environment both qcc and blangd
// built inline.

#include "Resolver.h"
#include "Frontend.h"     // createGlobalScope(), gScope
#include "Expression.h"   // complete AST types for SmartPtr<Scope> instantiation

namespace QLang
{

Resolver::Resolver()
{
	// Own the global builtin scope (createGlobalScope returns refcount 0; the
	// SmartPtr takes the owning reference — Frontend.h's contract, so the first
	// child's parent ref does not free it out from under us). Install it as the
	// process `gScope` alias the parser/sema read, saving the previous alias so a
	// nested Resolver (LSP server, unit tests) restores it on destruction.
	mGlobalScope = createGlobalScope();
	mGlobalRaw = (Scope *)mGlobalScope;
	mPrevGScope = gScope;
	gScope = mGlobalRaw;
}

Resolver::~Resolver()
{
	// Restore whatever alias was in place before this Resolver (typically nullptr
	// for the top-level driver, or an enclosing Resolver's scope). mGlobalScope's
	// SmartPtr releases the global scope here.
	gScope = mPrevGScope;
}

Scope *Resolver::newModuleScope( Scope *parent )
{
	Scope *s = new Scope( Scope::kScope_Module );
	s->setParent( parent != nullptr ? parent : (Scope *)mGlobalScope );
	return s;
}

void Resolver::registerNamespace( const std::string &name, Scope *ns )
{
	mNamespaces[name] = ns;
}

Scope *Resolver::resolveNamespace( const std::string &name ) const
{
	auto it = mNamespaces.find( name );
	return it != mNamespaces.end() ? it->second : nullptr;
}

} // namespace QLang
