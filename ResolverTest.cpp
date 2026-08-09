// modules-v2-graph U4 — ResolverTest: exercises the standalone Resolver component
// INDEPENDENT of the qcc/bcc drivers (done-condition 4). No LLVM, no CodeGen.

#include <cassert>
#include <cstdio>

#include "Resolver.h"
#include "Type.h"
#include "Expression.h"
#include "Frontend.h"

using namespace QLang;

int main()
{
	// The resolver constructs and owns the global builtin scope, and installs the
	// process `gScope` alias.
	{
		Resolver r;
		assert( r.globalScope() != nullptr );
		assert( gScope == r.globalScope() && "resolver installs the gScope alias" );

		// Builtin types resolve through the global scope.
		assert( r.globalScope()->findType( "int" ) != nullptr );
		assert( r.globalScope()->findType( "string" ) != nullptr );
		assert( r.globalScope()->findType( "no_such_type" ) == nullptr );

		// A fresh module scope parents into the environment: builtins are visible
		// via the parent chain, but the module scope is its own symbol table.
		// NOTE: hold module scopes in SmartPtr locals so they RELEASE at end of this
		// block — before ~Resolver. A raw Scope* would leak, and (because
		// Scope::mParent retains) each leaked module scope would pin the builtin
		// global scope alive so ~Resolver could not free it (LSan). The production
		// LSP caller already owns its scope via SmartPtr; this is a test-local
		// lifetime concern only.
		SmartPtr<Scope> m = r.newModuleScope();
		assert( (Scope *)m != nullptr );
		assert( m->findType( "int" ) != nullptr && "builtins visible via parent" );

		// Two module scopes are isolated from each other (a symbol in one is not
		// visible in the other) — the per-module-scope property U4 provides.
		SmartPtr<Scope> a = r.newModuleScope();
		SmartPtr<Scope> b = r.newModuleScope();
		a->addType( new Type( "OnlyInA" ) );
		assert( a->findType( "OnlyInA" ) != nullptr );
		assert( b->findType( "OnlyInA" ) == nullptr && "module scopes are isolated" );

		// Module-graph namespace registry (present for both drivers; dormant in
		// blangd until Epic C).
		SmartPtr<Scope> ns = r.newModuleScope();
		r.registerNamespace( "mymod", (Scope *)ns );
		assert( r.resolveNamespace( "mymod" ) == (Scope *)ns );
		assert( r.resolveNamespace( "absent" ) == nullptr );
	}

	// After the resolver is destroyed the gScope alias is restored (here: back to
	// null, the pre-construction value) — no dangling alias across constructions.
	assert( gScope == nullptr && "resolver restores the prior gScope on destruction" );

	printf( "ResolverTest: OK\n" );
	return 0;
}
