// bcc - BLang Compiler Driver
//
// User-facing CLI that orchestrates the full compilation pipeline:
//   1. Parse + generate LLVM IR  (via qcc)
//   2. Compile IR to object file (via llc)
//   3. Link to native binary     (via cc)
//
// Usage:
//   bcc source.b                  # compile and link -> a.out
//   bcc source.b -o myprogram     # compile and link -> myprogram
//   bcc -S source.b               # emit LLVM IR only -> source.ll
//   bcc -c source.b               # compile to object only -> source.o
//   bcc -v source.b               # verbose, show each pipeline step
//   bcc test                      # discover and run test files

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "ProjectConfig.h"
#include "BuildCache.h"
#include "Type.h"
#include "Expression.h"
#include "SchemaMigration.h"
#include "runtime/blang_db.h"

using namespace std;

struct Options
{
	string inputFile;
	string outputFile;
	bool emitIROnly = false;     // -S
	bool compileOnly = false;    // -c
	bool verbose = false;        // -v
	bool jsonDiagnostics = false;// --json  (forwarded to qcc)
	bool werror = false;         // -Werror (forwarded to qcc)
	string optLevel;             // -O<n>: "" none, else 0..3/s/z (U2)
	bool release = false;        // --release: implies -O2 (U2)
	string targetTriple;         // --target <triple>: cross-compile (U2)
	bool debugInfo = false;      // -g: emit DWARF debug info; forces -O0 (U3)
	vector<string> linkerFlags;  // -l, -L, etc.
};

static void printUsage( const char *progName )
{
	cerr << "Usage: " << progName << " [options] <source.b>" << endl;
	cerr << "       " << progName << " test [--verbose]" << endl;
	cerr << endl;
	cerr << "Subcommands:" << endl;
	cerr << "  build        Build project from blang.toml" << endl;
	cerr << "  clean        Remove build cache (~/.cache/blang/)" << endl;
	cerr << "  test         Discover and run BLang test files" << endl;
	cerr << "  migrate      Schema migration (--preview, --apply, --generate)" << endl;
	cerr << endl;
	cerr << "Options:" << endl;
	cerr << "  -o <file>    Output file name" << endl;
	cerr << "  -S           Emit LLVM IR only (.ll)" << endl;
	cerr << "  -c           Compile to object file only (.o)" << endl;
	cerr << "  -v           Verbose output" << endl;
	cerr << "  --json       Emit compiler diagnostics as JSON" << endl;
	cerr << "  -Werror      Treat warnings as errors" << endl;
	cerr << "  -O<n>        Optimize (0..3, s, z); bare -O = -O2" << endl;
	cerr << "  --release    Optimized build (implies -O2)" << endl;
	cerr << "  --target <t> Cross-compile to target triple (object emission)" << endl;
	cerr << "  -g           Emit DWARF debug info (forces -O0)" << endl;
	cerr << "  -l<lib>      Link with library" << endl;
	cerr << "  -L<dir>      Add library search path" << endl;
	cerr << "  -h, --help   Show this help" << endl;
}

static bool parseArgs( int argc, char *argv[], Options &opts )
{
	for ( int i = 1; i < argc; i++ )
	{
		string arg = argv[i];

		if ( arg == "-h" || arg == "--help" )
		{
			printUsage( argv[0] );
			exit( 0 );
		}
		else if ( arg == "-o" )
		{
			if ( i + 1 >= argc )
			{
				cerr << "error: -o requires an argument" << endl;
				return false;
			}
			opts.outputFile = argv[++i];
		}
		else if ( arg == "-S" )
		{
			opts.emitIROnly = true;
		}
		else if ( arg == "-c" )
		{
			opts.compileOnly = true;
		}
		else if ( arg == "-v" )
		{
			opts.verbose = true;
		}
		else if ( arg == "--json" )
		{
			opts.jsonDiagnostics = true;
		}
		else if ( arg == "-Werror" )
		{
			opts.werror = true;
		}
		else if ( arg == "--release" )
		{
			opts.release = true;
		}
		else if ( arg == "--target" )
		{
			if ( i + 1 >= argc )
			{
				cerr << "error: --target requires a triple argument" << endl;
				return false;
			}
			opts.targetTriple = argv[++i];
		}
		else if ( arg == "-O" )
		{
			opts.optLevel = "2";               // bare -O means -O2
		}
		else if ( arg.size() > 2 && arg.substr( 0, 2 ) == "-O" )
		{
			opts.optLevel = arg.substr( 2 );   // -O0/1/2/3/s/z
		}
		else if ( arg == "-g" )
		{
			opts.debugInfo = true;             // DWARF debug info (U3)
		}
		else if ( arg.substr( 0, 2 ) == "-l" || arg.substr( 0, 2 ) == "-L" )
		{
			opts.linkerFlags.push_back( arg );
		}
		else if ( arg[0] == '-' )
		{
			cerr << "error: unknown option '" << arg << "'" << endl;
			return false;
		}
		else
		{
			if ( !opts.inputFile.empty() )
			{
				cerr << "error: multiple input files not supported" << endl;
				return false;
			}
			opts.inputFile = arg;
		}
	}

	if ( opts.inputFile.empty() )
	{
		cerr << "error: no input file" << endl;
		return false;
	}

	return true;
}

// Get the base name without extension
static string getBaseName( const string &path )
{
	// Strip directory
	size_t slash = path.rfind( '/' );
	string name = ( slash != string::npos ) ? path.substr( slash + 1 ) : path;

	// Strip extension
	size_t dot = name.rfind( '.' );
	if ( dot != string::npos )
		name = name.substr( 0, dot );

	return name;
}

// Get directory of file
static string getDirName( const string &path )
{
	size_t slash = path.rfind( '/' );
	if ( slash != string::npos )
		return path.substr( 0, slash );
	return ".";
}

// Get directory of the bcc executable itself
static string getExeDir( const char *argv0 )
{
	// Try /proc/self/exe first (Linux)
	char buf[4096];
	ssize_t len = readlink( "/proc/self/exe", buf, sizeof( buf ) - 1 );
	if ( len > 0 )
	{
		buf[len] = '\0';
		string exePath = buf;
		size_t slash = exePath.rfind( '/' );
		if ( slash != string::npos )
			return exePath.substr( 0, slash );
	}

	// Fallback: assume qcc is in the same directory as bcc
	string arg0 = argv0;
	size_t slash = arg0.rfind( '/' );
	if ( slash != string::npos )
		return arg0.substr( 0, slash );

	return ".";
}

