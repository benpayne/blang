// modules-v2-lsp U1 — ProjectCompileTest: drives the shared project-compile entry
// point (ProjectCompile::run) and the shared stdlib selection (StdlibSelect)
// DIRECTLY, independent of the qcc/bcc/blangd drivers (spec FR-019). No LLVM.
//
// The qcc binary is used only as an ORACLE for the expected rendered
// diagnostics (it is not the system under test). The oracle is mandatory: a
// missing qcc is a FAIL, never a skip.
//
// Usage: projectCompileTest <qcc-oracle> <source-root>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "BmodEmitter.h"
#include "BmodFormat.h"
#include "DiagnosticEngine.h"
#include "Expression.h"
#include "Frontend.h"
#include "ProjectCompile.h"
#include "Resolver.h"
#include "Sema.h"
#include "StdlibSelect.h"
#include "StringLexerReader.h"
#include "Type.h"

using namespace QLang;

static int gFailures = 0;

// Checked in every build type (not compiled out under NDEBUG).
#define CHECK( cond ) \
	do { if ( !( cond ) ) { std::fprintf( stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); gFailures++; } } while ( false )

static std::string gQcc;
static std::string gSrc;

static std::string readFile( const std::string &path )
{
	std::ifstream f( path, std::ios::binary );
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static std::string shellQuote( const std::string &s )
{
	std::string out = "'";
	for ( char c : s )
	{
		if ( c == '\'' )
			out += "'\\''";
		else
			out += c;
	}
	return out + "'";
}

struct OracleResult
{
	std::string text;
	int exitCode = -1;
};

// qcc --parse-only --combine [--json] <paths>, capturing stderr + exit code.
// ASAN_OPTIONS=detect_leaks=0 applies to the CHILD only (analyze H1): under
// build-asan the oracle's pre-existing error-exit leak would otherwise append a
// LeakSanitizer report to the compared stderr. This process keeps leak
// detection on, so ProjectCompile::run itself is still LSan-checked.
static OracleResult oracle( const std::vector<std::string> &paths, bool json )
{
	std::string cmd = "ASAN_OPTIONS=detect_leaks=0 " + shellQuote( gQcc ) +
		" --parse-only --combine";
	if ( json )
		cmd += " --json";
	for ( const auto &p : paths )
		cmd += " " + shellQuote( p );
	cmd += " 2>&1 1>/dev/null";
	OracleResult r;
	FILE *pipe = popen( cmd.c_str(), "r" );
	if ( pipe == nullptr )
		return r;
	char buf[4096];
	size_t n;
	while ( ( n = fread( buf, 1, sizeof( buf ), pipe ) ) > 0 )
		r.text.append( buf, n );
	int status = pclose( pipe );
	if ( status != -1 && WIFEXITED( status ) )
		r.exitCode = WEXITSTATUS( status );
	return r;
}

static CompilePlan combinePlan( const std::vector<std::string> &paths )
{
	CompilePlan plan;
	for ( const auto &p : paths )
	{
		CompileInput in;
		in.path = p;
		in.kind = classifyInput( p );
		plan.inputs.push_back( in );
	}
	plan.combine = true;
	return plan;
}

struct RunOutcome
{
	std::string rendered;
	bool failed = false;          // ctx->hadError() || diag.hasErrors()
	std::size_t errorCount = 0;
	std::size_t diagCount = 0;
};

// One compile in its OWN Resolver (nested block): the context is destroyed
// before the resolver (KG-7), so nothing crosses cases. `inspect` runs while
// the context is alive.
template <typename Inspect>
static RunOutcome runCase( const CompilePlan &plan, bool json, Inspect inspect )
{
	RunOutcome out;
	std::ostringstream os;
	{
		Resolver resolver;
		DiagnosticEngine diag( os );
		diag.setJson( json );
		{
			std::unique_ptr<CompileContext> ctx = ProjectCompile::run( plan, resolver, diag );
			out.failed = ctx->hadError() || diag.hasErrors();
			inspect( *ctx );
		}
		for ( const auto &d : diag.diagnostics() )
			if ( d.severity == Severity::Error )
				out.errorCount++;
		out.diagCount = diag.diagnostics().size();
		diag.finish();
	}
	out.rendered = os.str();
	return out;
}

static RunOutcome runCase( const CompilePlan &plan, bool json )
{
	return runCase( plan, json, []( const CompileContext & ) {} );
}

static std::vector<std::string> withBaseStdlib( const std::string &fixture )
{
	std::vector<std::string> paths = BlangStdlib::selectStdlibFiles(
		gSrc + "/stdlib", BlangStdlib::scanImports( readFile( fixture ) ) );
	paths.push_back( fixture );
	return paths;
}

static long findRecord( const CompileContext &ctx, const std::string &suffix )
{
	for ( std::size_t i = 0; i < ctx.records().size(); i++ )
	{
		const std::string &p = ctx.record( i ).path;
		if ( p.size() >= suffix.size() &&
			 p.compare( p.size() - suffix.size(), suffix.size(), suffix ) == 0 )
			return (long)i;
	}
	return -1;
}

// Parity of `paths` against the qcc oracle, human and --json.
static void checkParity( const std::vector<std::string> &paths, const char *label )
{
	for ( bool json : { false, true } )
	{
		RunOutcome got = runCase( combinePlan( paths ), json );
		OracleResult want = oracle( paths, json );
		if ( got.rendered != want.text )
		{
			std::fprintf( stderr, "FAIL %s (%s): rendered diagnostics differ from qcc\n"
				"--- run():\n%s--- qcc:\n%s---\n", label, json ? "json" : "human",
				got.rendered.c_str(), want.text.c_str() );
			gFailures++;
		}
		CHECK( got.failed == ( want.exitCode != 0 ) );
	}
}

static void caseC1()
{
	const std::string fixture = gSrc + "/test_files/project_compile/diag_parity.b";
	std::vector<std::string> paths = withBaseStdlib( fixture );
	checkParity( paths, "C1" );
	RunOutcome got = runCase( combinePlan( paths ), false );
	CHECK( got.failed );
	CHECK( got.errorCount == 2 );   // unknown module + the Sema type error
	CHECK( got.rendered.find( "unused import 'net'" ) != std::string::npos );
	CHECK( got.rendered.find( "unknown module 'nonesuch'" ) != std::string::npos );
}

static void caseC2C3()
{
	const std::string fixture = gSrc + "/test_files/project_compile/prelude_use.b";
	std::vector<std::string> paths = withBaseStdlib( fixture );
	RunOutcome got = runCase( combinePlan( paths ), false, [&]( const CompileContext &ctx )
	{
		// C2: prelude resolution.
		Scope *prelude = ctx.preludeScope();
		Scope *user = ctx.userScope();
		CHECK( prelude != nullptr && user != nullptr );
		if ( prelude == nullptr || user == nullptr )
			return;
		CHECK( prelude->findType( "Map" ) != nullptr );
		CHECK( prelude->findType( "Set" ) != nullptr );
		CHECK( user->ownSymbols().count( "Map" ) == 0 );
		CHECK( user->ownSymbols().count( "Set" ) == 0 );
		CHECK( user->findSymbol( "Map" ) != nullptr );
		CHECK( user->findSymbol( "Map" ) == prelude->findSymbol( "Map" ) );
		CHECK( user->findSymbol( "Set" ) == prelude->findSymbol( "Set" ) );
		CHECK( user->findType( "Map" ) == prelude->findType( "Map" ) );

		// C3: namespace registration + roles + codegen prefixes.
		CHECK( user->findNamespace( "net" ) != nullptr );
		long net = findRecord( ctx, "/stdlib/net.b" );
		long buffer = findRecord( ctx, "/stdlib/buffer.b" );
		long coll = findRecord( ctx, "/stdlib/collections.b" );
		long me = findRecord( ctx, "/prelude_use.b" );
		CHECK( net >= 0 && buffer >= 0 && coll >= 0 && me >= 0 );
		if ( net < 0 || buffer < 0 || coll < 0 || me < 0 )
			return;
		CHECK( ctx.record( net ).role == ModuleRole::kNamespaced );
		CHECK( ctx.codegenPrefix( net ) == "net" );
		CHECK( !ctx.isPreludeProvider( net ) );
		for ( long p : { buffer, coll } )
		{
			CHECK( ctx.record( p ).role == ModuleRole::kPreludeProvider );
			CHECK( ctx.codegenPrefix( p ).empty() );
			CHECK( ctx.isPreludeProvider( p ) );
		}
		CHECK( ctx.record( me ).role == ModuleRole::kUser );
		CHECK( ctx.record( me ).scope == user );
		CHECK( ctx.codegenPrefix( me ).empty() );
		CHECK( ctx.moduleIndices().size() == ctx.records().size() );
		CHECK( ctx.suppressedPrelude().empty() );
	} );
	CHECK( !got.failed );
	CHECK( got.diagCount == 0 );
	checkParity( paths, "C2" );
}

static void caseC4()
{
	const std::string fixture = gSrc + "/test_files/project_compile/prelude_shadow.b";
	std::vector<std::string> paths = withBaseStdlib( fixture );
	RunOutcome got = runCase( combinePlan( paths ), false, [&]( const CompileContext &ctx )
	{
		Scope *user = ctx.userScope();
		long me = findRecord( ctx, "/prelude_shadow.b" );
		CHECK( user != nullptr && me >= 0 && ctx.record( me ).module != nullptr );
		if ( user == nullptr || me < 0 || ctx.record( me ).module == nullptr )
			return;
		Symbol *userMap = nullptr;
		for ( const auto &sp : ctx.record( me ).module->getStructList() )
			if ( sp->getName() == "Map" )
				userMap = const_cast<StructDefinition *>( (const StructDefinition *)sp );
		CHECK( userMap != nullptr );
		CHECK( user->findSymbol( "Map" ) == userMap );
		CHECK( user->findSymbol( "Map" ) != ctx.preludeScope()->findSymbol( "Map" ) );
		CHECK( ctx.suppressedPrelude().count( "Map" ) == 1 );
		CHECK( ctx.suppressedPrelude().count( "Set" ) == 0 );
	} );
	CHECK( !got.failed );
	CHECK( got.diagCount == 0 );
	checkParity( paths, "C4" );
}

static void caseC5()
{
	std::set<std::string> scan = BlangStdlib::scanImports(
		"import cli;\nimport timer;\n// import math;\n   import   env.x;\n"
		"import zzz_unknown;\nimport\tmath;\n" );
	CHECK( ( scan == std::set<std::string>{ "cli", "timer", "env", "zzz_unknown" } ) );
	std::vector<std::string> sel = BlangStdlib::selectStdlibFiles( gSrc + "/stdlib", scan );
	std::vector<std::string> want;
	for ( const char *n : { "sys", "buffer", "collections", "fs", "net", "timer", "env", "cli" } )
		want.push_back( gSrc + "/stdlib/" + n + ".b" );
	CHECK( sel == want );
}

static void caseC6()
{
	const std::string path = "/nonexistent/u1_kNone.b";
	const std::string text = "fn main() -> int {\n    int x = \"s\";\n    return 0;\n}\n";

	std::vector<Diagnostic> got;
	{
		Resolver resolver;
		DiagnosticEngine diag;
		CompilePlan plan;
		CompileInput in;
		in.path = path;
		in.inMemory = true;
		in.text = text;
		plan.inputs.push_back( in );
		plan.identity = IdentityPolicy::kNone;
		{
			std::unique_ptr<CompileContext> ctx = ProjectCompile::run( plan, resolver, diag );
			CHECK( ctx->records().size() == 1 );
			CHECK( ctx->record( 0 ).scope != nullptr );
			CHECK( ctx->record( 0 ).module != nullptr );
			CHECK( ctx->record( 0 ).role == ModuleRole::kStandalone );
			CHECK( ctx->userScope() == nullptr && ctx->preludeScope() == nullptr );
		}
		got = diag.diagnostics();
	}
	for ( const auto &d : got )
		CHECK( d.message.find( "canonical origin" ) == std::string::npos );

	// The pre-unit blangd shape, done directly on a fresh resolver.
	std::vector<Diagnostic> want;
	{
		Resolver resolver;
		DiagnosticEngine diag;
		DiagnosticEngine *prev = gDiag;
		gDiag = &diag;
		{
			SmartPtr<Scope> scope = resolver.newModuleScope();
			StringLexerReader reader( text, path );
			Lexer lexer( &reader );
			SmartPtr<Module> mod = Module::Parse( lexer, (Scope *)scope );
			if ( mod != nullptr )
			{
				stampDefiningOrigin( (Module *)mod, path );
				Sema::analyze( (Module *)mod, (Scope *)scope, diag );
			}
		}
		gDiag = prev;
		want = diag.diagnostics();
	}
	CHECK( !want.empty() );
	CHECK( got.size() == want.size() );
	for ( std::size_t i = 0; i < got.size() && i < want.size(); i++ )
	{
		CHECK( got[i].message == want[i].message );
		CHECK( got[i].severity == want[i].severity );
		CHECK( got[i].location.line == want[i].location.line );
		CHECK( got[i].location.col == want[i].location.col );
		CHECK( got[i].location.file == want[i].location.file );
	}
}

static void caseC7()
{
	const std::string bmod = gSrc + "/test_files/project_compile/wrong_format.bmod";
	const std::string user = gSrc + "/test_files/project_compile/dropped_user.b";

	// Premise: the fixture's marker really differs from this compiler's format,
	// so a future format bump fails loudly instead of voiding the case.
	{
		std::string first;
		std::ifstream f( bmod );
		std::getline( f, first );
		const std::string marker = "// blang-bmod-format:";
		CHECK( first.find( marker ) == 0 );
		int v = std::atoi( first.c_str() + marker.size() );
		CHECK( v != BlangBmod::kFormatVersion );
	}

	std::vector<std::string> paths = withBaseStdlib( user );
	paths.push_back( bmod );
	checkParity( paths, "C7" );
	RunOutcome got = runCase( combinePlan( paths ), false, [&]( const CompileContext &ctx )
	{
		long bi = findRecord( ctx, "/wrong_format.bmod" );
		CHECK( bi == 0 );   // interfaces first
		if ( bi < 0 )
			return;
		CHECK( ctx.record( bi ).role == ModuleRole::kInterface );
		CHECK( !ctx.record( bi ).recorded );
		CHECK( ctx.record( bi ).module == nullptr );
		CHECK( ctx.record( bi ).scope != nullptr );
		for ( std::size_t ri : ctx.moduleIndices() )
			CHECK( ri != (std::size_t)bi );
		CHECK( ctx.moduleIndices().size() == ctx.records().size() - 1 );
		CHECK( ctx.hadError() );
	} );
	CHECK( got.failed );
	CHECK( got.errorCount == 1 );
	CHECK( got.diagCount == 1 );
	CHECK( got.rendered.find( "wrong_format.bmod:1:1: error: interface file was produced by "
		"a different compiler version" ) != std::string::npos );
}

// C8 (KG-7 resolution back-references): an expression's link to the definition
// it resolved to (CallExpression/FunctionRefExpression -> FunctionDefinition,
// ConstructExpression -> StructDefinition, EnumConstructExpression ->
// EnumDefinition) is NON-owning — the resolving scope owns the definition. An
// owning link closed a cycle for a recursive function or a method constructing
// its own struct (stdlib/buffer.b's Buffer.slice), so the AST was never freed.
// Meaningful under build-asan: LeakSanitizer fails the process at exit.
static void caseC8()
{
	const std::string text =
		"struct B { int n; }\n"
		"impl B {\n"
		"    pub init(int c) { self.n = c; }\n"
		"    pub fn dup(self) -> B { return B(self.n); }\n"
		"}\n"
		"enum E { a(int), b }\n"
		"fn mk() -> E { return E.a(1); }\n"
		"fn f(int n) -> int { if n > 0 { return f(n - 1); } return 0; }\n"
		"fn apply(fn(int) -> int g, int v) -> int { return g(v); }\n"
		"fn main() -> int { B b = B(1); B c = b.dup(); E e = mk(); return f(c.n) + apply(f, 2); }\n";
	Resolver resolver;
	DiagnosticEngine diag;
	CompilePlan plan;
	CompileInput in;
	in.path = "/nonexistent/u1_kg7.b";
	in.inMemory = true;
	in.text = text;
	plan.inputs.push_back( in );
	plan.identity = IdentityPolicy::kNone;
	{
		std::unique_ptr<CompileContext> ctx = ProjectCompile::run( plan, resolver, diag );
		CHECK( ctx->record( 0 ).module != nullptr );
		CHECK( !ctx->hadError() );
	}
	CHECK( !diag.hasErrors() );
	for ( const auto &d : diag.diagnostics() )
		if ( d.severity == Severity::Error )
			std::fprintf( stderr, "C8 unexpected: %s\n", d.message.c_str() );
}

// C9 (modules-v2-lsp U3, audit R1 + addendum H3): the emitter's source-text
// provider. A generic body is sliced from the PROVIDED (parsed) text, never from
// the process-global disk line cache; a provider MISS is reported in `missing`,
// never silently filled from the cache or a fresh disk read.
static void caseC9()
{
	char tmpl[] = "/tmp/u3_c9_XXXXXX";
	const std::string dir = mkdtemp( tmpl );
	const std::string path = dir + "/lib.b";
	const std::string diskText =
		"pub fn wrap<T>(T v) -> T {\n    return v; // DISK-BODY\n}\n";
	const std::string bufText =
		"// buffer line 1\n// buffer line 2\npub fn wrap<T>(T v) -> T {\n    return v; // BUFFER-BODY\n}\n";
	{
		std::ofstream( path ) << diskText;
	}
	auto emitWith = [&]( const std::string &parseText, const QLang::BmodEmitter::SourceTextProvider *prov,
		std::vector<std::string> *missing ) -> std::string
	{
		std::string out;
		Resolver resolver;
		DiagnosticEngine diag;
		CompilePlan plan;
		CompileInput in;
		in.path = path;
		in.inMemory = true;
		in.text = parseText;
		plan.inputs.push_back( in );
		plan.identity = IdentityPolicy::kNone;
		{
			std::unique_ptr<CompileContext> ctx = ProjectCompile::run( plan, resolver, diag );
			CHECK( !ctx->hadError() && !diag.hasErrors() );
			std::vector<Module *> mods;
			for ( std::size_t ri : ctx->moduleIndices() )
				mods.push_back( const_cast<Module *>( (const Module *)ctx->record( ri ).module ) );
			std::ostringstream os;
			QLang::BmodEmitter::emit( mods, os, ctx->bmodEmitScope(), prov, missing );
			out = os.str();
		}
		return out;
	};

	// Seed the process-global cache from DISK (the qcc path, no provider).
	std::string viaDisk = emitWith( diskText, nullptr, nullptr );
	CHECK( viaDisk.find( "DISK-BODY" ) != std::string::npos );

	// A provider HIT: the buffer body, even though the cache holds the disk text
	// and the buffer's line numbers differ from the disk file's.
	QLang::BmodEmitter::SourceTextProvider hit = [&]( const std::string &p ) -> const std::string * {
		return p == path ? &bufText : nullptr;
	};
	std::vector<std::string> missing;
	std::string viaBuf = emitWith( bufText, &hit, &missing );
	CHECK( missing.empty() );
	CHECK( viaBuf.find( "BUFFER-BODY" ) != std::string::npos );
	CHECK( viaBuf.find( "DISK-BODY" ) == std::string::npos );

	// A provider MISS for the generic-defining source: reported, and the cache's
	// (pre-seeded, different) disk text is NOT what was emitted.
	QLang::BmodEmitter::SourceTextProvider miss = []( const std::string & ) -> const std::string * {
		return nullptr;
	};
	missing.clear();
	std::string viaMiss = emitWith( bufText, &miss, &missing );
	CHECK( missing.size() == 1 && missing[0] == path );
	CHECK( viaMiss.find( "DISK-BODY" ) == std::string::npos );
	CHECK( viaMiss.find( "wrap" ) == std::string::npos );

	unlink( path.c_str() );
	rmdir( dir.c_str() );
}

int main( int argc, char *argv[] )
{
	if ( argc < 3 )
	{
		std::fprintf( stderr, "FAIL usage: projectCompileTest <qcc-oracle> <source-root>\n" );
		return 1;
	}
	gQcc = argv[1];
	gSrc = argv[2];
	if ( access( gQcc.c_str(), X_OK ) != 0 )
	{
		std::fprintf( stderr, "FAIL qcc oracle not found/executable: %s\n", gQcc.c_str() );
		return 1;
	}

	caseC1();
	caseC2C3();
	caseC4();
	caseC5();
	caseC6();
	caseC7();
	caseC8();
	caseC9();

	if ( gFailures != 0 )
	{
		std::fprintf( stderr, "ProjectCompileTest: %d FAILURE(S)\n", gFailures );
		return 1;
	}
	std::printf( "ProjectCompileTest: OK\n" );
	return 0;
}
