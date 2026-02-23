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
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

struct Options
{
	string inputFile;
	string outputFile;
	bool emitIROnly = false;     // -S
	bool compileOnly = false;    // -c
	bool verbose = false;        // -v
	vector<string> linkerFlags;  // -l, -L, etc.
};

static void printUsage( const char *progName )
{
	cerr << "Usage: " << progName << " [options] <source.b>" << endl;
	cerr << "       " << progName << " test [--verbose]" << endl;
	cerr << endl;
	cerr << "Subcommands:" << endl;
	cerr << "  test         Discover and run BLang test files" << endl;
	cerr << endl;
	cerr << "Options:" << endl;
	cerr << "  -o <file>    Output file name" << endl;
	cerr << "  -S           Emit LLVM IR only (.ll)" << endl;
	cerr << "  -c           Compile to object file only (.o)" << endl;
	cerr << "  -v           Verbose output" << endl;
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

// Run qcc on a single file; return true on success (exit 0)
static bool parseFile( const string &qcc, const string &file, bool verbose )
{
	string cmd = "\"" + qcc + "\" \"" + file + "\" >/dev/null 2>/dev/null";
	int ret = system( cmd.c_str() );
	if ( WIFEXITED( ret ) )
		return WEXITSTATUS( ret ) == 0;
	return false;
}

// bcc test subcommand
//
// Discovery strategy:
//   1. If a tests/ subdirectory exists in the current directory, search there.
//   2. Otherwise search the current directory for *_test.b files.
//
// Each discovered .b file is passed to qcc for parse-only verification.
// Results are reported with a summary line at the end; exit code is non-zero
// when any test fails.
static int runTests( int argc, char *argv[] )
{
	bool verbose = false;
	for ( int i = 2; i < argc; i++ )
	{
		string arg = argv[i];
		if ( arg == "--verbose" || arg == "-v" )
			verbose = true;
	}

	// Locate qcc alongside bcc
	char exeBuf[4096];
	string exeDir = ".";
	ssize_t len = readlink( "/proc/self/exe", exeBuf, sizeof( exeBuf ) - 1 );
	if ( len > 0 )
	{
		exeBuf[len] = '\0';
		string exePath = exeBuf;
		size_t slash = exePath.rfind( '/' );
		if ( slash != string::npos )
			exeDir = exePath.substr( 0, slash );
	}
	string qcc = exeDir + "/qcc";

	// Determine search root
	string searchRoot;
	bool testsSubdirExists = isDirectory( "tests" );
	if ( testsSubdirExists )
	{
		searchRoot = "tests";
		cerr << "bcc test: searching tests/ directory" << endl;
	}
	else
	{
		searchRoot = ".";
		cerr << "bcc test: no tests/ directory found, searching current directory for *.b files" << endl;
	}

	vector<string> files = collectTestFiles( searchRoot );

	if ( files.empty() )
	{
		cerr << "bcc test: no .b files found in " << searchRoot << endl;
		return 0;
	}

	int passed = 0;
	int failed = 0;

	for ( const auto &file : files )
	{
		bool ok = parseFile( qcc, file, verbose );
		if ( ok )
		{
			passed++;
			cerr << "  PASS  " << file << endl;
		}
		else
		{
			failed++;
			cerr << "  FAIL  " << file << endl;
		}
	}

	cerr << endl;
	cerr << "Results: " << passed << " passed, " << failed << " failed"
	     << " (" << ( passed + failed ) << " total)" << endl;

	return ( failed > 0 ) ? 1 : 0;
}

int main( int argc, char *argv[] )
{
	// Intercept the 'test' subcommand before normal option parsing
	if ( argc >= 2 && string( argv[1] ) == "test" )
	{
		return runTests( argc, argv );
	}

	Options opts;
	if ( !parseArgs( argc, argv, opts ) )
	{
		printUsage( argv[0] );
		return 1;
	}

	string exeDir = getExeDir( argv[0] );
	string baseName = getBaseName( opts.inputFile );
	string srcDir = getDirName( opts.inputFile );

	// Locate qcc (same directory as bcc)
	string qcc = exeDir + "/qcc";

	// Step 1: Parse and generate LLVM IR
	if ( opts.verbose )
		cerr << "--- Step 1: Parsing and generating LLVM IR ---" << endl;

	{
		vector<string> cmd = { qcc, opts.inputFile };
		int ret = runCommand( cmd, opts.verbose, true );
		if ( ret != 0 )
		{
			// Show captured compiler errors
			if ( !opts.verbose )
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

	string llc;
#ifdef BCC_LLC_PATH
	// Use the llc path discovered at build time
	if ( access( BCC_LLC_PATH, X_OK ) == 0 )
		llc = BCC_LLC_PATH;
#endif
	if ( llc.empty() )
		llc = findTool( "llc-18", { "llc" } );
	if ( llc.empty() )
	{
		cerr << "error: llc not found (install llvm-18 or llvm)" << endl;
		remove( irFile.c_str() );
		return 1;
	}

	string objFile = "/tmp/" + baseName + ".o";
	{
		vector<string> cmd = { llc, "-filetype=obj" };
#ifdef BCC_HOST_ARCH
		cmd.push_back( string( "-mtriple=" ) + BCC_HOST_ARCH + "-apple-darwin" );
#endif
		cmd.push_back( irFile );
		cmd.push_back( "-o" );
		cmd.push_back( objFile );
		int ret = runCommand( cmd, opts.verbose );
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
#ifdef BCC_HOST_ARCH
		cmd.push_back( "-arch" );
		cmd.push_back( BCC_HOST_ARCH );
#endif
		cmd.push_back( objFile );
		cmd.push_back( "-o" );
		cmd.push_back( outFile );
		for ( const auto &flag : opts.linkerFlags )
			cmd.push_back( flag );
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