// Run a command and return its exit code
static int runCommand( const vector<string> &args, bool verbose, bool suppressOutput = false )
{
	if ( verbose )
	{
		for ( size_t i = 0; i < args.size(); i++ )
		{
			if ( i > 0 ) cerr << " ";
			cerr << args[i];
		}
		cerr << endl;
	}

	// Build command string for system()
	string cmd;
	for ( size_t i = 0; i < args.size(); i++ )
	{
		if ( i > 0 ) cmd += " ";
		// Quote arguments that might contain spaces
		cmd += "\"" + args[i] + "\"";
	}

	// Suppress stdout/stderr in non-verbose mode when requested
	// Stderr is captured to a temp file so errors can be shown on failure
	if ( suppressOutput && !verbose )
		cmd += " >/dev/null 2>/tmp/bcc_stderr.txt";

	int ret = system( cmd.c_str() );
	if ( WIFEXITED( ret ) )
		return WEXITSTATUS( ret );
	return -1;
}

// Find a tool, checking multiple possible names
static string findTool( const string &name, const vector<string> &alternatives )
{
	// Check each candidate with 'which'
	vector<string> candidates;
	candidates.push_back( name );
	for ( const auto &alt : alternatives )
		candidates.push_back( alt );

	for ( const auto &candidate : candidates )
	{
		string cmd = "which " + candidate + " >/dev/null 2>&1";
		if ( system( cmd.c_str() ) == 0 )
			return candidate;
	}

	return "";
}

// ---------------------------------------------------------------------------
// Pipeline foundation (epic 001-toolchain-and-stdlib, U0)
//
// The bcc→qcc→llc→cc pipeline used to duplicate the llc object-emission block
// 4x and the runtime-link library list 3x. These helpers are the single site
// for each. Later units extend the pipeline HERE, in one place:
//   - U2 (-O / --target): emitObject() owns the llc flags + the host triple.
//   - U3 (-g): emitObject() is the llc-side hook (the qcc-emit side is the qcc
//     arg loop); debug metadata must survive the text-.ll boundary through here.
//   - U4/U5 (new stdlib .a): appendRuntimeLibs() is the one place a lib is added.
// ---------------------------------------------------------------------------

// Resolve the llc tool once (build-time baked path, then PATH). Empty on failure.
static string resolveLlc()
{
	string llc;
#ifdef BCC_LLC_PATH
	if ( access( BCC_LLC_PATH, X_OK ) == 0 )
		llc = BCC_LLC_PATH;
#endif
	if ( llc.empty() )
		llc = findTool( "llc-18", { "llc" } );
	return llc;
}

// Emit a native object from a textual .ll via llc. Owns the -filetype=obj flag
// vector, the backend optimization level (U2 layer 2), and the target triple.
// Returns llc's exit code (0 == ok). Per-path error messages and IR-file cleanup
// stay at the call sites.
//   optLevel     — "" for none, else "0".."3"/"s"/"z" → llc -O<n>.
//   targetTriple — "" for the host-baked triple (byte-identical to pre-U2), else
//                  the given triple (cross-compile, U2 --target).
//   debugInfo    — currently informational only: DWARF debug metadata carried in
//                  the textual .ll is emitted into the object by llc automatically
//                  (llc has no -g flag). qcc already forced -O0 when -g (S-A
//                  stance). Kept as a parameter so the single llc site owns the
//                  knob if a future backend needs an explicit flag.
static int emitObject( const string &llc, const string &llFile,
                       const string &objFile, bool verbose,
                       const string &optLevel = "",
                       const string &targetTriple = "",
                       bool debugInfo = false )
{
	(void)debugInfo;
	vector<string> cmd = { llc, "-filetype=obj", "--relocation-model=pic" };
	if ( !optLevel.empty() )
	{
		// llc's -O accepts only numeric 0..3. The size levels (-Os/-Oz) are
		// applied as IR passes in qcc (layer 1); map them to backend -O2 here so
		// llc gets a valid level.
		string llcOpt = ( optLevel == "s" || optLevel == "z" ) ? "2" : optLevel;
		cmd.push_back( string( "-O" ) + llcOpt );
	}
	if ( !targetTriple.empty() )
	{
		cmd.push_back( string( "-mtriple=" ) + targetTriple );
	}
	else
	{
#if defined(BCC_HOST_ARCH)
#if defined(PLATFORM_DARWIN)
		cmd.push_back( string( "-mtriple=" ) + BCC_HOST_ARCH + "-apple-darwin" );
#elif defined(PLATFORM_LINUX)
		cmd.push_back( string( "-mtriple=" ) + BCC_HOST_ARCH + "-unknown-linux-gnu" );
#endif
#endif
	}
	cmd.push_back( llFile );
	cmd.push_back( "-o" );
	cmd.push_back( objFile );
	return runCommand( cmd, verbose );
}

// Which runtime libs a link line needs. The `bcc test` path links the test
// driver and omits the db lib; program builds (single-file + combined) link the
// db lib and omit the test driver.
struct RuntimeLinkProfile
{
	bool withTestRunner;
	bool withDb;
};

