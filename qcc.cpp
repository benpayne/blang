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
#include "ProjectCompile.h"
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
	// component (Resolver.h) — the same class lsp/Compile.cpp constructs. It OWNS
	// the global builtin scope and installs the `gScope` alias the parser/sema read.
	Resolver resolver;

	// Separate input files into .bmod and .b (the combined IR name below uses the
	// last source file). ProjectCompile::run applies the same .bmod-first reorder
	// to the plan's inputs.
	std::vector<std::string> bmodFiles, sourceFiles;
	for ( const auto &f : inputFiles )
	{
		if ( classifyInput( f ) == CompileInput::Kind::kInterface )
			bmodFiles.push_back( f );
		else
			sourceFiles.push_back( f );
	}

	// Quiet by default (FR-007): parser progress is a gated trace on STDERR
	// (Frontend.h), enabled by -v. stdout carries only machine output (the
	// --dump-locations node dump), so no cout redirection games are needed.
	setParseTraceEnabled( verbose && !dumpLocations );

	// modules-v2-lsp U1 (REQ-001): every compile rule — per-input scope routing,
	// prelude LOAD/PROMOTE, the .bmod pre-scan/format check/injection, origin and
	// digest stamping, Sema, and the import-diagnostics pass — runs through the ONE
	// shared entry point ProjectCompile::run, the same call lsp/Compile.cpp makes.
	// This driver only builds the plan from its arguments and runs its own output
	// stages on the returned context.
	CompilePlan plan;
	for ( const auto &f : inputFiles )
	{
		CompileInput in;
		in.path = f;
		in.kind = classifyInput( f );
		if ( in.kind == CompileInput::Kind::kInterface )
		{
			auto oit = bmodOrigins.find( f );
			if ( oit != bmodOrigins.end() )
				in.origin = oit->second;
		}
		plan.inputs.push_back( in );
	}
	plan.combine = combineMode;
	plan.moduleOrigin = moduleOrigin;
	plan.identity = IdentityPolicy::kCanonical;
	plan.lexerTrace = verbose && !dumpLocations;

	// KG-7: declared AFTER `resolver` so the context — every module scope and the
	// parser scopes/AST they own — is released before the global scope.
	std::unique_ptr<CompileContext> ctx = ProjectCompile::run( plan, resolver, diagnostics );

	// U1 (multi-error): stop before any output-producing stage if the compile had
	// errors — never codegen a rejected program (Constitution III). finish()
	// renders all buffered diagnostics once (human text or --json array). On the
	// success path finish() is DEFERRED to the exit points below so the codegen
	// phase can buffer its own located errors through the same engine and
	// everything still renders exactly once.
	if ( ctx->hadError() || gDiag->hasErrors() )
	{
		gDiag->finish();
		return 1;
	}

	// The RECORDED modules (ProjectCompile's recorded-only view, in processing
	// order) that every output stage below reads, plus each one's record index.
	std::vector<SmartPtr<Module>> modules;
	std::vector<std::size_t> moduleRecords;
	for ( std::size_t ri : ctx->moduleIndices() )
	{
		modules.push_back( ctx->record( ri ).module );
		moduleRecords.push_back( ri );
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
		QLang::BmodEmitter::emit( modPtrs, bmodOut, ctx->bmodEmitScope() );
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
		// modules-v2-graph U3 (D13, §5b): ProjectCompile computed which prelude type
		// names a USER module shadows (suppressedPrelude()); the prelude provider's
		// struct of that name is filtered out of allStructs below — making CodeGen's
		// flat mStructDefMap agree with the scope registries (the user's definition
		// wins in all three).
		// modules-v2-lsp U4: the collection is CompileContext::useCapabilityTypes
		// (lifted verbatim, shared with blangd's use-capability navigation).
		std::vector<SmartPtr<StructDefinition>> allStructs;
		std::vector<SmartPtr<EnumDefinition>> allEnums;
		ctx->useCapabilityTypes( allStructs, allEnums );

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
				// Prelude providers (buffer/collections) are namespaced for
				// resolution but compile PREFIX-FREE (modules-v2-graph U3, Option B);
				// the rule lives in CompileContext::codegenPrefix.
				std::string modPrefix = ctx->codegenPrefix( moduleRecords[idx] );
				codegen.setModulePrefix( modPrefix );

				if ( !codegen.generate( modules[idx] ) )
				{
					// The located diagnostics say what failed; add the generic
					// line only if codegen somehow failed without buffering one.
					if ( !gDiag->hasErrors() )
						cerr << "Code generation failed for " << ctx->record( moduleRecords[idx] ).path << endl;
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

				const std::string &inputFile = ctx->record( moduleRecords[ idx ] ).path;
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

