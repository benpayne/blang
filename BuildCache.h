#ifndef BLANG_BUILD_CACHE_H_
#define BLANG_BUILD_CACHE_H_

#include <string>
#include <vector>

class BuildCache
{
public:
	static std::string computeKey( const std::vector<std::string> &sourceFiles,
	                               const std::string &tomlContent,
	                               const std::vector<std::string> &depHashes );

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
