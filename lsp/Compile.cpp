#include "Compile.h"

#include "../FileLexer.h"
#include "../Type.h"
#include "../Expression.h"
#include "../Frontend.h"
#include "../Resolver.h"
#include "../ProjectCompile.h"
#include "../StdlibSelect.h"
#include "Run.h"
#include "ProjectContext.h"

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

// A setup diagnostic at the document's first `import <name>` (when the document
// imports it), else at 1:1 with where it was declared (plan D-5).
static Diagnostic placeSetupDiagnostic( const SetupDiagnostic &s, const std::string &docPath,
	const CompileResult &result )
{
	Diagnostic d;
	d.severity = s.severity;
	d.code = s.severity == Severity::Warning ? "blang-toml" : "project-setup";
	d.location.file = docPath;
	d.location.line = 1;
	d.location.col = 1;
	d.message = s.message;
	bool placed = false;
	if ( !s.importName.empty() && result.module != nullptr )
	{
		for ( const auto &impSp : result.module->getImports() )
		{
			const ImportStatement *imp = (const ImportStatement *)impSp;
			if ( imp != nullptr && imp->getModuleName() == s.importName )
			{
				d.location = imp->getLocation();
				placed = true;
				break;
			}
		}
	}
	if ( !placed && !s.declaredAt.empty() )
		d.message += " (declared at " + s.declaredAt + ")";
	return d;
}

// While a dependency module is unresolvable, the consumer's diagnostics ABOUT that
// module (it is simply absent from the context) would bury the one setup
// diagnostic: drop those that name it as a quoted identifier or qualifier
// (research R7 — a presentation filter over the shared compile's output, not a
// compile rule). Everything else is published.
static bool namesUnresolvedModule( const Diagnostic &d, const std::set<std::string> &names )
{
	for ( const auto &n : names )
		if ( d.message.find( "'" + n + "'" ) != std::string::npos ||
			 d.message.find( "'" + n + "." ) != std::string::npos )
			return true;
	return false;
}

CompileResult compileDocument( const std::string &path, const std::string &text,
	const StdlibLocation &stdlib, const Overlay &overlay )
{
	ProjectContextResult pc = buildProjectContext( path, text, overlay );
	if ( pc.tomlUnreadable )
	{
		// N4: the project's blang.toml exists but cannot be read — one Warning,
		// then the single-file (stdlib) context.
		CompileResult result = compileDocument( path, text, stdlib );
		Diagnostic warn;
		warn.severity = Severity::Warning;
		warn.code = "blang-toml";
		warn.location.file = path;
		warn.location.line = 1;
		warn.location.col = 1;
		warn.message = pc.tomlPath + ": cannot read blang.toml";
		result.diagnostics.insert( result.diagnostics.begin(), warn );
		return result;
	}
	if ( !pc.isProject )
		return compileDocument( path, text, stdlib );

	// The plan bcc build's final qcc invocation uses (plan D-4 step 5).
	CompilePlan plan;
	plan.identity = IdentityPolicy::kCanonical;
	plan.moduleOrigin = pc.moduleOrigin;
	std::vector<std::string> stdlibFiles;
	if ( !pc.isLibrary && stdlib.usable )
	{
		// Each source scanned SEPARATELY (audit addendum H2), the document's own
		// text being its buffer.
		std::vector<std::string> texts;
		for ( const auto &in : pc.sources )
			texts.push_back( in.text );
		stdlibFiles = BlangStdlib::selectStdlibFiles( stdlib.path, BlangStdlib::scanImportsAll( texts ) );
	}
	plan.combine = !stdlibFiles.empty() && !pc.isLibrary;   // bcc's useCombine
	for ( const std::string &f : stdlibFiles )
	{
		CompileInput in;
		in.path = f;
		in.kind = classifyInput( f );
		plan.inputs.push_back( in );
	}
	for ( const auto &in : pc.sources )
		plan.inputs.push_back( in );
	for ( const auto &in : pc.interfaces )
		plan.inputs.push_back( in );

	CompileResult result = runDocumentPlan( plan, path );
	result.deps = pc.depRuns;

	std::vector<Diagnostic> published;
	if ( !pc.isLibrary && !stdlib.usable )
	{
		Diagnostic warn;   // U2's stdlib-not-found warning still leads
		warn.severity = Severity::Warning;
		warn.location.file = path;
		warn.location.line = 1;
		warn.location.col = 1;
		warn.message = stdlibNotFoundMessage( stdlib );
		warn.code = "stdlib-not-found";
		published.push_back( warn );
	}
	for ( const auto &s : pc.setup )
		if ( s.severity == Severity::Warning )
			published.push_back( placeSetupDiagnostic( s, path, result ) );
	for ( const auto &s : pc.setup )
		if ( s.severity != Severity::Warning )
			published.push_back( placeSetupDiagnostic( s, path, result ) );
	for ( const auto &d : result.diagnostics )
		if ( !namesUnresolvedModule( d, pc.unresolvedNames ) )
			published.push_back( d );
	result.diagnostics = published;
	return result;
}

} // namespace lsp
