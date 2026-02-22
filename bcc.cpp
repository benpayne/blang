// bcc - BLang Compiler Driver
//
// User-facing CLI that orchestrates the full compilation pipeline:
//   1. Parse + generate LLVM IR  (via qcc)
//   2. Compile IR to object file (via llc)
//   3. Link to native binary     (via cc)
//
// Usage:
//   bcc source.c                  # compile and link -> a.out
//   bcc source.c -o myprogram     # compile and link -> myprogram
//   bcc -S source.c               # emit LLVM IR only -> source.ll
//   bcc -c source.c               # compile to object only -> source.o
//   bcc -v source.c               # verbose, show each pipeline step

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <sys/wait.h>
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
	cerr << "Usage: " << progName << " [options] <source.c>" << endl;
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

int main( int argc, char *argv[] )
{
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

	string llc = findTool( "llc-18", { "llc" } );
	if ( llc.empty() )
	{
		cerr << "error: llc not found (install llvm-18 or llvm)" << endl;
		// Cleanup IR file
		remove( irFile.c_str() );
		return 1;
	}

	string objFile = "/tmp/" + baseName + ".o";
	{
		vector<string> cmd = { llc, "-filetype=obj", irFile, "-o", objFile };
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
		vector<string> cmd = { "cc", objFile, "-o", outFile };
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
