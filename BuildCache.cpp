#include "BuildCache.h"
#include "sha256.h"
#include "BmodFormat.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

static bool fileExists( const string &path )
{
	return access( path.c_str(), F_OK ) == 0;
}

static bool mkdirRecursive( const string &path )
{
	string current;
	for ( size_t i = 0; i < path.size(); i++ )
	{
		current += path[i];
		if ( path[i] == '/' && i > 0 )
		{
			mkdir( current.c_str(), 0755 );
		}
	}
	mkdir( path.c_str(), 0755 );
	return fileExists( path );
}

static bool copyFile( const string &src, const string &dst )
{
	ifstream in( src, ios::binary );
	if ( !in.is_open() )
	{
		return false;
	}
	ofstream out( dst, ios::binary );
	if ( !out.is_open() )
	{
		return false;
	}
	out << in.rdbuf();
	return out.good();
}

static bool removeRecursive( const string &path )
{
	struct stat st;
	if ( stat( path.c_str(), &st ) != 0 )
	{
		return true;
	}

	if ( S_ISDIR( st.st_mode ) )
	{
		DIR *dir = opendir( path.c_str() );
		if ( !dir )
		{
			return false;
		}
		struct dirent *entry;
		while ( ( entry = readdir( dir ) ) != nullptr )
		{
			if ( strcmp( entry->d_name, "." ) == 0 || strcmp( entry->d_name, ".." ) == 0 )
			{
				continue;
			}
			string child = path + "/" + entry->d_name;
			if ( !removeRecursive( child ) )
			{
				closedir( dir );
				return false;
			}
		}
		closedir( dir );
		return rmdir( path.c_str() ) == 0;
	}
	else
	{
		return unlink( path.c_str() ) == 0;
	}
}

static string hashToHex( const uint8_t hash[32] )
{
	ostringstream ss;
	for ( int i = 0; i < 32; i++ )
	{
		ss << hex << setfill( '0' ) << setw( 2 ) << (int)hash[i];
	}
	return ss.str();
}

static string readFileContent( const string &path )
{
	ifstream f( path, ios::binary );
	if ( !f.is_open() )
	{
		return "";
	}
	ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static string extractFilename( const string &path )
{
	size_t pos = path.rfind( '/' );
	if ( pos == string::npos )
	{
		return path;
	}
	return path.substr( pos + 1 );
}

string BuildCache::computeKey( const vector<string> &sourceFiles,
                                const string &tomlContent,
                                const vector<string> &depHashes,
                                int formatVersion )
{
	if ( formatVersion < 0 )
		formatVersion = BlangBmod::kFormatVersion;

	SHA256_CTX ctx;
	sha256_init( &ctx );

	// Salt with the .bmod interface FORMAT version. A cache entry holds a .bmod
	// as well as a .a, so an entry written before a format change would
	// otherwise be served as a valid interface to a compiler that expects the
	// new shape. Bumping BlangBmod::kFormatVersion invalidates every entry —
	// which is the point.
	//
	// NOTE (known-issues KI-1): this key still hashes file CONTENTS only — no
	// filenames and no separator between files — so renaming a source, or moving
	// a line between two sources in one project, does not change it. Filed, not
	// fixed here: it is a behavioural change to every project's key and traces
	// to no requirement in this epic.
	string formatSalt = "bmod-format:" + std::to_string( formatVersion ) + "\n";
	sha256_update( &ctx, (const uint8_t *)formatSalt.data(), formatSalt.size() );

	for ( const auto &file : sourceFiles )
	{
		string content = readFileContent( file );
		sha256_update( &ctx, (const uint8_t *)content.data(), content.size() );
	}

	sha256_update( &ctx, (const uint8_t *)tomlContent.data(), tomlContent.size() );

	for ( const auto &dep : depHashes )
	{
		sha256_update( &ctx, (const uint8_t *)dep.data(), dep.size() );
	}

	uint8_t hash[32];
	sha256_final( &ctx, hash );
	return hashToHex( hash );
}

bool BuildCache::lookup( const string &hash,
                          string &aFilePath,
                          string &bmodFilePath )
{
	string dir = getCacheDir() + "/" + hash;

	if ( !fileExists( dir ) )
	{
		return false;
	}

	/* Scan directory for .a and .bmod files */
	DIR *d = opendir( dir.c_str() );
	if ( !d )
	{
		return false;
	}

	bool foundA = false;
	bool foundBmod = false;
	struct dirent *entry;

	while ( ( entry = readdir( d ) ) != nullptr )
	{
		string name = entry->d_name;
		size_t len = name.size();

		if ( len > 2 && name.substr( len - 2 ) == ".a" )
		{
			aFilePath = dir + "/" + name;
			foundA = true;
		}
		else if ( len > 5 && name.substr( len - 5 ) == ".bmod" )
		{
			bmodFilePath = dir + "/" + name;
			foundBmod = true;
		}
	}
	closedir( d );

	return foundA && foundBmod;
}

bool BuildCache::store( const string &hash,
                         const string &aFile,
                         const string &bmodFile )
{
	string dir = getCacheDir() + "/" + hash;

	if ( !mkdirRecursive( dir ) )
	{
		return false;
	}

	string aDst = dir + "/" + extractFilename( aFile );
	string bmodDst = dir + "/" + extractFilename( bmodFile );

	if ( !copyFile( aFile, aDst ) )
	{
		return false;
	}
	if ( !copyFile( bmodFile, bmodDst ) )
	{
		return false;
	}

	return true;
}

string BuildCache::getCacheDir()
{
	const char *xdg = getenv( "XDG_CACHE_HOME" );
	if ( xdg && xdg[0] != '\0' )
	{
		return string( xdg ) + "/blang/objects";
	}

	const char *home = getenv( "HOME" );
	if ( home && home[0] != '\0' )
	{
		return string( home ) + "/.cache/blang/objects";
	}

	return "/tmp/.cache/blang/objects";
}

bool BuildCache::clean()
{
	string dir = getCacheDir();

	/* Walk up to remove /blang/objects -> /blang -> only if empty */
	if ( !removeRecursive( dir ) )
	{
		return false;
	}

	return true;
}
