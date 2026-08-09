#include <assert.h>
#include <climits>
#include <cstdlib>

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>

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

	// Build the global scope of compiler builtins. main() OWNS it via the
	// SmartPtr; gScope is a non-owning alias for the parser (see Frontend.h —
	// without an owner, the first file scope's parent release would free it).
	SmartPtr<Scope> globalScopeOwner = createGlobalScope();
	gScope = (Scope *)globalScopeOwner;

	// Parse each input file into its own Module. Each module gets its own
	// module-level scope parented to the shared global scope so that built-in
	// types are visible everywhere but top-level symbols remain per-file.
	// Cross-module symbol resolution (pub visibility enforcement) will be
	// layered on top once multi-module linking is implemented (Task 63).
	// Phase 1: Parse .bmod files first (they provide type info for .b files).
	// Build a map from module name to its parsed scope for import resolution.
	std::vector<SmartPtr<Module>> modules;
	std::map<std::string, Module*> bmodMap;

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
	if ( combineMode )
	{
		combineScope = new Scope( Scope::kScope_Module );
		combineScope->setParent( gScope );
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
			// A few stdlib modules define fundamental TYPES that programs use
			// unqualified (no `module.` prefix) after importing them, so they are
			// parsed into combineScope directly rather than a namespace scope:
			//   - buffer: the `Buffer` type.
			//   - collections: the `Map<K,V>` container (S2). It defines only the
			//     Map struct + its impl (no free functions), so promoting it to
			//     combineScope makes `Map<...>` resolve in a variable declaration
			//     (the seeded S2 bug: a generic type from a namespaced combined
			//     module was invisible unqualified) without polluting the global
			//     namespace with functions. bcc only combines collections.b when
			//     the program `import collections;`, so it is never present unless
			//     requested.
			bool isUserFile = ( fileIdx == inputFiles.size() - 1 );
			//   - cli (U5): parsed into combineScope (global, unqualified
			//     `has_flag(...)`) like collections. A namespaced module's
			//     internal string-returning calls (has_flag -> flag_name_of) hit a
			//     string-ARC double-free under the module-prefix codegen; global
			//     modules (collections' Map methods calling each other) are clean.
			bool isGlobalTypeLib = ( moduleName == "buffer" ||
				moduleName == "collections" || moduleName == "cli" );
			if ( isUserFile || isGlobalTypeLib )
			{
				fileScope = combineScope;
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
			fileScope = new Scope( Scope::kScope_Module );
			fileScope->setParent( gScope );
		}

		// For .b files: inject symbols from any .bmod modules that match imports.
		// Since we don't know imports yet (they're parsed inside Module::Parse),
		// we inject ALL bmod symbols into the global scope so they're available
		// during parsing. This implements the flat merge.
		if ( !isBmod && !bmodMap.empty() )
		{
			for ( auto &pair : bmodMap )
			{
				Module *bmod = pair.second;
				for ( const auto &sp : bmod->getFunctionList() )
				{
					FunctionDefinition *f = const_cast<FunctionDefinition*>( (const FunctionDefinition*)sp );
					if ( f->isPublic() )
					{
						// Non-generic: mark extern so codegen only declares (no
						// body) — the symbol links from the library archive.
						// GENERIC functions ship their bodies in the .bmod and
						// stay non-extern so the consumer monomorphizes them on
						// demand (instances are linkonce_odr, deduped with the
						// library's own instantiations at link time).
						if ( !f->isGeneric() )
							f->setFunctionExtern( true );
						gScope->addSymbol( f );
					}
				}
				for ( const auto &sp : bmod->getStructList() )
				{
					StructDefinition *s = const_cast<StructDefinition*>( (const StructDefinition*)sp );
					if ( s->isPublic() )
					{
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
			}
			bmodMap.clear(); // only inject once
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
		}

		modules.push_back( mod );
		PARSE_TRACE( "Completed parse" );
		// Semantic analysis (U3): runs in ALL build modes, immediately after a
		// non-extern module parses and before any code generation, resolving
		// member references and annotating expression types through the single
		// DiagnosticEngine. Extern .bmod modules provide types only and are not
		// analyzed (Sema::analyze skips them). On any sema error the compile
		// fails (non-zero exit) and codegen is not reached for this file.
		if ( !isBmod && !Sema::analyze( (Module *)mod, fileScope, *gDiag ) )
			hadError = true;
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
		QLang::BmodEmitter::emit( modPtrs, bmodOut );
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
		std::vector<SmartPtr<StructDefinition>> allStructs;
		std::vector<SmartPtr<EnumDefinition>> allEnums;
		for ( auto &mod : modules )
		{
			for ( auto &s : mod->getStructList() )
				allStructs.push_back( s );
			for ( auto &e : mod->getEnumList() )
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
					if ( moduleNamespaces.count( fname ) > 0 )
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