// Append the ordered libblang_*.a list to a link command (dependents before
// dependencies). THIS IS THE ONE PLACE a new stdlib .a is added (U4/U5). Only
// the libblang_*.a list is appended here; each path's tail (BCC_DB_LINKFLAGS,
// -lpthread, -o, user linker flags, -luv) stays at the call site so argument
// order is preserved byte-for-byte.
static void appendRuntimeLibs( vector<string> &cmd, const string &exeDir,
                               const RuntimeLinkProfile &profile )
{
	auto findLib = [&]( const char *baked, const char *name ) -> string {
		string lib;
		if ( baked != nullptr )
			lib = baked;
		if ( lib.empty() || access( lib.c_str(), F_OK ) != 0 )
		{
			string fallback = exeDir + "/lib" + name + ".a";
			if ( access( fallback.c_str(), F_OK ) == 0 )
				lib = fallback;
			else
				lib.clear();
		}
		return lib;
	};

	const char *bakedTestRunner = nullptr, *bakedRuntime = nullptr, *bakedString = nullptr;
	const char *bakedArray = nullptr, *bakedBuffer = nullptr, *bakedJson = nullptr;
	const char *bakedNet = nullptr, *bakedFs = nullptr, *bakedSys = nullptr, *bakedDb = nullptr;
	// Native stdlib modules (U4). Their .a's are offered to the linker
	// unconditionally (like sys/fs/net) and dropped when unreferenced — import
	// gating happens at the .b combine layer (kKnownOrder), not here.
	const char *bakedMath = nullptr, *bakedTime = nullptr, *bakedRandom = nullptr, *bakedEnv = nullptr;
	const char *bakedHash = nullptr;   // U5: FNV-1a for hashed collections
#ifdef BCC_TESTRUNNER_LIB
	bakedTestRunner = BCC_TESTRUNNER_LIB;
#endif
#ifdef BCC_RUNTIME_LIB
	bakedRuntime = BCC_RUNTIME_LIB;
#endif
#ifdef BCC_STRING_LIB
	bakedString = BCC_STRING_LIB;
#endif
#ifdef BCC_ARRAY_LIB
	bakedArray = BCC_ARRAY_LIB;
#endif
#ifdef BCC_BUFFER_LIB
	bakedBuffer = BCC_BUFFER_LIB;
#endif
#ifdef BCC_JSON_LIB
	bakedJson = BCC_JSON_LIB;
#endif
#ifdef BCC_NET_LIB
	bakedNet = BCC_NET_LIB;
#endif
#ifdef BCC_FS_LIB
	bakedFs = BCC_FS_LIB;
#endif
#ifdef BCC_SYS_LIB
	bakedSys = BCC_SYS_LIB;
#endif
#ifdef BCC_DB_LIB
	bakedDb = BCC_DB_LIB;
#endif
#ifdef BCC_MATH_LIB
	bakedMath = BCC_MATH_LIB;
#endif
#ifdef BCC_TIME_LIB
	bakedTime = BCC_TIME_LIB;
#endif
#ifdef BCC_RANDOM_LIB
	bakedRandom = BCC_RANDOM_LIB;
#endif
#ifdef BCC_ENV_LIB
	bakedEnv = BCC_ENV_LIB;
#endif
#ifdef BCC_HASH_LIB
	bakedHash = BCC_HASH_LIB;
#endif

	// Leading lib (test driver or db), then the shared dependents->deps chain.
	vector<string> libs;
	if ( profile.withTestRunner )
		libs.push_back( findLib( bakedTestRunner, "blang_testrunner" ) );
	if ( profile.withDb )
		libs.push_back( findLib( bakedDb, "blang_db" ) );
	libs.push_back( findLib( bakedSys, "blang_sys" ) );
	// Native stdlib modules (U4) — placed before their deps (string/array) so
	// GNU ld resolves them; math's libm dep is added as a trailing -lm below.
	libs.push_back( findLib( bakedMath, "blang_math" ) );
	libs.push_back( findLib( bakedTime, "blang_time" ) );
	libs.push_back( findLib( bakedRandom, "blang_random" ) );
	libs.push_back( findLib( bakedEnv, "blang_env" ) );
	// blang_hash (U5) is a leaf dep of the collections .b combine layer (only
	// user code references it), so its position is order-tolerant; placed before
	// its blang_string dep for GNU ld.
	libs.push_back( findLib( bakedHash, "blang_hash" ) );
	libs.push_back( findLib( bakedFs, "blang_fs" ) );
	libs.push_back( findLib( bakedNet, "blang_net" ) );
	libs.push_back( findLib( bakedJson, "blang_json" ) );
	libs.push_back( findLib( bakedBuffer, "blang_buffer" ) );
	libs.push_back( findLib( bakedArray, "blang_array" ) );
	libs.push_back( findLib( bakedString, "blang_string" ) );
	libs.push_back( findLib( bakedRuntime, "blang_runtime" ) );

	for ( const auto &lib : libs )
	{
		if ( !lib.empty() )
			cmd.push_back( lib );
	}

	// Math (U4) needs libm. Appended as a trailing system-linker token (not a
	// findLib path) AFTER blang_math.a so GNU ld resolves math's sqrt/pow/etc.
	// Harmless when math is unused (no libm symbol is referenced, so nothing is
	// pulled in). System libm is always present.
	cmd.push_back( "-lm" );
}

// Check whether a path is an existing directory
static bool isDirectory( const string &path )
{
	struct stat st;
	if ( stat( path.c_str(), &st ) != 0 )
		return false;
	return S_ISDIR( st.st_mode );
}

// Collect .b files by running find via popen
static vector<string> collectTestFiles( const string &searchRoot )
{
	vector<string> files;
	string cmd = "find \"" + searchRoot + "\" -name \"*.b\" 2>/dev/null";
	FILE *fp = popen( cmd.c_str(), "r" );
	if ( !fp )
		return files;

	char buf[4096];
	while ( fgets( buf, sizeof( buf ), fp ) )
	{
		string line = buf;
		// Strip trailing newline
		while ( !line.empty() && ( line.back() == '\n' || line.back() == '\r' ) )
			line.pop_back();
		if ( !line.empty() )
			files.push_back( line );
	}
	pclose( fp );
	return files;
}

// Compile a single .b test file with the test-runner entry point and run it.
//
// Mirrors the normal bcc pipeline (qcc --combine <stdlib> <file>
// --emit-test-main -> llc -> cc) but links the fork-isolated test driver
// (libblang_testrunner.a). The produced binary registers each test{} block and
// dispatches to __blang_test_main, forwarding `--filter <name>` when set.
// Returns the test binary's exit code (non-zero iff a test failed), or a
// non-zero sentinel on a compile/link failure.
static set<string> parseImports( const string &path );
static vector<string> resolveStdlibFiles( const string &exeDir,
	const set<string> &imports );

static int compileAndRunTestFile( const string &exeDir, const string &qcc,
	const string &file, const string &filter, bool verbose )
{
	string baseName = getBaseName( file );
	string srcDir = getDirName( file );

	// Collect stdlib .b files for --combine — the SAME resolution as the normal
	// compile path (base modules always; env/cli/math/... gated on the file's
	// imports), so a test file can exercise exactly what its program uses. The
	// previous hand-rolled subset here (base + collections/timer only) made
	// `bcc test` fail on files importing env/cli/etc. that `bcc build` accepts.
	vector<string> stdlibFiles =
		resolveStdlibFiles( exeDir, parseImports( file ) );

	// Step 1: qcc --combine <stdlib...> <file> --emit-test-main
	{
		vector<string> cmd = { qcc, "--combine" };
		for ( const auto &sf : stdlibFiles )
			cmd.push_back( sf );
		cmd.push_back( file );
		cmd.push_back( "--emit-test-main" );
		int ret = runCommand( cmd, verbose, !verbose );
		if ( ret != 0 )
		{
			cerr << "bcc test: compilation failed for " << file << endl;
			return 2;
		}
	}

	// qcc (combine mode) writes IR derived from the last source file (the user
	// file) as <file-with-.ll>.
	string irFile = srcDir + "/" + baseName + ".ll";
	if ( access( irFile.c_str(), F_OK ) != 0 )
	{
		cerr << "bcc test: no .ll generated for " << file
		     << " (is qcc built with LLVM?)" << endl;
		return 2;
	}

	// Step 2: llc -> object
	string llc = resolveLlc();
	if ( llc.empty() )
	{
		cerr << "bcc test: llc not found" << endl;
		remove( irFile.c_str() );
		return 2;
	}

	string objFile = "/tmp/" + baseName + "_bcctest.o";
	{
		int ret = emitObject( llc, irFile, objFile, verbose );
		remove( irFile.c_str() );
		if ( ret != 0 )
		{
			cerr << "bcc test: IR compilation failed for " << file << endl;
			return 2;
		}
	}

	// Step 3: link with the test driver + BLang runtime libs
	string binFile = "/tmp/" + baseName + "_bcctest_bin";
	{
		string cc = "cc";
#ifdef BCC_CC_PATH
		cc = BCC_CC_PATH;
#endif
		vector<string> cmd = { cc };
#if defined(BCC_HOST_ARCH) && defined(PLATFORM_DARWIN)
		cmd.push_back( "-arch" );
		cmd.push_back( BCC_HOST_ARCH );
#endif
		cmd.push_back( objFile );

		// Test driver first (referenced by the emitted main), then the runtime
		// libs; the test path links no db lib.
		appendRuntimeLibs( cmd, exeDir,
			RuntimeLinkProfile{ /*withTestRunner=*/true, /*withDb=*/false } );

		cmd.push_back( "-lpthread" );
		cmd.push_back( "-o" );
		cmd.push_back( binFile );
#ifdef BCC_HAS_LIBUV
		cmd.push_back( "-luv" );
#endif
		int ret = runCommand( cmd, verbose );
		remove( objFile.c_str() );
		if ( ret != 0 )
		{
			cerr << "bcc test: linking failed for " << file << endl;
			return 2;
		}
	}

	// Step 4: run the test binary, forwarding --filter. Its stdout/stderr are
	// inherited so per-test PASS/FAIL, located failures, and the summary appear.
	int exitCode;
	{
		string runCmd = "\"" + binFile + "\"";
		if ( !filter.empty() )
			runCmd += " --filter \"" + filter + "\"";
		int ret = system( runCmd.c_str() );
		if ( WIFEXITED( ret ) )
			exitCode = WEXITSTATUS( ret );
		else
			exitCode = 3; // crashed / signaled
	}
	remove( binFile.c_str() );
	return exitCode;
}

