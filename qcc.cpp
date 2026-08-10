#include <assert.h>
#include <climits>
#include <cstdlib>

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

#include "FileLexer.h"
#include "Type.h"
#include "Expression.h"

#include "CompilerHelpers.h"

#include "logging.h"

#include "BmodEmitter.h"
#include "LocationDumper.h"
#include "DiagnosticEngine.h"
#include "Sema.h"
#include "Frontend.h"
#include "Resolver.h"
#include "SchemaMigration.h"

#ifdef BLANG_HAS_LLVM
#include "CodeGen.h"
#include "llvm/Support/raw_ostream.h"
#endif

using namespace QLang;
using namespace std;

static void printUsage( const char *progName )
{
	std::cerr << "Usage: " << progName << " [options] <filename> [<filename> ...]" << std::endl;
	std::cerr << "Options:" << std::endl;
#ifdef BLANG_HAS_LLVM
	std::cerr << "  -S, --emit-ir     Emit LLVM IR (.ll file)" << std::endl;
	std::cerr << "  -c, --emit-obj    Emit object file (.o file)" << std::endl;
	std::cerr << "  -o, --output FILE Output file name" << std::endl;
	std::cerr << "  --parse-only      Parse only, no code generation" << std::endl;
	std::cerr << "  --combine         Combine all .b files into a single .ll output" << std::endl;
	std::cerr << "  --emit-test-main  Emit a main() that runs test{} blocks via the test driver" << std::endl;
#endif
	std::cerr << "  --dump-locations  Print <file>:<line>:<col> <NodeKind> per AST node and exit" << std::endl;
	std::cerr << "  --emit-bmod FILE  Emit .bmod interface file" << std::endl;
	std::cerr << "  -v, --verbose     Emit parse-progress/trace output (quiet by default)" << std::endl;
	std::cerr << "  --debug-compiler  Show compiler-internal detail on errors (throw site, raw IR)" << std::endl;
	std::cerr << "  -h, --help        Show this help" << std::endl;
}

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		printUsage( argv[0] );
		return -1;
	}

	bool emitIR = false;
	bool emitObj = false;
	bool parseOnly = false;
	bool combineMode = false;
	bool dumpLocations = false;
	bool verbose = false;
	bool debugCompiler = false;
	bool jsonDiagnostics = false;   // --json: emit diagnostics as a JSON array
	bool werror = false;            // -Werror: promote warnings to errors (exit)
	std::string optLevel;           // -O<n>: in-process IR optimization level
	bool debugInfo = false;         // -g: emit DWARF debug info (U3)
	bool emitTestMain = false;
	std::string outputFile;
	std::string emitBmodFile;
	std::string emitSchemaFile;
	std::vector<std::string> inputFiles;
	// Database config forwarded by bcc from blang.toml [database].
	std::string dbDriver;
	std::string dbUrl;
	struct DbConnArg { std::string name, driver, url; };
	std::vector<DbConnArg> dbNamedConns;
	// modules-v2-graph U1 (D5/D10): canonical module identity, supplied by the
	// driver (bcc). --module-origin is the canonical origin (realpath of the
	// project dir) of the module being compiled; --bmod-origin <bmodpath>=<origin>
	// gives a directly-imported dep's canonical origin so its .bmod definitions are
	// stamped with the DEP's identity (clarity-note source (a)), keeping the
	// consumer's mangled generic symbols identical to the dep's own — so
	// linkonce_odr dedup holds across the module boundary. Absent (single-file /
	// combine, no bcc) => each input defaults to realpath(inputFile).
	std::string moduleOrigin;
	std::map<std::string, std::string> bmodOrigins;

	for ( int i = 1; i < argc; i++ )
	{
		std::string arg = argv[i];
		if ( arg == "-S" || arg == "--emit-ir" )
			emitIR = true;
		else if ( arg == "-c" || arg == "--emit-obj" )
			emitObj = true;
		else if ( arg == "--parse-only" )
			parseOnly = true;
		else if ( arg == "--dump-locations" )
		{
			// Print one <file>:<line>:<col> <NodeKind> line per AST node,
			// then exit. Implies parse-only; no LLVM dependency.
			dumpLocations = true;
			parseOnly = true;
		}
		else if ( arg == "--combine" )
			combineMode = true;
		else if ( arg == "--emit-test-main" )
			emitTestMain = true;
		else if ( arg == "-v" || arg == "--verbose" )
			verbose = true;
		else if ( arg == "--debug-compiler" )
			debugCompiler = true;
		else if ( arg == "--json" )
			jsonDiagnostics = true;
		else if ( arg == "-Werror" )
			werror = true;
		else if ( arg == "-O" )
			optLevel = "2";                 // bare -O means -O2 (gcc convention)
		else if ( arg.size() > 2 && arg.substr( 0, 2 ) == "-O" )
			optLevel = arg.substr( 2 );     // -O0/1/2/3/s/z; validated at optimize()
		else if ( arg == "-g" )
			debugInfo = true;               // -g: emit DWARF debug info (U3)
		else if ( arg == "--emit-bmod" )
		{
			if ( i + 1 < argc )
				emitBmodFile = argv[++i];
			else
			{
				std::cerr << "Error: --emit-bmod requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "-o" || arg == "--output" )
		{
			if ( i + 1 < argc )
				outputFile = argv[++i];
			else
			{
				std::cerr << "Error: " << arg << " requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "--emit-schema" )
		{
			if ( i + 1 < argc )
				emitSchemaFile = argv[++i];
			else
			{
				std::cerr << "Error: --emit-schema requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "--db-driver" )
		{
			if ( i + 1 < argc )
				dbDriver = argv[++i];
			else
			{
				std::cerr << "Error: --db-driver requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "--db-url" )
		{
			if ( i + 1 < argc )
				dbUrl = argv[++i];
			else
			{
				std::cerr << "Error: --db-url requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "--db-conn" )
		{
			// --db-conn <name> <driver> <url>
			if ( i + 3 < argc )
			{
				DbConnArg c;
				c.name = argv[++i];
				c.driver = argv[++i];
				c.url = argv[++i];
				dbNamedConns.push_back( c );
			}
			else
			{
				std::cerr << "Error: --db-conn requires <name> <driver> <url>" << std::endl;
				return -1;
			}
		}
		else if ( arg == "--module-origin" )
		{
			if ( i + 1 < argc )
				moduleOrigin = argv[++i];
			else
			{
				std::cerr << "Error: --module-origin requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "--bmod-origin" )
		{
			// --bmod-origin <bmodpath>=<canonical-origin>
			if ( i + 1 < argc )
			{
				std::string spec = argv[++i];
				size_t eq = spec.rfind( '=' );
				if ( eq == std::string::npos )
				{
					std::cerr << "Error: --bmod-origin expects <bmodpath>=<origin>" << std::endl;
					return -1;
				}
				bmodOrigins[spec.substr( 0, eq )] = spec.substr( eq + 1 );
			}
			else
			{
				std::cerr << "Error: --bmod-origin requires an argument" << std::endl;
				return -1;
			}
		}
		else if ( arg == "-h" || arg == "--help" )
		{
			printUsage( argv[0] );
			return 0;
		}
		else if ( arg[0] == '-' )
		{
			std::cerr << "Unknown option: " << arg << std::endl;
			return -1;
		}
		else
		{
			inputFiles.push_back( arg );
		}
	}

	if ( inputFiles.empty() )
	{
		std::cerr << "Error: no input file specified" << std::endl;
		return -1;
	}

	// Install the single diagnostic reporting path for this process. The
	// top-level parse-catch (Module::Parse) renders located errors through it.
	DiagnosticEngine diagnostics;
	diagnostics.setDebugCompiler( debugCompiler );
	diagnostics.setJson( jsonDiagnostics );
	diagnostics.setWerror( werror );
	gDiag = &diagnostics;

	// modules-v2-graph U4: resolution runs through the standalone Resolver
	// component (Resolver.h) — the same class lsp/Compile.cpp constructs (the
	// Epic C seam). It OWNS the global builtin scope and installs the `gScope`
	// alias the parser/sema read (replacing the inline createGlobalScope() +
	// SmartPtr owner). Behavior-neutral: the same builtin scope, the same alias.
	// The combine routing policy and the .bmod flat-merge injection stay in this
	// driver (their removal is done-condition 6 / U6).
	Resolver resolver;

	// Parse each input file into its own Module. Each module gets its own
	// module-level scope parented to the shared global scope so that built-in
	// types are visible everywhere but top-level symbols remain per-file.
	// Cross-module symbol resolution (pub visibility enforcement) will be
	// layered on top once multi-module linking is implemented (Task 63).
	// Phase 1: Parse .bmod files first (they provide type info for .b files).
	// Build a map from module name to its parsed scope for import resolution.
	std::vector<SmartPtr<Module>> modules;
	std::map<std::string, Module*> bmodMap;

	// modules-v2-graph U6a: a .bmod dependency's public interface is registered so
	// resolution runs through the IMPORT GRAPH, not a flat merge into gScope.
	//
	//   - FREE FUNCTIONS land in a PER-MODULE NAMESPACE scope keyed by the dep's
	//     import name and registered on gScope (the common ancestor of both the
	//     combine user scope and a non-combine module scope, so `mathlib.add`
	//     resolves in either build). They are NO LONGER added to gScope's symbol
	//     list, so an UNQUALIFIED `add(3,4)` no longer resolves — that is the
	//     import-enforcement half of done-condition 6: a dep symbol is reachable
	//     only as `module.name` after `import module;` (D1). The namespace path is
	//     the same one the namespaced stdlib already uses (QExpression.cpp:251).
	//
	//   - TYPES (struct/enum + their Type entries) and PROTOCOLS still land in
	//     gScope. This is the deliberate U6a/U6b SPLIT: naming a foreign type
	//     (declare a variable of it, construct one) is D7 *name-capability*, whose
	//     enforcement + the accompanying capability/collision diagnostics are U6b.
	//     Keeping type names resolvable here is the bridge that lets the type-using
	//     consumers (Pair/Counter/Point/Box/Todo construction, methods, to_json,
	//     query) stay green while U6a lands only the FUNCTION mechanism + migration.
	//     It also preserves U5's parse-time foreign-type resolution (midx.bmod's
	//     `-> Box<int>` where Box is defined in boxq.bmod), since this is still
	//     called INCREMENTALLY in topological order (a dependency before its
	//     dependent).
	auto injectBmodSymbols = [&]( Module *bmod, const std::string &moduleName )
	{
		// Per-module export namespace for this dep's free functions. Registered on
		// gScope so a consumer in either build mode reaches it up the parent chain.
		Scope *depNs = gScope->findNamespace( moduleName );
		if ( depNs == nullptr )
		{
			depNs = new Scope( Scope::kScope_Namespace );
			depNs->setParent( gScope );
			gScope->addNamespace( moduleName, depNs );
		}
		for ( const auto &sp : bmod->getFunctionList() )
		{
			FunctionDefinition *f = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
			if ( f->isPublic() )
			{
				if ( !f->isGeneric() )
					f->setFunctionExtern( true );
				// Namespace only — NOT gScope. Qualified access resolves it here;
				// unqualified access is a located error (enforcement).
				depNs->addSymbol( f );
			}
		}
		for ( const auto &sp : bmod->getStructList() )
		{
			StructDefinition *s = const_cast<StructDefinition*>( (const StructDefinition*)sp );
			if ( s->isPublic() )
			{
				// Bridge (U6b will re-key on name-capability): keep the type namable.
				gScope->addSymbol( s );
				gScope->addType( new Type( s->getName() ) );
			}
		}
		for ( const auto &sp : bmod->getEnumList() )
		{
			EnumDefinition *e = const_cast<EnumDefinition*>( (const EnumDefinition*)sp );
			if ( e->isPublic() )
			{
				gScope->addSymbol( e );
				gScope->addType( new Type( e->getName() ) );
			}
		}
		for ( const auto &sp : bmod->getProtocolList() )
		{
			ProtocolDefinition *p = const_cast<ProtocolDefinition*>( (const ProtocolDefinition*)sp );
			if ( p->isPublic() )
				gScope->addSymbol( p );
		}
	};

	// Separate input files into .bmod and .b
	std::vector<std::string> bmodFiles, sourceFiles;
	for ( const auto &f : inputFiles )
	{
		if ( f.size() >= 5 && f.substr( f.size() - 5 ) == ".bmod" )
			bmodFiles.push_back( f );
		else
			sourceFiles.push_back( f );
	}
	// Reorder: .bmod first, then .b
	std::vector<std::string> orderedFiles;
	orderedFiles.insert( orderedFiles.end(), bmodFiles.begin(), bmodFiles.end() );
	orderedFiles.insert( orderedFiles.end(), sourceFiles.begin(), sourceFiles.end() );
	inputFiles = orderedFiles;

	// In combine mode, create a shared scope for all .b files.
	// Stdlib .b files get their own namespace scopes registered on combineScope.
	// The user's .b file (last in order) uses combineScope directly.
	Scope *combineScope = nullptr;
	// Track module names for namespace scopes in combine mode
	std::map<std::string, Scope*> moduleNamespaces;
	// modules-v2-graph U3 (D12/D13): prelude LOAD/PROMOTE tracking.
	//   preludeModuleNames  — stdlib modules that PROVIDE prelude types (buffer,
	//                         collections). LOADED into a holding namespace scope
	//                         (never combineScope), then a post-parse PROMOTE pass
	//                         injects their prelude TYPES into combineScope.
	//   preludePrefixExempt — those same modules compile PREFIX-FREE (Option B): a
	//                         prelude type used unqualified must not carry a module
	//                         prefix (esp. non-generic Buffer).
	//   suppressedPrelude   — prelude type names shadowed by a user definition; the
	//                         prelude one is suppressed uniformly (not promoted; and
	//                         filtered from allStructs so mStructDefMap gets the
	//                         user's) so "user definition wins" across all three
	//                         registries (mTypeList, mSymbolList, mStructDefMap).
	std::set<std::string> preludeModuleNames;
	std::set<std::string> preludePrefixExempt;
	std::set<std::string> suppressedPrelude;
	// Module name (basename, no extension) parallel to `modules`, for the PROMOTE
	// pass and the allStructs shadow filter.
	std::vector<std::string> moduleNames;
	// modules-v2-graph U3 (D13): the PRELUDE scope sits between core (gScope) and
	// the user scope (combineScope). Promoted prelude types (Map/Set/Buffer) live
	// here — "base scope like int," never imported. Because combineScope is its
	// CHILD, a user's own `struct Map` in combineScope shadows the prelude Map by
	// normal child-first lookup, UNIFORMLY across both scope registries (mTypeList
	// and mSymbolList) with no addSymbol/addType disagreement. (mStructDefMap, a
	// flat CodeGen map, is reconciled separately by the allStructs shadow filter.)
	Scope *preludeScope = nullptr;
	if ( combineMode )
	{
		// U4: module scopes come from the resolver (parented into its environment).
		preludeScope = resolver.newModuleScope();               // parent = global
		combineScope = resolver.newModuleScope( preludeScope );
	}

	// Quiet by default (FR-007): parser progress is a gated trace on STDERR
	// (Frontend.h), enabled by -v. stdout carries only machine output (the
	// --dump-locations node dump), so no cout redirection games are needed —
	// the old rdbuf-swap hack (and the process-teardown SIGSEGV it once
	// caused) is gone with the unconditional couts it was hiding.
	setParseTraceEnabled( verbose && !dumpLocations );

	// U1: accumulate failure across all files instead of aborting at the first,
	// so one compile reports every file's diagnostics. Buffered diagnostics are
	// rendered once by gDiag->finish() after the loop; codegen runs only if no
	// errors remain (Constitution III).
	bool hadError = false;

	for ( std::size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++ )
	{
		const auto &inputFile = inputFiles[fileIdx];
		bool isBmod = ( inputFile.size() >= 5 &&
			inputFile.substr( inputFile.size() - 5 ) == ".bmod" );

		Scope *fileScope;
		if ( combineMode && !isBmod )
		{
			// Derive module name from filename: "stdlib/sys.b" -> "sys"
			std::string moduleName;
			{
				std::string fname = inputFile;
				size_t slash = fname.rfind( '/' );
				if ( slash != std::string::npos )
					fname = fname.substr( slash + 1 );
				size_t dot = fname.rfind( '.' );
				if ( dot != std::string::npos )
					fname = fname.substr( 0, dot );
				moduleName = fname;
			}

			// Last source file is the user's code — use combineScope directly.
			// Stdlib files (not last) get their own namespace scope.
			//
			// modules-v2-graph U3 (D12/D13): three type tiers.
			//   - PRELUDE modules (buffer, collections) PROVIDE prelude TYPES
			//     (Map/Set/Buffer). They are LOADED into a holding namespace scope
			//     (prefix-exempt), NOT combineScope: their parse-time addSymbol/
			//     addType land in the namespace, never combineScope's registries.
			//     A post-parse PROMOTE pass then injects only their prelude types
			//     into combineScope, suppression-checked (§5b) — so "user wins"
			//     shadowing is uniform. collections' free function `sort` stays in
			//     the `collections` namespace: qualified `collections.sort` (library
			//     tier). This LOAD-vs-PROMOTE split closes the mSymbolList seam at
			//     its source (B2-a): no prelude symbol is ever committed to
			//     combineScope at parse time.
			//   - `cli` is an ORDINARY namespaced library module (modules-v2-graph
			//     U6, closes KG-6): qualified `cli.has_flag(...)`, module-prefixed
			//     like every other stdlib namespace (net/fs/timer). Its former
			//     promotion into combineScope was a CODEGEN special case — the
			//     module-prefix string-ARC double-free workaround — now that U2 has
			//     fixed+regression-locked that path (F3), the promotion is retired
			//     and `cli` falls through to the plain namespace routing below. Its
			//     internal string-returning calls (has_flag -> flag_name_of) are the
			//     exact prefixed shape U2's nsarc fixture proves --leak-check clean.
			bool isUserFile = ( fileIdx == inputFiles.size() - 1 );
			bool isPreludeModule =
				( moduleName == "buffer" || moduleName == "collections" );
			if ( isUserFile )
			{
				fileScope = combineScope;
			}
			else if ( isPreludeModule )
			{
				// LOAD into a prefix-free holding namespace scope; PROMOTE later.
				Scope *nsScope = new Scope( Scope::kScope_Namespace );
				nsScope->setParent( combineScope );
				combineScope->addNamespace( moduleName, nsScope );
				moduleNamespaces[moduleName] = nsScope;
				preludeModuleNames.insert( moduleName );
				preludePrefixExempt.insert( moduleName );
				fileScope = nsScope;
			}
			else
			{
				// Create a namespace scope for this stdlib module
				Scope *nsScope = new Scope( Scope::kScope_Namespace );
				nsScope->setParent( combineScope );
				combineScope->addNamespace( moduleName, nsScope );
				moduleNamespaces[moduleName] = nsScope;
				fileScope = nsScope;
			}
		}
		else
		{
			// U4: non-combine module scope from the resolver (parent = global).
			fileScope = resolver.newModuleScope();
		}

		// modules-v2-graph U5/U6a: a .bmod's public interface is registered
		// INCREMENTALLY as each .bmod parses (injectBmodSymbols, above), in the
		// transitive-closure's topological order — so a later .bmod referencing an
		// earlier one's foreign types resolves at parse time. U6a moved the FREE
		// FUNCTIONS out of the gScope flat merge into a per-module namespace keyed
		// by the dep's import name (qualified `module.fn` after `import module;`);
		// TYPES stay in gScope as the name-capability bridge (U6b). This is the
		// import-graph resolution DC6 requires — the flat merge of callable symbols
		// is retired.

		// modules-v2-graph U5 (done-condition 7): pre-scan a .bmod for FOREIGN-TYPE
		// headers and register each named type BEFORE parsing, so signatures that
		// reference a type owned by another module (`-> Box<int>`, Box from boxq)
		// resolve — the interface parses STANDALONE (the bmod_parses check) even
		// when the defining .bmod is not also on the command line. In a real build
		// the transitive closure has already injected the real definition (with its
		// digest); this only supplies a placeholder type name when it has not.
		// SECURITY: the .bmod is untrusted parsed input — a malformed foreign-ref is
		// a LOCATED error, never a crash (Quality Gate 7).
		if ( isBmod )
		{
			std::ifstream fin( inputFile );
			std::string fline;
			int lineNo = 0;
			while ( std::getline( fin, fline ) )
			{
				lineNo++;
				const std::string tag = "// foreign-type:";
				size_t at = fline.find( tag );
				if ( at == std::string::npos )
					continue;
				std::istringstream fs( fline.substr( at + tag.size() ) );
				std::string fname2, fhuman, fdigest, extra;
				fs >> fname2 >> fhuman >> fdigest;
				if ( fname2.empty() || fhuman.empty() || fdigest.empty() ||
					 ( fs >> extra ) )
				{
					SourceLocation floc;
					floc.file = inputFile;
					floc.line = lineNo;
					floc.col = 1;
					gDiag->error( floc,
						"malformed foreign-type reference in interface file "
						"(expected '// foreign-type: <name> <module> <digest>')" );
					hadError = true;
					continue;
				}
				if ( gScope->findType( fname2 ) == nullptr )
					gScope->addType( new Type( fname2 ) );
			}
			if ( hadError )
				continue;
		}

		LexerReader reader( inputFile.c_str() );
		Lexer l( &reader );
		// Per-token "Symbol …" trace only under -v, never in --dump-locations.
		l.setTraceEnabled( verbose && !dumpLocations );

		SmartPtr<Module> mod = Module::Parse( l, fileScope );
		if ( mod == nullptr )
		{
			// Catastrophic (unrecoverable) parse failure for this file. The
			// located diagnostic was already buffered; record failure and move on
			// so remaining files still report their errors.
			hadError = true;
			continue;
		}

		// Stamp each struct with the SOURCE FILE that defines it. Applied to .b
		// and .bmod inputs alike, because the namespaced stdlib modules have a
		// real module boundary but arrive as .b source. Shared with blangd via
		// stampDefiningOrigin so the compiler and the editor cannot disagree.
		//
		// Populated here, enforced by a later unit. Nothing reads it yet — and
		// nothing may, until the file -> module mapping exists (M-3).
		stampDefiningOrigin( (Module *)mod, inputFile );

		// modules-v2-graph U1: stamp each struct with its DEFINING module's
		// canonical-identity digest (D5/D10) so generic mangling can key on it.
		// Origin source (clarity-note source (a), driver-side):
		//   - a dependency's .bmod: the dep's origin passed via --bmod-origin
		//     (so its generics mangle identically to the dep's own build => dedup);
		//   - a compiled .b module: --module-origin when bcc supplies it, else
		//     realpath(inputFile) in single-file/combine mode (self-consistent —
		//     no cross-.bmod reference exists there to disagree with).
		{
			std::string origin;
			if ( isBmod )
			{
				auto oit = bmodOrigins.find( inputFile );
				if ( oit != bmodOrigins.end() )
					origin = oit->second;
			}
			else if ( !moduleOrigin.empty() )
			{
				origin = moduleOrigin;
			}
			if ( origin.empty() )
			{
				char resolved[PATH_MAX];
				if ( realpath( inputFile.c_str(), resolved ) )
					origin = resolved;
				else
				{
					// realpath failure is a located hard error, NEVER an empty
					// identity (design-audit-U1 §2.1): an empty/degenerate digest
					// would collapse distinct modules onto one symbol (a P10-class
					// miscompile through the back door).
					SourceLocation oloc;
					oloc.file = inputFile;
					oloc.line = 1;
					oloc.col = 1;
					gDiag->error( oloc,
						"cannot resolve a canonical origin for module identity "
						"(realpath failed for '" + inputFile + "')" );
					hadError = true;
					continue;
				}
			}
			stampModuleDigest( (Module *)mod, blangModuleDigest( origin ) );
		}

		if ( isBmod )
		{
			// Validate the interface FORMAT VERSION before trusting the file.
			// Without this the marker is write-only: a format-2 .bmod read by a
			// format-3 compiler would silently invert the meaning of every
			// unmarked `init` (exported in 2, private in 3) — the exact inversion
			// the marker exists to prevent. A mismatch is a located error naming
			// the fix, not a mysterious downstream failure.
			{
				std::ifstream bin( inputFile );
				std::string bline;
				int fileVersion = -1;
				for ( int probe = 0; probe < 8 && std::getline( bin, bline ); probe++ )
				{
					const std::string marker = "// blang-bmod-format:";
					size_t at = bline.find( marker );
					if ( at != std::string::npos )
					{
						fileVersion = atoi( bline.c_str() + at + marker.size() );
						break;
					}
				}
				// No marker at all == format 1 (pre-versioned, emitted before the
				// marker existed).
				if ( fileVersion < 0 )
					fileVersion = 1;
				if ( fileVersion != BlangBmod::kFormatVersion )
				{
					SourceLocation bloc;
					bloc.file = inputFile;
					bloc.line = 1;
					bloc.col = 1;
					gDiag->error( bloc,
						"interface file was produced by a different compiler version "
						"(.bmod format " + std::to_string( fileVersion ) +
						", this compiler expects " +
						std::to_string( BlangBmod::kFormatVersion ) +
						"); rebuild the dependency" );
					hadError = true;
					continue;
				}
			}

			mod->setExtern( true );

			// Stamp ABI provenance on every struct this interface declares.
			// Construction of a struct that arrived through a .bmod must go
			// through the library-emitted factory (the consumer has no field
			// layout to size or destroy it with); construction of a struct
			// defined in this compilation keeps the inline path. This is
			// deliberately NOT the flat-merge injection below — it only marks
			// where a definition came from.
			for ( const auto &sp : mod->getStructList() )
			{
				StructDefinition *s = const_cast<StructDefinition*>(
					(const StructDefinition*)sp );
				s->setFromInterface( true );
			}

			// Extract module name: "/path/to/foo.bmod" -> "foo"
			std::string fname = inputFile;
			size_t slash = fname.rfind( '/' );
			if ( slash != std::string::npos )
				fname = fname.substr( slash + 1 );
			size_t dot = fname.rfind( '.' );
			if ( dot != std::string::npos )
				fname = fname.substr( 0, dot );
			bmodMap[fname] = mod;
			// U5/U6a: register NOW (topological order) so a subsequent .bmod
			// referencing this one's foreign types resolves at parse time. `fname`
			// (the .bmod basename) is the dep's import name — the key for its
			// per-module function namespace.
			injectBmodSymbols( (Module *)mod, fname );
		}

		modules.push_back( mod );
		{
			// Parallel module name (basename, no extension) for the U3 PROMOTE
			// pass and the allStructs shadow filter. Kept in lockstep with
			// `modules` so index alignment holds even if a file fails to parse.
			std::string bn = inputFile;
			size_t sl = bn.rfind( '/' );
			if ( sl != std::string::npos )
				bn = bn.substr( sl + 1 );
			size_t dt = bn.rfind( '.' );
			if ( dt != std::string::npos )
				bn = bn.substr( 0, dt );
			moduleNames.push_back( bn );
		}
		PARSE_TRACE( "Completed parse" );
		// Semantic analysis (U3): runs in ALL build modes, immediately after a
		// non-extern module parses and before any code generation, resolving
		// member references and annotating expression types through the single
		// DiagnosticEngine. Extern .bmod modules provide types only and are not
		// analyzed (Sema::analyze skips them). On any sema error the compile
		// fails (non-zero exit) and codegen is not reached for this file.
		if ( !isBmod && !Sema::analyze( (Module *)mod, fileScope, *gDiag ) )
			hadError = true;

		// modules-v2-graph U3 EAGER PROMOTE (D13): if the just-parsed module is a
		// prelude PROVIDER (buffer/collections), inject its prelude TYPES into
		// preludeScope NOW — before the user file (parsed last) reaches its own
		// sema — so `Map`/`Set`/`Buffer` resolve unqualified with zero imports.
		// Only prelude-manifest types are promoted; a mixed module's free functions
		// (collections.sort) stay in its namespace scope for qualified access
		// (library tier). Shadowing is uniform and automatic: combineScope is a
		// CHILD of preludeScope, so a user's own same-named struct wins by
		// child-first lookup in BOTH scope registries — no addSymbol/addType
		// disagreement (that is B2-a closed at its source).
		if ( combineMode && preludeScope != nullptr && !moduleNames.empty() &&
			 preludeModuleNames.count( moduleNames.back() ) )
		{
			for ( const auto &sp : mod->getStructList() )
			{
				StructDefinition *s = const_cast<StructDefinition*>(
					(const StructDefinition*)sp );
				if ( isPreludeTypeName( s->getName() ) )
				{
					preludeScope->addSymbol( s );
					preludeScope->addType( new Type( s->getName() ) );
				}
			}
		}
	}

	// U1: stop before any output-producing stage if the compile had errors —
	// never codegen a rejected program (Constitution III). finish() renders all
	// buffered diagnostics once (human text or --json array). On the success
	// path finish() is DEFERRED to the exit points below so the codegen phase
	// can buffer its own located errors through the same engine (U-last) and
	// everything — codegen errors and warnings alike — still renders exactly
	// once, in one JSON array under --json.
	if ( hadError || gDiag->hasErrors() )
	{
		gDiag->finish();
		return 1;
	}

	// --dump-locations: restore stdout and print one line per AST node for
	// each parsed source module (command-line order), then exit. This is
	// the entire stdout of a dump run.
	if ( dumpLocations )
	{
		// the RAII guard so it does not restore a second time.
		for ( auto &mod : modules )
		{
			if ( !mod->isExtern() )
				LocationDumper::dump( (Module*)mod, std::cout );
		}
		gDiag->finish();   // flush warnings (stderr; stdout stays pure)
		return 0;
	}

	// Emit .bmod interface file if requested (runs after parsing, before codegen)
	if ( !emitBmodFile.empty() )
	{
		std::vector<Module*> modPtrs;
		for ( auto &mod : modules )
		{
			if ( !mod->isExtern() )
				modPtrs.push_back( mod );
		}

		std::ofstream bmodOut( emitBmodFile );
		if ( !bmodOut.is_open() )
		{
			cerr << "Error: cannot open " << emitBmodFile << " for writing" << endl;
			gDiag->finish();
			return -1;
		}
		QLang::BmodEmitter::emit( modPtrs, bmodOut, gScope );
		PARSE_TRACE( "Wrote .bmod to " << emitBmodFile );
	}

	// Emit the current schema (table structs) as JSON for `bcc migrate`.
	if ( !emitSchemaFile.empty() )
	{
		std::vector<SmartPtr<StructDefinition>> tableStructs;
		for ( auto &mod : modules )
		{
			if ( mod->isExtern() )
				continue;
			for ( auto &s : mod->getStructList() )
				tableStructs.push_back( s );
		}

		QLang::SchemaMigration mig;
		mig.extractSchema( tableStructs );
		if ( !mig.saveSchema( emitSchemaFile ) )
		{
			cerr << "Error: cannot write schema to " << emitSchemaFile << endl;
			gDiag->finish();
			return -1;
		}
		PARSE_TRACE( "Wrote schema to " << emitSchemaFile );
		gDiag->finish();
		return 0;
	}

#ifdef BLANG_HAS_LLVM
	if ( !parseOnly )
	{
		// Collect all struct and enum definitions across all modules for
		// cross-module type sharing (Task 67).
		// modules-v2-graph U3 (D13, §5b): compute which prelude type names a USER
		// module shadows, so the prelude provider's struct of that name is filtered
		// out of allStructs below — making CodeGen's flat mStructDefMap agree with
		// the scope registries (the user's definition wins in all three). A "user
		// module" is one routed to combineScope: not a prelude provider and not a
		// namespaced stdlib module (i.e. the user file, or cli).
		std::set<std::string> userTypeNames;
		for ( std::size_t idx = 0; idx < modules.size() && idx < moduleNames.size(); idx++ )
		{
			const std::string &mn = moduleNames[idx];
			bool routedToCombine =
				!preludeModuleNames.count( mn ) && !moduleNamespaces.count( mn );
			if ( routedToCombine )
				for ( auto &s : modules[idx]->getStructList() )
					userTypeNames.insert( s->getName() );
		}
		for ( const auto &n : userTypeNames )
			if ( isPreludeTypeName( n ) )
				suppressedPrelude.insert( n );

		std::vector<SmartPtr<StructDefinition>> allStructs;
		std::vector<SmartPtr<EnumDefinition>> allEnums;
		for ( std::size_t idx = 0; idx < modules.size(); idx++ )
		{
			bool isPreludeProvider =
				( idx < moduleNames.size() ) && preludeModuleNames.count( moduleNames[idx] );
			for ( auto &s : modules[idx]->getStructList() )
			{
				// Shadowed prelude struct: drop it so mStructDefMap holds the
				// user's same-named definition (the scope registries already do).
				if ( isPreludeProvider && suppressedPrelude.count( s->getName() ) )
					continue;
				allStructs.push_back( s );
			}
			for ( auto &e : modules[idx]->getEnumList() )
				allEnums.push_back( e );
		}

		if ( combineMode )
		{
			// Combined mode: all .b files compile into a single .ll output.
			// Create one CodeGen instance and generate all modules into it.
			std::string combinedName = "combined";
			if ( !outputFile.empty() )
				combinedName = outputFile;
			else if ( !sourceFiles.empty() )
			{
				// Use the last source file (user code) as the module name
				combinedName = sourceFiles.back();
				size_t dot = combinedName.rfind( '.' );
				if ( dot != std::string::npos )
					combinedName = combinedName.substr( 0, dot );
			}

			QLang::CodeGen codegen( combinedName.c_str() );
			codegen.setTestMode( emitTestMain );
			codegen.setDebugInfo( debugInfo );
			codegen.registerExternalTypes( allStructs, allEnums );
			codegen.setDbConfig( dbDriver, dbUrl );
			for ( auto &c : dbNamedConns )
				codegen.addDbNamedConn( c.name, c.driver, c.url );

			for ( std::size_t idx = 0; idx < modules.size(); idx++ )
			{
				if ( modules[idx]->isExtern() )
					continue;

				// Determine module prefix for namespace name mangling.
				// Stdlib modules (those with a registered namespace) get a prefix;
				// the user's code (last source file) gets no prefix.
				std::string modPrefix;
				{
					std::string fname = inputFiles[idx];
					size_t slash = fname.rfind( '/' );
					if ( slash != std::string::npos )
						fname = fname.substr( slash + 1 );
					size_t dot = fname.rfind( '.' );
					if ( dot != std::string::npos )
						fname = fname.substr( 0, dot );
					// Prelude providers (buffer/collections) are namespaced for
					// resolution (qualified collections.sort) but compile PREFIX-FREE
					// (modules-v2-graph U3, Option B): a prelude type used unqualified
					// must not carry a module prefix — decisive for non-generic
					// Buffer, whose methods would otherwise mangle `buffer__Buffer_*`.
					if ( moduleNamespaces.count( fname ) > 0 &&
						 !preludePrefixExempt.count( fname ) )
						modPrefix = fname;
				}
				codegen.setModulePrefix( modPrefix );

				if ( !codegen.generate( modules[idx] ) )
				{
					// The located diagnostics say what failed; add the generic
					// line only if codegen somehow failed without buffering one.
					if ( !gDiag->hasErrors() )
						cerr << "Code generation failed for " << inputFiles[idx] << endl;
					gDiag->finish();
					return 1;
				}
			}

			// Clear module prefix after all modules are generated
			codegen.setModulePrefix( "" );

			if ( !codegen.verify() )
			{
				cerr << "internal compiler error: generated IR failed verification; please report this bug" << endl;
				if ( debugCompiler )
					cerr << codegen.getVerifyError() << endl;
				gDiag->finish();
				return -1;
			}

			// Layer 1 of -O: run the in-process IR optimization pipeline (opt-in;
			// empty/-O0 leaves the module byte-identical to the unoptimized build),
			// then re-verify (opt must not produce invalid IR).
			if ( !optLevel.empty() && optLevel != "0" )
			{
				if ( !codegen.optimize( optLevel ) )
				{
					cerr << "error: invalid optimization level '-O" << optLevel << "'" << endl;
					gDiag->finish();
					return -1;
				}
				if ( !codegen.verify() )
				{
					cerr << "internal compiler error: IR failed verification after optimization; please report this bug" << endl;
					if ( debugCompiler )
						cerr << codegen.getVerifyError() << endl;
					gDiag->finish();
					return -1;
				}
			}

			// Determine output IR file path
			std::string irFile;
			if ( !outputFile.empty() )
			{
				irFile = outputFile;
			}
			else if ( !sourceFiles.empty() )
			{
				// Derive from last source file (the user's main file)
				irFile = sourceFiles.back();
				size_t dot = irFile.rfind( '.' );
				if ( dot != std::string::npos )
					irFile = irFile.substr( 0, dot );
				irFile += ".ll";
			}
			else
			{
				irFile = "combined.ll";
			}

			// Print IR to stdout
			codegen.print( llvm::outs() );

			// Write IR to file
			std::error_code ec;
			llvm::raw_fd_ostream outFile( irFile, ec );
			if ( !ec )
			{
				codegen.print( outFile );
				PARSE_TRACE( "Wrote IR to " << irFile );
			}
			else
			{
				cerr << "Failed to write " << irFile << ": " << ec.message() << endl;
			}
		}
		else
		{
			// Normal mode: each .b file gets its own .ll output.
			// Code generation: process each parsed module in order.
			// Skip extern-only modules (.bmod files) — they provide type info only.
			for ( std::size_t idx = 0; idx < modules.size(); idx++ )
			{
				if ( modules[idx]->isExtern() )
					continue;

				const std::string &inputFile = inputFiles[ idx ];
				QLang::CodeGen codegen( inputFile.c_str() );
				codegen.setTestMode( emitTestMain );
				codegen.setDebugInfo( debugInfo );
				codegen.setDbConfig( dbDriver, dbUrl );
				for ( auto &c : dbNamedConns )
					codegen.addDbNamedConn( c.name, c.driver, c.url );

				// Register types from all other modules before generating
				codegen.registerExternalTypes( allStructs, allEnums );

				if ( !codegen.generate( modules[ idx ] ) )
				{
					if ( !gDiag->hasErrors() )
						cerr << "Code generation failed for " << inputFile << endl;
					gDiag->finish();
					return 1;
				}

				if ( !codegen.verify() )
				{
					cerr << "internal compiler error: generated IR failed verification; please report this bug" << endl;
					if ( debugCompiler )
						cerr << codegen.getVerifyError() << endl;
					gDiag->finish();
					return -1;
				}

				// Layer 1 of -O (see combine path above): in-process IR passes,
				// opt-in, then re-verify. Empty/-O0 leaves the module unchanged.
				if ( !optLevel.empty() && optLevel != "0" )
				{
					if ( !codegen.optimize( optLevel ) )
					{
						cerr << "error: invalid optimization level '-O" << optLevel << "'" << endl;
						gDiag->finish();
						return -1;
					}
					if ( !codegen.verify() )
					{
						cerr << "internal compiler error: IR failed verification after optimization; please report this bug" << endl;
						if ( debugCompiler )
							cerr << codegen.getVerifyError() << endl;
						gDiag->finish();
						return -1;
					}
				}

				// Determine output file path for IR. When multiple input files are
				// given, -o only applies to the first; remaining files use derived names.
				std::string irFile;
				if ( !outputFile.empty() && idx == 0 )
				{
					irFile = outputFile;
				}
				else
				{
					// Derive .ll name from input file
					irFile = inputFile;
					size_t dot = irFile.rfind( '.' );
					if ( dot != std::string::npos )
						irFile = irFile.substr( 0, dot );
					irFile += ".ll";
				}

				// Print IR to stdout
				codegen.print( llvm::outs() );

				// Write IR to file
				std::error_code ec;
				llvm::raw_fd_ostream outFile( irFile, ec );
				if ( !ec )
				{
					codegen.print( outFile );
					PARSE_TRACE( "Wrote IR to " << irFile );
				}
				else
				{
					cerr << "Failed to write " << irFile << ": " << ec.message() << endl;
				}
			}
		}
	}
#else
	// Without LLVM, --parse-only is the only valid mode; ignore emit flags silently
	(void)emitIR;
	(void)emitObj;
	(void)parseOnly;
	(void)combineMode;
	(void)emitTestMain;
#endif

	// Single render point on the success path: flushes warnings buffered by
	// parse/sema (a warning-only compile still exits 0).
	gDiag->finish();
	return 0;
}

