#include "Compile.h"

#include "../FileLexer.h"
#include "../Type.h"
#include "../Expression.h"
#include "../Frontend.h"
#include "../Resolver.h"
#include "../ProjectCompile.h"
#include "../StdlibSelect.h"
#include "Run.h"

namespace lsp
{

using namespace QLang;

// The shared tail: run the plan through the ONE shared entry point
// ProjectCompile::run (modules-v2-lsp REQ-001 — grep it in qcc.cpp too), so every
// rule blangd applies (routing, prelude, import diagnostics, Sema) is the
// compiler's own. Each compile is ISOLATED (U3 audit R2): a fresh manual-alias
// resolver per run (lsp/Run.h), owned by the result. `docPath` selects the
// document's own record (any project member may be the open document).
static CompileResult runDocumentPlan( const CompilePlan &plan, const std::string &docPath )
{
	DiagnosticEngine engine;   // fresh per reparse; never finish()ed
	Run run = runPlan( plan, engine );

	CompileResult result;
	result.resolver = run.resolver;
	result.context = run.ctx;
	// The module is null only on a catastrophic parse (as before); the routed
	// file scope is always non-null.
	const ModuleRecord *user = &result.context->records().back();
	for ( const auto &rec : result.context->records() )
		if ( rec.path == docPath )
			user = &rec;
	result.module = user->module;
	result.fileScope = user->scope;
	// Diagnostics are about the open document (design C5): a diagnostic located
	// in another file is never published against it.
	for ( const auto &d : engine.diagnostics() )
		if ( d.location.file == docPath || d.location.file.empty() )
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
	return runDocumentPlan( singleFilePlan( path, text ), path );
}

CompileResult compileDocument( const std::string &path, const std::string &text,
	const StdlibLocation &stdlib )
{
	if ( !stdlib.usable )
	{
		// Today's single-file compile, led by ONE located warning naming the route
		// and path tried (spec FR-008; contracts/stdlib-discovery.md).
		CompileResult result = runDocumentPlan( singleFilePlan( path, text ), path );
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
	return runDocumentPlan( plan, path );
}

} // namespace lsp