// bcc test subcommand
//
//   bcc test [--filter <name>] <file.b> [<file2.b> ...]
//     Compile each file with the test-runner entry point and run its test{}
//     blocks, reporting per-test PASS/FAIL with file:line on failure. Exit code
//     is non-zero iff any test fails.
//
//   bcc test [--filter <name>]        (no file given)
//     Legacy discovery: search tests/ (or the current directory) and run each
//     discovered .b file the same way.
static int runTests( int argc, char *argv[], const string &exeDir )
{
	bool verbose = false;
	string filter;
	vector<string> fileArgs;

	for ( int i = 2; i < argc; i++ )
	{
		string arg = argv[i];
		if ( arg == "--verbose" || arg == "-v" )
			verbose = true;
		else if ( arg == "--filter" && i + 1 < argc )
			filter = argv[++i];
		else if ( arg.rfind( "--filter=", 0 ) == 0 )
			filter = arg.substr( 9 );
		else if ( !arg.empty() && arg[0] != '-' )
			fileArgs.push_back( arg );
	}

	string qcc = exeDir + "/qcc";

	// Determine the set of files to run: explicit args, else discovery.
	vector<string> files = fileArgs;
	if ( files.empty() )
	{
		string searchRoot;
		if ( isDirectory( "tests" ) )
		{
			searchRoot = "tests";
			cerr << "bcc test: searching tests/ directory" << endl;
		}
		else
		{
			searchRoot = ".";
			cerr << "bcc test: no tests/ directory found, searching current directory for *.b files" << endl;
		}
		files = collectTestFiles( searchRoot );
	}

	if ( files.empty() )
	{
		cerr << "bcc test: no .b files found" << endl;
		return 0;
	}

	int worstExit = 0;
	for ( const auto &file : files )
	{
		if ( files.size() > 1 )
			cout << "=== " << file << " ===" << endl;
		int rc = compileAndRunTestFile( exeDir, qcc, file, filter, verbose );
		if ( rc != 0 )
			worstExit = rc;
	}

	return worstExit;
}

// Forward declarations (defined later) so migrate can resolve stdlib imports
// the same way `bcc build` does.
static set<string> parseImports( const string &path );
static vector<string> resolveStdlibFiles( const string &exeDir,
	const set<string> &imports );

// bcc migrate subcommand
//
// Compares current table struct definitions against stored schema snapshot
// and generates migration SQL.
//
// Usage:
//   bcc migrate --preview     Show what would change
//   bcc migrate --apply       Apply changes to the database
//   bcc migrate --generate    Generate migration SQL to stdout
static int runMigrate( int argc, char *argv[] )
{
	string mode = "--preview"; // default
	bool allowDestructive = false;
	vector<string> sourceFiles;

	for ( int i = 2; i < argc; i++ )
	{
		string arg = argv[i];
		if ( arg == "--preview" || arg == "--apply" || arg == "--generate" )
			mode = arg;
		else if ( arg == "--allow-destructive" )
			allowDestructive = true;
		else if ( arg[0] != '-' )
			sourceFiles.push_back( arg );
	}

	if ( sourceFiles.empty() )
	{
		// Try to find .b files in the current directory
		vector<string> found = collectTestFiles( "." );
		for ( const auto &f : found )
			sourceFiles.push_back( f );
	}

	if ( sourceFiles.empty() )
	{
		cerr << "bcc migrate: no source files found" << endl;
		return 1;
	}

	// 1. Obtain the current schema by parsing the sources with qcc and having
	//    it emit the table-struct schema as JSON (qcc owns the parser).
	string exeDir = getExeDir( argv[0] );
	string qcc = exeDir + "/qcc";

	mkdir( ".blang", 0755 );
	string storedSchemaPath = ".blang/schema.json";
	string currentSchemaPath = ".blang/schema.current.json";

	// Resolve stdlib imports so projects that `import net;` (etc.) parse — the
	// table struct lives in a source file that references stdlib symbols, so the
	// stdlib modules must be combined into the same parse, exactly as bcc build
	// does. User sources go last (combine treats the last file as user scope).
	set<string> imports;
	for ( const auto &f : sourceFiles )
		for ( const auto &imp : parseImports( f ) )
			imports.insert( imp );
	vector<string> stdlibFiles = resolveStdlibFiles( exeDir, imports );

	vector<string> qccCmd = { qcc };
	if ( !stdlibFiles.empty() )
		qccCmd.push_back( "--combine" );
	qccCmd.push_back( "--emit-schema" );
	qccCmd.push_back( currentSchemaPath );
	for ( const auto &sf : stdlibFiles )
		qccCmd.push_back( sf );
	for ( const auto &f : sourceFiles )
		qccCmd.push_back( f );
	if ( runCommand( qccCmd, false, true ) != 0 )
	{
		cerr << "bcc migrate: failed to extract schema from sources" << endl;
		return 1;
	}

	// 2. Diff the stored snapshot against the current schema.
	QLang::SchemaMigration mig;
	mig.loadSchema( storedSchemaPath );          // stored (empty on first run)
	mig.loadCurrentSchema( currentSchemaPath );  // current

	vector<QLang::MigrationStep> steps = mig.computeDiff();

	if ( mode == "--preview" )
	{
		cout << mig.preview();
		remove( currentSchemaPath.c_str() );
		return 0;
	}

	if ( mode == "--generate" )
	{
		cout << mig.generateSQL();
		remove( currentSchemaPath.c_str() );
		return 0;
	}

	// mode == "--apply"
	if ( steps.empty() )
	{
		cout << "No schema changes to apply." << endl;
		remove( currentSchemaPath.c_str() );
		return 0;
	}

	// Destructive changes (DROP TABLE / DROP COLUMN) require explicit consent.
	// The @drop annotation marks an intentional removal in source; since a
	// removed entity no longer exists in source, apply additionally gates on
	// the --allow-destructive flag as the CLI confirmation path.
	if ( mig.hasDestructiveChanges() && !allowDestructive )
	{
		cerr << "bcc migrate: refusing to apply destructive changes without "
			 << "--allow-destructive:" << endl;
		for ( const auto &step : steps )
			if ( step.isDestructive )
				cerr << "  [DESTRUCTIVE] " << step.description << endl;
		remove( currentSchemaPath.c_str() );
		return 1;
	}

	// Resolve the database connection from blang.toml [database] or the
	// BLANG_DATABASE_URL environment variable.
	string driver = "sqlite";
	string url;
	ProjectConfig *cfg = ProjectConfig::loadFromDirectory( "." );
	if ( cfg != nullptr )
	{
		if ( !cfg->getDbDriver().empty() ) driver = cfg->getDbDriver();
		url = cfg->getDbUrl();
		delete cfg;
	}
	if ( url.empty() )
	{
		const char *envUrl = getenv( "BLANG_DATABASE_URL" );
		if ( envUrl != nullptr )
			url = envUrl;
	}
	if ( url.empty() )
	{
		cerr << "bcc migrate: no database url configured (set [database].url in "
			 << "blang.toml or BLANG_DATABASE_URL)" << endl;
		remove( currentSchemaPath.c_str() );
		return 1;
	}

	const char *errMsg = nullptr;
	BlangDBConn *conn = __blang_db_open(
		__blang_db_driver_from_name( driver.c_str() ), url.c_str(), &errMsg );
	if ( conn == nullptr )
	{
		cerr << "bcc migrate: cannot open database: "
			 << ( errMsg ? errMsg : "unknown error" ) << endl;
		remove( currentSchemaPath.c_str() );
		return 1;
	}

	int applied = 0;
	for ( const auto &step : steps )
	{
		const char *stepErr = nullptr;
		if ( __blang_db_exec_raw( conn, step.sql.c_str(), &stepErr ) != 0 )
		{
			cerr << "bcc migrate: failed: " << step.description << endl;
			cerr << "  SQL: " << step.sql << endl;
			cerr << "  error: " << ( stepErr ? stepErr : "unknown" ) << endl;
			__blang_db_close( conn );
			remove( currentSchemaPath.c_str() );
			return 1;
		}
		cout << "applied: " << step.description << endl;
		applied++;
	}

	__blang_db_close( conn );

	// Snapshot the now-current schema as the new stored baseline.
	mig.saveSchema( storedSchemaPath );
	remove( currentSchemaPath.c_str() );

	cout << "Migration complete: " << applied << " step(s) applied." << endl;
	return 0;
}

