#include "StdlibSelect.h"

#include <cctype>
#include <sstream>

#include <unistd.h>

using namespace std;

namespace BlangStdlib
{

// Extract the set of imported module names from BLang source text.
// Recognizes top-level `import name;` and `import name.sub;` statements,
// returning just the leading identifier ("name"). Comments and other tokens
// are ignored well enough for stdlib resolution. (Moved verbatim from bcc.cpp's
// parseImports; the file read stays in bcc.)
set<string> scanImports( const string &sourceText )
{
	set<string> imports;
	istringstream in( sourceText );
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
// in, so an unused module never pollutes the namespace. (Moved verbatim from
// bcc.cpp's resolveStdlibFiles; the candidate is stdlibDir + "/" + name + ".b",
// identical to the former exeDir + "/stdlib/" + name + ".b".)
vector<string> selectStdlibFiles( const string &stdlibDir,
	const set<string> &imports )
{
	vector<string> files;
	set<string> handled;
	auto addIfPresent = [&]( const string &name ) {
		string candidate = stdlibDir + "/" + name + ".b";
		if ( access( candidate.c_str(), F_OK ) == 0 )
			files.push_back( candidate );
		handled.insert( name );
	};

	// Local's base modules are always combined, in dependency-safe order:
	// buffer.b must precede fs.b and net.b (they use Buffer). These ship with
	// the compiler and many programs use them (File, Socket, Buffer) without an
	// explicit import, so inclusion is unconditional (preserves local behavior).
	//
	// modules-v2-graph U3 (D13): `collections` is now UNCONDITIONAL — it provides
	// the PRELUDE types Map/Set, which are automatically in scope with zero imports.
	// qcc parses it into a prelude holding scope and promotes only Map/Set (its free
	// function `sort` stays qualified as collections.sort). A program defining its
	// own Map/Set shadows the prelude via the prelude-scope hierarchy. `buffer` (the
	// Buffer prelude type) was already unconditional. Placed before cli (cli uses Map).
	for ( const char *name : { "sys", "buffer", "collections", "fs", "net" } )
		addIfPresent( name );

	// Origin's import-gated extras: only pulled in when the program imports
	// them, so an unused module never pollutes the namespace. (collections moved
	// to the unconditional base list above — D13 prelude load. cli stays gated: it
	// is library-tier, qualified access.)
	static const char *kKnownOrder[] = { "timer",
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

} // namespace BlangStdlib
