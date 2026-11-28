// modules-v2-lsp U3: bcc's project rules, shared (see ProjectLayout.h). The
// helpers below are moved VERBATIM from bcc.cpp; bcc calls them, and so does
// blangd. Process-free: no fork/exec/system/popen, no network.

#include "ProjectLayout.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sha256.h"

using namespace std;

namespace BlangProject
{

vector<string> discoverSourceFiles( const string &dir )
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

// Resolve absolute path from a possibly relative path
string resolvePath( const string &basePath, const string &relPath )
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

// Checkout cache root, sibling of BuildCache's objects/ dir so `bcc clean`
// (which removes the whole blang cache directory) clears checkouts too.
string gitCacheRoot()
{
	const char *xdg = getenv( "XDG_CACHE_HOME" );
	if ( xdg != nullptr && xdg[0] != '\0' )
		return string( xdg ) + "/blang/git";
	const char *home = getenv( "HOME" );
	if ( home != nullptr && home[0] != '\0' )
		return string( home ) + "/.cache/blang/git";
	return "/tmp/.cache/blang/git";
}

string shortHashHex( const string &s )
{
	SHA256_CTX ctx;
	uint8_t h[32];
	sha256_init( &ctx );
	sha256_update( &ctx, (const uint8_t *)s.data(), s.size() );
	sha256_final( &ctx, h );
	static const char *hex = "0123456789abcdef";
	string out;
	for ( int i = 0; i < 8; i++ )
	{
		out += hex[h[i] >> 4];
		out += hex[h[i] & 0xf];
	}
	return out;
}

// The pure LOCATE half of bcc's git-dependency resolution. The pin (rev, else
// tag) is REQUIRED — an unpinned git dep would make the build irreproducible.
// The checkout lives at <cache>/<name>-<hash(url@pin)>; it is warm iff its
// blang.toml exists. Fetching (bcc only) is the Fetcher's job.
GitLocation locateGitCheckout( const Dependency &dep )
{
	GitLocation loc;
	loc.pin = !dep.rev.empty() ? dep.rev : dep.tag;
	if ( loc.pin.empty() )
	{
		loc.error = "git dependency '" + dep.name +
			"' must pin a version: add tag = \"...\" or rev = \"...\"";
		return loc;
	}
	loc.dir = gitCacheRoot() + "/" + dep.name + "-" + shortHashHex( dep.gitUrl + "@" + loc.pin );
	struct stat st;
	loc.warm = stat( ( loc.dir + "/blang.toml" ).c_str(), &st ) == 0;
	return loc;
}

string canonicalPath( const string &p )
{
	char resolved[PATH_MAX];
	if ( realpath( p.c_str(), resolved ) )
		return string( resolved );
	return p;
}

DepDir resolveDependencyDir( const string &declaringDir, const Dependency &dep,
	const Fetcher *fetch )
{
	DepDir dd;
	if ( !dep.gitUrl.empty() )
	{
		dd.git = locateGitCheckout( dep );
		if ( !dd.git.error.empty() )
		{
			dd.error = DepDir::Error::kUnpinned;
			dd.message = dd.git.error;
			return dd;
		}
		if ( fetch != nullptr )
		{
			// Fetch (or reuse) a pinned checkout, then treat it as a path dep.
			string outDir;
			if ( ( *fetch )( dep, dd.git, outDir ) != 0 )
			{
				dd.error = DepDir::Error::kFetchFailed;
				return dd;
			}
			dd.dir = outDir;
		}
		else if ( !dd.git.warm )
		{
			// Never fetched here (blangd): report, don't clone.
			dd.error = DepDir::Error::kUnfetched;
			dd.dir = dd.git.dir;
			return dd;
		}
		else
		{
			dd.dir = dd.git.dir;
		}
	}
	else if ( dep.path.empty() )
	{
		dd.error = DepDir::Error::kNoPathOrGit;
		dd.message = "dependency '" + dep.name + "' has neither a path nor a git url";
		return dd;
	}
	else
	{
		dd.dir = resolvePath( declaringDir, dep.path );
	}
	// Canonical origin of this dep = realpath of its project dir (D5). The same
	// value the dep's OWN build stamps via --module-origin, so digests agree.
	dd.origin = canonicalPath( dd.dir );
	return dd;
}

string findProjectRoot( const string &startDir )
{
	string dir = canonicalPath( startDir.empty() ? "." : startDir );
	for ( ;; )
	{
		struct stat st;
		if ( stat( ( dir + "/blang.toml" ).c_str(), &st ) == 0 )
			return dir;
		if ( dir.empty() || dir == "/" )
			return "";
		size_t slash = dir.rfind( '/' );
		dir = ( slash == 0 || slash == string::npos ) ? string( "/" ) : dir.substr( 0, slash );
	}
}

} // namespace BlangProject