// Discover all .b files in a directory (non-recursive, project root only)
static vector<string> discoverSourceFiles( const string &dir )
{
	vector<string> files;
	DIR *d = opendir( dir.c_str() );
	if ( !d )
		return files;

	struct dirent *entry;
	while ( ( entry = readdir( d ) ) != nullptr )
	{
		string name = entry->d_name;
		if ( name.size() > 2 && name.substr( name.size() - 2 ) == ".b" )
		{
			string path = dir;
			if ( !path.empty() && path.back() != '/' )
				path += '/';
			path += name;
			files.push_back( path );
		}
	}
	closedir( d );
	sort( files.begin(), files.end() );
	return files;
}

// Read file content as string for cache key computation
static string readFileToString( const string &path )
{
	ifstream f( path, ios::binary );
	if ( !f.is_open() )
		return "";
	ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// Extract the set of imported module names from a BLang source file.
// Recognizes top-level `import name;` and `import name.sub;` statements,
// returning just the leading identifier ("name"). Comments and other tokens
// are ignored well enough for stdlib resolution.
static set<string> parseImports( const string &path )
{
	set<string> imports;
	string src = readFileToString( path );
	istringstream in( src );
	string line;
	while ( getline( in, line ) )
	{
		// Strip a trailing line comment.
		size_t cpos = line.find( "//" );
		if ( cpos != string::npos )
			line = line.substr( 0, cpos );

		// Find the first non-space character.
		size_t i = 0;
		while ( i < line.size() && isspace( (unsigned char)line[i] ) )
			i++;
		if ( line.compare( i, 7, "import " ) != 0 )
			continue;
		i += 7;
		while ( i < line.size() && isspace( (unsigned char)line[i] ) )
			i++;

		// Read the leading identifier of the module path.
		size_t start = i;
		while ( i < line.size() &&
			( isalnum( (unsigned char)line[i] ) || line[i] == '_' ) )
			i++;
		if ( i > start )
			imports.insert( line.substr( start, i - start ) );
	}
	return imports;
}

// Given the user's imports, return the stdlib `.b` files to combine, in a
// dependency-safe order. Only modules the program actually imports are pulled
// in, so an unused module never pollutes the namespace (e.g. collections' Map).
static vector<string> resolveStdlibFiles( const string &exeDir,
	const set<string> &imports )
{
	vector<string> files;
	set<string> handled;
	auto addIfPresent = [&]( const string &name ) {
		string candidate = exeDir + "/stdlib/" + name + ".b";
		if ( access( candidate.c_str(), F_OK ) == 0 )
			files.push_back( candidate );
		handled.insert( name );
	};

	// Local's base modules are always combined, in dependency-safe order:
	// buffer.b must precede fs.b and net.b (they use Buffer). These ship with
	// the compiler and many programs use them (File, Socket, Buffer) without an
	// explicit import, so inclusion is unconditional (preserves local behavior).
	for ( const char *name : { "sys", "buffer", "fs", "net" } )
		addIfPresent( name );

	// Origin's import-gated extras: only pulled in when the program imports
	// them, so an unused module never pollutes the namespace (e.g. collections'
	// Map). Ordered so base modules resolve first under --combine.
	static const char *kKnownOrder[] = { "collections", "timer",
		"math", "time", "random", "env", "cli" };
	for ( const char *name : kKnownOrder )
	{
		if ( imports.count( name ) && !handled.count( name ) )
			addIfPresent( name );
	}
	// Any other imported name that happens to ship a stdlib file.
	for ( const string &name : imports )
	{
		if ( !handled.count( name ) )
			addIfPresent( name );
	}
	return files;
}

// Resolve absolute path from a possibly relative path
static string resolvePath( const string &basePath, const string &relPath )
{
	if ( !relPath.empty() && relPath[0] == '/' )
		return relPath;
	// basePath is the directory containing blang.toml
	string base = basePath;
	if ( !base.empty() && base.back() != '/' )
		base += '/';
	// Resolve with realpath
	string combined = base + relPath;
	char resolved[PATH_MAX];
	if ( realpath( combined.c_str(), resolved ) )
		return string( resolved );
	return combined;
}

// Build a single project directory. Returns 0 on success.
// Outputs: fills aFile and bmodFile if type=lib.
// depBmodFiles/depAFiles receive dependency artifacts to pass downstream.
static int buildProject( const string &projectDir, const string &exeDir,
	bool verbose, set<string> &building,
	string &outAFile, string &outBmodFile, string &cacheHash )
{
	// Circular dependency detection
	char resolvedDir[PATH_MAX];
	string canonDir = projectDir;
	if ( realpath( projectDir.c_str(), resolvedDir ) )
		canonDir = resolvedDir;

	if ( building.count( canonDir ) )
	{
		cerr << "error: circular dependency detected: " << canonDir << endl;
		return 1;
	}
	building.insert( canonDir );

	ProjectConfig *config = ProjectConfig::loadFromDirectory( projectDir );
	if ( !config )
	{
		cerr << "error: no blang.toml found in " << projectDir << endl;
		return 1;
	}

	if ( verbose )
		cerr << "--- Building " << config->getName() << " (" << projectDir << ") ---" << endl;

	// Recursively build dependencies first
	vector<string> depBmodFiles;
	vector<string> depAFiles;
	vector<string> depHashes;

	for ( const auto &dep : config->getDependencies() )
	{
		if ( dep.path.empty() )
		{
			cerr << "error: dependency '" << dep.name << "' has no path (git deps not yet supported)" << endl;
			delete config;
			return 1;
		}

		string depDir = resolvePath( projectDir, dep.path );

		// Check build cache
		ProjectConfig *depConfig = ProjectConfig::loadFromDirectory( depDir );
		if ( !depConfig )
		{
			cerr << "error: no blang.toml in dependency '" << dep.name << "' at " << depDir << endl;
			delete config;
			return 1;
		}

		vector<string> depSources = discoverSourceFiles( depDir );
		string depToml = readFileToString( depDir + "/blang.toml" );
		string depCacheKey = BuildCache::computeKey( depSources, depToml, {} );

		string cachedA, cachedBmod;
		if ( BuildCache::lookup( depCacheKey, cachedA, cachedBmod ) )
		{
			if ( verbose )
				cerr << "  cache hit for " << dep.name << " (" << depCacheKey.substr( 0, 12 ) << "...)" << endl;
			depAFiles.push_back( cachedA );
			depBmodFiles.push_back( cachedBmod );
			depHashes.push_back( depCacheKey );
			delete depConfig;
			continue;
		}

		delete depConfig;

		// Cache miss — build recursively
		string depA, depBmod, depHash;
		int ret = buildProject( depDir, exeDir, verbose, building, depA, depBmod, depHash );
		if ( ret != 0 )
		{
			delete config;
			return ret;
		}

		depAFiles.push_back( depA );
		depBmodFiles.push_back( depBmod );
		depHashes.push_back( depHash );
	}

	// Discover source files
	vector<string> sources = discoverSourceFiles( projectDir );
	if ( sources.empty() )
	{
		cerr << "error: no .b source files found in " << projectDir << endl;
		delete config;
		return 1;
	}

	// Resolve stdlib modules imported by the project's own sources (e.g.
	// `import timer;`). For binaries these are combined into the program so the
	// stdlib bodies are present; the matching runtime libs are linked below.
	// Libraries intentionally do NOT embed stdlib (it would duplicate symbols
	// when a downstream binary imports the same module) — they reference stdlib
	// declarations and the final binary supplies the bodies.
	set<string> projImports;
	for ( const auto &src : sources )
	{
		set<string> imp = parseImports( src );
		projImports.insert( imp.begin(), imp.end() );
	}
	vector<string> stdlibFiles = resolveStdlibFiles( exeDir, projImports );
	bool useCombine = !stdlibFiles.empty() && !config->isLibrary();

	// Compute cache key for this project
	string tomlContent = readFileToString( projectDir + "/blang.toml" );
	cacheHash = BuildCache::computeKey( sources, tomlContent, depHashes );

	// Check cache for this project
	if ( config->isLibrary() )
	{
		string cachedA, cachedBmod;
		if ( BuildCache::lookup( cacheHash, cachedA, cachedBmod ) )
		{
			if ( verbose )
				cerr << "  cache hit for " << config->getName() << " (" << cacheHash.substr( 0, 12 ) << "...)" << endl;
			outAFile = cachedA;
			outBmodFile = cachedBmod;
			delete config;
			return 0;
		}
	}

	string qcc = exeDir + "/qcc";

	// Build qcc command: all source files + dependency .bmod files
	vector<string> qccCmd = { qcc };
	for ( const auto &src : sources )
		qccCmd.push_back( src );
	for ( const auto &bmod : depBmodFiles )
		qccCmd.push_back( bmod );

	if ( config->isLibrary() )
	{
		// For libraries: emit .bmod and compile to .ll files
		string bmodPath = projectDir + "/" + config->getName() + ".bmod";
		qccCmd.push_back( "--emit-bmod" );
		qccCmd.push_back( bmodPath );

		int ret = runCommand( qccCmd, verbose, !verbose );
		if ( ret != 0 )
		{
			cerr << "error: compilation failed for " << config->getName() << endl;
			delete config;
			return 1;
		}

		// Compile each .ll to .o
		string llc = resolveLlc();
		if ( llc.empty() )
		{
			cerr << "error: llc not found" << endl;
			delete config;
			return 1;
		}

		vector<string> objFiles;
		for ( const auto &src : sources )
		{
			string base = getBaseName( src );
			string srcDir = getDirName( src );
			string llFile = srcDir + "/" + base + ".ll";
			string objFile = "/tmp/" + config->getName() + "_" + base + ".o";

			if ( access( llFile.c_str(), F_OK ) != 0 )
				continue;

			ret = emitObject( llc, llFile, objFile, verbose );
			if ( ret != 0 )
			{
				cerr << "error: IR compilation failed for " << base << endl;
				delete config;
				return 1;
			}
			remove( llFile.c_str() );
			objFiles.push_back( objFile );
		}

		// Archive into .a
		string aPath = projectDir + "/lib" + config->getName() + ".a";
		{
			vector<string> arCmd = { "ar", "rcs", aPath };
			for ( const auto &obj : objFiles )
				arCmd.push_back( obj );
			ret = runCommand( arCmd, verbose );
			if ( ret != 0 )
			{
				cerr << "error: archive creation failed" << endl;
				delete config;
				return 1;
			}
			for ( const auto &obj : objFiles )
				remove( obj.c_str() );
		}

		outAFile = aPath;
		outBmodFile = bmodPath;

		// Store in cache
		BuildCache::store( cacheHash, aPath, bmodPath );

		if ( verbose )
			cerr << "  built " << config->getName() << " -> " << aPath << " + " << bmodPath << endl;
	}
	else
	{
		// For binaries: compile and link. When the program imports stdlib
		// modules, build in --combine mode so the stdlib sources share scope
		// with the program and emit a single combined .ll; otherwise keep the
		// per-source .ll path (which preserves the existing dependency flow).
		string combinedLL;
		if ( useCombine )
		{
			combinedLL = projectDir + "/" + config->getName() + ".ll";
			qccCmd = { qcc, "--combine" };
			for ( const auto &sf : stdlibFiles )
				qccCmd.push_back( sf );
			for ( const auto &src : sources )
				qccCmd.push_back( src );
			for ( const auto &bmod : depBmodFiles )
				qccCmd.push_back( bmod );
			qccCmd.push_back( "-o" );
			qccCmd.push_back( combinedLL );

			// Forward [database] config so the generated main() opens the
			// default/named connections at startup.
			if ( !config->getDbUrl().empty() )
			{
				qccCmd.push_back( "--db-driver" );
				qccCmd.push_back( config->getDbDriver().empty()
					? "sqlite" : config->getDbDriver() );
				qccCmd.push_back( "--db-url" );
				qccCmd.push_back( config->getDbUrl() );
			}
			for ( const auto &c : config->getNamedDbConns() )
			{
				qccCmd.push_back( "--db-conn" );
				qccCmd.push_back( c.name );
				qccCmd.push_back( c.driver.empty() ? "sqlite" : c.driver );
				qccCmd.push_back( c.url );
			}
		}

		int ret = runCommand( qccCmd, verbose, !verbose );
		if ( ret != 0 )
		{
			cerr << "error: compilation failed for " << config->getName() << endl;
			delete config;
			return 1;
		}

		// Compile each .ll to .o
		string llc = resolveLlc();
		if ( llc.empty() )
		{
			cerr << "error: llc not found" << endl;
			delete config;
			return 1;
		}

		// The IR files to compile: a single combined module in --combine mode,
		// or one per project source otherwise. Track each .ll with the object
		// name to emit.
		vector<pair<string,string> > llToObj;
		if ( useCombine )
		{
			llToObj.push_back( { combinedLL,
				"/tmp/" + config->getName() + "_combined.o" } );
		}
		else
		{
			for ( const auto &src : sources )
			{
				string base = getBaseName( src );
				string srcDir = getDirName( src );
				llToObj.push_back( { srcDir + "/" + base + ".ll",
					"/tmp/" + config->getName() + "_" + base + ".o" } );
			}
		}

		vector<string> objFiles;
		for ( const auto &entry : llToObj )
		{
			const string &llFile = entry.first;
			const string &objFile = entry.second;

			if ( access( llFile.c_str(), F_OK ) != 0 )
				continue;

			ret = emitObject( llc, llFile, objFile, verbose );
			if ( ret != 0 )
			{
				cerr << "error: IR compilation failed for " << getBaseName( llFile ) << endl;
				delete config;
				return 1;
			}
			remove( llFile.c_str() );
			objFiles.push_back( objFile );
		}

		// Link
		string outName = config->getName();
		string cc = "cc";
#ifdef BCC_CC_PATH
		cc = BCC_CC_PATH;
#endif
		vector<string> linkCmd = { cc };
#if defined(BCC_HOST_ARCH) && defined(PLATFORM_DARWIN)
		linkCmd.push_back( "-arch" );
		linkCmd.push_back( BCC_HOST_ARCH );
#endif
		for ( const auto &obj : objFiles )
			linkCmd.push_back( obj );

		// Link dependency .a files
		for ( const auto &depA : depAFiles )
			linkCmd.push_back( depA );

		// Link BLang runtime libraries (order: dependents before dependencies)
		appendRuntimeLibs( linkCmd, exeDir,
			RuntimeLinkProfile{ /*withTestRunner=*/false, /*withDb=*/true } );

		// DB backend link flags (e.g. -lsqlite3); must follow libblang_db.a.
#ifdef BCC_DB_LINKFLAGS
		{
			string dbFlags = BCC_DB_LINKFLAGS;
			istringstream dbf( dbFlags );
			string tok;
			while ( dbf >> tok )
				linkCmd.push_back( tok );
		}
#endif

		linkCmd.push_back( "-lpthread" );
		linkCmd.push_back( "-o" );
		linkCmd.push_back( projectDir + "/" + outName );
#ifdef BCC_HAS_LIBUV
		linkCmd.push_back( "-luv" );
#endif

		ret = runCommand( linkCmd, verbose );
		if ( ret != 0 )
		{
			cerr << "error: linking failed for " << config->getName() << endl;
			for ( const auto &obj : objFiles )
				remove( obj.c_str() );
			delete config;
			return 1;
		}
		for ( const auto &obj : objFiles )
			remove( obj.c_str() );

		if ( verbose )
			cerr << "  built " << config->getName() << " -> " << projectDir + "/" + outName << endl;
	}

	building.erase( canonDir );
	delete config;
	return 0;
}

// bcc build subcommand
static int runBuild( int argc, char *argv[], const string &exeDir )
{
	bool verbose = false;
	string projectDir = ".";

	for ( int i = 2; i < argc; i++ )
	{
		string arg = argv[i];
		if ( arg == "-v" || arg == "--verbose" )
			verbose = true;
		else if ( arg[0] != '-' )
			projectDir = arg;
	}

	set<string> building;
	string outA, outBmod, hash;
	return buildProject( projectDir, exeDir, verbose, building, outA, outBmod, hash );
}

// bcc clean subcommand
static int runClean()
{
	string cacheDir = BuildCache::getCacheDir();
	cerr << "Removing build cache: " << cacheDir << endl;
	if ( BuildCache::clean() )
	{
		cerr << "Done." << endl;
		return 0;
	}
	else
	{
		cerr << "error: failed to remove cache directory" << endl;
		return 1;
	}
}

int main( int argc, char *argv[] )
{
	string exeDir = getExeDir( argv[0] );

	// Intercept subcommands before normal option parsing
	if ( argc >= 2 && string( argv[1] ) == "build" )
	{
		return runBuild( argc, argv, exeDir );
	}
	if ( argc >= 2 && string( argv[1] ) == "clean" )
	{
		return runClean();
	}
	if ( argc >= 2 && string( argv[1] ) == "test" )
	{
		return runTests( argc, argv, exeDir );
	}
	if ( argc >= 2 && string( argv[1] ) == "migrate" )
	{
		return runMigrate( argc, argv );
	}

	Options opts;
	if ( !parseArgs( argc, argv, opts ) )
	{
		printUsage( argv[0] );
		return 1;
	}

	string baseName = getBaseName( opts.inputFile );
	string srcDir = getDirName( opts.inputFile );

	// Effective optimization level: an explicit -O wins; otherwise --release
	// implies -O2 (design D3: --release keeps asserts, just optimizes).
	string effectiveOpt = opts.optLevel;
	if ( effectiveOpt.empty() && opts.release )
		effectiveOpt = "2";

	// Debug info stance (U3, S-A): `-g` forces -O0 for best line-table fidelity.
	// It overrides any -O/--release, and is computed here — in the single
	// effectiveOpt block — so both qcc (layer 1) and emitObject/llc (layer 2)
	// see -O0. (Emission stays verifier-clean under -O; a future -g -O2 is a
	// small step — spec §"the -g × -O stance".)
	if ( opts.debugInfo )
		effectiveOpt = "0";

	// Locate qcc (same directory as bcc)
	string qcc = exeDir + "/qcc";

	// Check for stdlib files to include via --combine. Local's base modules
	// (sys, buffer, fs, net) are always included in dependency order (buffer
	// before fs/net); origin's import-gated extras (collections, timer, ...)
	// are pulled in only when the program imports them. The matching runtime
	// libraries are linked unconditionally in step 3, so any module resolves.
	vector<string> stdlibFiles =
		resolveStdlibFiles( exeDir, parseImports( opts.inputFile ) );

	// Step 1: Parse and generate LLVM IR
	if ( opts.verbose )
		cerr << "--- Step 1: Parsing and generating LLVM IR ---" << endl;

	{
		vector<string> cmd = { qcc };
		if ( opts.jsonDiagnostics )
			cmd.push_back( "--json" );
		if ( opts.werror )
			cmd.push_back( "-Werror" );
		if ( !effectiveOpt.empty() )
			cmd.push_back( string( "-O" ) + effectiveOpt );   // layer 1: IR passes
		if ( opts.debugInfo )
			cmd.push_back( "-g" );                            // DWARF debug info (U3)
		if ( !stdlibFiles.empty() )
		{
			cmd.push_back( "--combine" );
			for ( const auto &sf : stdlibFiles )
				cmd.push_back( sf );
		}
		cmd.push_back( opts.inputFile );
		// Always capture qcc output (its IR goes to stdout, diagnostics to stderr).
		int ret = runCommand( cmd, opts.verbose, true );
		// In --json mode, replay qcc's captured stderr verbatim — it is exactly the
		// JSON diagnostics array (quiet mode emits nothing else there) — so the
		// user sees pure JSON and never the IR (which went to the discarded stdout).
		if ( opts.jsonDiagnostics && !opts.verbose )
		{
			FILE *jf = fopen( "/tmp/bcc_stderr.txt", "r" );
			if ( jf )
			{
				char jbuf[1024];
				while ( fgets( jbuf, sizeof( jbuf ), jf ) )
					fputs( jbuf, stderr );
				fclose( jf );
			}
		}
		if ( ret != 0 )
		{
			// Show captured compiler errors. In --json mode the JSON was already
			// replayed above, so skip the human summary to keep the output pure.
			if ( !opts.verbose && !opts.jsonDiagnostics )
			{
				FILE *f = fopen( "/tmp/bcc_stderr.txt", "r" );
				if ( f )
				{
					char buf[1024];
					while ( fgets( buf, sizeof( buf ), f ) )
					{
						// Filter out TRACE lines, only show real errors
						string line = buf;
						if ( line.find( "[TRACE]" ) == string::npos &&
						     line.find( "Saving position" ) == string::npos &&
						     line.find( "Not a decl" ) == string::npos &&
						     line.find( "resetting position" ) == string::npos )
							cerr << line;
					}
					fclose( f );
				}
			}
			// Keep --json output pure: qcc already emitted the JSON diagnostics.
			if ( !opts.jsonDiagnostics )
				cerr << "error: compilation failed" << endl;
			return 1;
		}
	}

	// Find the .ll file (qcc writes it next to the source file)
	string irFile = srcDir + "/" + baseName + ".ll";
	if ( access( irFile.c_str(), F_OK ) != 0 )
	{
		// Also check current directory
		irFile = baseName + ".ll";
		if ( access( irFile.c_str(), F_OK ) != 0 )
		{
			cerr << "error: no .ll file generated (is qcc built with LLVM?)" << endl;
			return 1;
		}
	}

	// If -S, we're done — just move the IR to the output location
	if ( opts.emitIROnly )
	{
		string outFile = opts.outputFile.empty() ? baseName + ".ll" : opts.outputFile;
		if ( irFile != outFile )
		{
			rename( irFile.c_str(), outFile.c_str() );
		}
		if ( opts.verbose )
			cerr << "Wrote " << outFile << endl;
		return 0;
	}

	// Step 2: Compile IR to object file
	if ( opts.verbose )
		cerr << "--- Step 2: Compiling IR to object file ---" << endl;

	string llc = resolveLlc();
	if ( llc.empty() )
	{
		cerr << "error: llc not found (install llvm-18 or llvm)" << endl;
		remove( irFile.c_str() );
		return 1;
	}

	string objFile = "/tmp/" + baseName + ".o";
	{
		// Layer 2 of -O (llc backend) + cross-compile triple (U2) + DWARF (U3).
		int ret = emitObject( llc, irFile, objFile, opts.verbose,
			effectiveOpt, opts.targetTriple, opts.debugInfo );
		if ( ret != 0 )
		{
			cerr << "error: IR compilation failed" << endl;
			remove( irFile.c_str() );
			return 1;
		}
	}

	// Clean up IR file (it was an intermediate)
	remove( irFile.c_str() );

	// If -c, we're done — just move the object to the output location
	if ( opts.compileOnly )
	{
		string outFile = opts.outputFile.empty() ? baseName + ".o" : opts.outputFile;
		rename( objFile.c_str(), outFile.c_str() );
		if ( opts.verbose )
			cerr << "Wrote " << outFile << endl;
		return 0;
	}

	// Step 3: Link to native binary
	if ( opts.verbose )
		cerr << "--- Step 3: Linking ---" << endl;

	string outFile = opts.outputFile.empty() ? "a.out" : opts.outputFile;
	{
		string cc = "cc";
#ifdef BCC_CC_PATH
		cc = BCC_CC_PATH;
#endif
		vector<string> cmd = { cc };
#if defined(BCC_HOST_ARCH) && defined(PLATFORM_DARWIN)
		cmd.push_back( "-arch" );
		cmd.push_back( BCC_HOST_ARCH );
#endif
		cmd.push_back( objFile );

		// Link BLang runtime libraries (order: dependents before dependencies)
		appendRuntimeLibs( cmd, exeDir,
			RuntimeLinkProfile{ /*withTestRunner=*/false, /*withDb=*/true } );

		// DB backend link flags (e.g. -lsqlite3); must follow libblang_db.a.
#ifdef BCC_DB_LINKFLAGS
		{
			string dbFlags = BCC_DB_LINKFLAGS;
			istringstream dbf( dbFlags );
			string tok;
			while ( dbf >> tok )
				cmd.push_back( tok );
		}
#endif

		cmd.push_back( "-lpthread" );

		cmd.push_back( "-o" );
		cmd.push_back( outFile );
		for ( const auto &flag : opts.linkerFlags )
			cmd.push_back( flag );
#ifdef BCC_HAS_LIBUV
		cmd.push_back( "-luv" );
#endif
		int ret = runCommand( cmd, opts.verbose );
		if ( ret != 0 )
		{
			cerr << "error: linking failed" << endl;
			remove( objFile.c_str() );
			return 1;
		}
	}

	// Clean up object file
	remove( objFile.c_str() );

	if ( opts.verbose )
		cerr << "Wrote " << outFile << endl;

	return 0;
}
