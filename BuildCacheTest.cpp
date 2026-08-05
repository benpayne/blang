// BuildCacheTest — unit tests for the content-addressed build cache key.
//
// Epic modules-v2-exports, U2, done-condition 7: "BuildCache keys incorporate a
// .bmod format version: a test proves a format-version bump invalidates a warm
// cache entry."
//
// The cache stores a .bmod alongside the .a, so an entry written before a
// format change must not be served to a compiler that expects the new shape —
// otherwise a stale interface surfaces as a syntax error inside a generated file
// at a consumer's build, which is precisely the failure this epic exists to
// eliminate.

#include "BuildCache.h"
#include "BmodFormat.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static int gFailures = 0;

static void check( bool cond, const std::string &what )
{
	if ( cond )
	{
		std::cout << "  PASS  " << what << std::endl;
	}
	else
	{
		std::cout << "  FAIL  " << what << std::endl;
		gFailures++;
	}
}

static std::string writeTemp( const std::string &name, const std::string &content )
{
	std::string path = "/tmp/" + name;
	std::ofstream f( path );
	f << content;
	f.close();
	return path;
}

int main()
{
	std::cout << "BuildCache key tests" << std::endl;

	std::string src = writeTemp( "bc_test_src.b", "pub fn add(int a, int b) -> int { return a + b; }\n" );
	std::string toml = "[project]\nname = \"x\"\ntype = \"lib\"\n";
	std::vector<std::string> sources = { src };
	std::vector<std::string> noDeps;

	// 1. Deterministic: same inputs, same key.
	std::string k1 = BuildCache::computeKey( sources, toml, noDeps );
	std::string k2 = BuildCache::computeKey( sources, toml, noDeps );
	check( k1 == k2, "key is deterministic for identical inputs" );
	check( k1.size() == 64, "key is a 64-hex-char SHA-256 digest" );

	// 2. THE done-condition-7 property: bumping the .bmod format version changes
	//    the key, so every warm entry misses and is rebuilt.
	std::string kCurrent = BuildCache::computeKey( sources, toml, noDeps, BlangBmod::kFormatVersion );
	std::string kNext    = BuildCache::computeKey( sources, toml, noDeps, BlangBmod::kFormatVersion + 1 );
	check( kCurrent != kNext,
		"a .bmod format-version bump invalidates a warm cache entry" );

	// 3. The default really is the shipped constant — otherwise test 2 would
	//    prove something about a parameter nobody uses in production.
	check( k1 == kCurrent,
		"the default format version is the shipped BlangBmod::kFormatVersion" );

	// 4. Source content still participates (the salt did not displace it).
	std::string src2 = writeTemp( "bc_test_src2.b", "pub fn add(int a, int b) -> int { return a - b; }\n" );
	std::string kOtherSrc = BuildCache::computeKey( { src2 }, toml, noDeps );
	check( k1 != kOtherSrc, "source content still participates in the key" );

	// 5. blang.toml and dep hashes still participate.
	std::string kOtherToml = BuildCache::computeKey( sources, toml + "version = \"2\"\n", noDeps );
	check( k1 != kOtherToml, "blang.toml still participates in the key" );
	std::string kWithDep = BuildCache::computeKey( sources, toml, { "deadbeef" } );
	check( k1 != kWithDep, "dependency hashes still participate in the key" );

	std::remove( src.c_str() );
	std::remove( src2.c_str() );

	if ( gFailures > 0 )
	{
		std::cout << gFailures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "all BuildCache key tests passed" << std::endl;
	return 0;
}
