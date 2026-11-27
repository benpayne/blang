#include "Compile.h"

#include "../FileLexer.h"
#include "../Type.h"
#include "../Expression.h"
#include "../Frontend.h"
#include "../Resolver.h"
#include "../ProjectCompile.h"
#include "../StdlibSelect.h"

namespace lsp
{

using namespace QLang;

// Resolution runs through the standalone Resolver — the SAME component qcc.cpp
// constructs. ONE process-lifetime Resolver owns the global builtin scope
// (Frontend.h's contract; a file scope's parent link is non-owning — KG-7 — so
// the global must outlive every CompileResult). Both compileDocument overloads
// reach it through this one function: a second static Resolver would fight over
// the gScope alias (analyze F2).
static Resolver &sharedResolver()
{
	static Resolver sResolver;
	return sResolver;
}

// The shared tail: run the plan through the ONE shared entry point
// ProjectCompile::run (modules-v2-lsp REQ-001 — grep it in qcc.cpp too), so every
// rule blangd applies (routing, prelude, import diagnostics, Sema) is the
// compiler's own. The user input is always the LAST record (combine mode puts
// the user file last; blangd plans carry no interface inputs).
static CompileResult runPlan( const CompilePlan &plan, const std::string &path )
{
	Resolver &resolver = sharedResolver();
	// The CALLER installs the gScope alias (run asserts it): a test-local
	// Resolver (resolver_reuse, parser_no_leak) restores gScope on destruction,
	// so re-install it on every call.
	gScope = resolver.globalScope();

	// Fresh collector per reparse: DiagnosticEngine has no reset by design;
	// finish() is never called (nothing may render — the LSP publishes the
	// buffered list itself). run() installs/restores the gDiag alias.
	DiagnosticEngine engine;

	CompileResult result;
	result.context = ProjectCompile::run( plan, resolver, engine );
	// The module is null only on a catastrophic parse (as before); the routed
	// file scope is always non-null.
	const ModuleRecord &user = result.context->records().back();
	result.module = user.module;
	result.fileScope = user.scope;
	// Diagnostics are about the open document (design C5): a diagnostic located
	// in a stdlib file is never published against it.
	for ( const auto &d : engine.diagnostics() )
		if ( d.location.file == path || d.location.file.empty() )
			result.diagnostics.push_back( d );
	return result;
}

static CompilePlan singleFilePlan( const std::string &path, const std::string &text )
{
	CompilePlan plan;
	CompileInput input;
	input.path = path;
	input.kind = CompileInput::Kind::kSource;
	input.inMemory = true;
	input.text = text;
	plan.inputs.push_back( input );
	plan.combine = false;
	// No realpath, no digest (U1 research R4): digests feed only mangling.
	plan.identity = IdentityPolicy::kNone;
	return plan;
}

CompileResult compileDocument( const std::string &path, const std::string &text )
{
	return runPlan( singleFilePlan( path, text ), path );
}

CompileResult compileDocument( const std::string &path, const std::string &text,
	const StdlibLocation &stdlib )
{
	if ( !stdlib.usable )
	{
		// Today's single-file compile, led by ONE located warning naming the route
		// and path tried (spec FR-008; contracts/stdlib-discovery.md).
		CompileResult result = runPlan( singleFilePlan( path, text ), path );
		Diagnostic warn;
		warn.severity = Severity::Warning;
		warn.location.file = path;
		warn.location.line = 1;
		warn.location.col = 1;
		warn.message = stdlibNotFoundMessage( stdlib );
		warn.code = "stdlib-not-found";
		result.diagnostics.insert( result.diagnostics.begin(), warn );
		return result;
	}

	// The plan `bcc <file>` hands qcc (modules-v2-lsp U2, spec FR-001): the shared
	// stdlib selection over the document's IN-MEMORY imports (FR-002), then the
	// document itself as the last (user) input, in combine mode. blangd only
	// assembles inputs — it owns no selection/routing/prelude/Sema rule.
	CompilePlan plan;
	plan.combine = true;
	plan.identity = IdentityPolicy::kNone;
	for ( const std::string &f : BlangStdlib::selectStdlibFiles( stdlib.path,
			BlangStdlib::scanImports( text ) ) )
	{
		CompileInput in;
		in.path = f;
		in.kind = classifyInput( f );
		plan.inputs.push_back( in );
	}
	CompileInput doc;
	doc.path = path;
	doc.kind = CompileInput::Kind::kSource;
	doc.inMemory = true;
	doc.text = text;
	plan.inputs.push_back( doc );
	return runPlan( plan, path );
}

} // namespace lsp
