#ifndef BLANG_BUILD_CACHE_H_
#define BLANG_BUILD_CACHE_H_

#include <string>
#include <vector>

class BuildCache
{
public:
	// The .bmod format version participates in the key: a cache entry holds a
	// .bmod as well as a .a, so an entry written before a format change must not
	// be served to a compiler expecting the new shape.
	//
	// `formatVersion` exists so a test can prove the salt actually participates
	// (pass two versions, assert the keys differ). Production callers use the
	// default, which is the real BlangBmod::kFormatVersion.
	static std::string computeKey( const std::vector<std::string> &sourceFiles,
	                               const std::string &tomlContent,
	                               const std::vector<std::string> &depHashes,
	                               int formatVersion = -1 );

	static bool lookup( const std::string &hash,
	                    std::string &aFilePath,
	                    std::string &bmodFilePath );

	static bool store( const std::string &hash,
	                   const std::string &aFile,
	                   const std::string &bmodFile );

	static std::string getCacheDir();

	static bool clean();
};

#endif /* BLANG_BUILD_CACHE_H_ */
