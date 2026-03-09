#ifndef BLANG_PROJECT_CONFIG_H_
#define BLANG_PROJECT_CONFIG_H_

#include <string>
#include <vector>

struct Dependency
{
	std::string name;
	std::string path;
	std::string gitUrl;
	std::string tag;
};

class ProjectConfig
{
public:
	static ProjectConfig *loadFromFile( const std::string &path );
	static ProjectConfig *loadFromDirectory( const std::string &dir );

	const std::string &getName() const { return mName; }
	const std::string &getVersion() const { return mVersion; }
	const std::string &getType() const { return mType; }
	const std::vector<Dependency> &getDependencies() const { return mDeps; }

	bool isLibrary() const { return mType == "lib"; }
	bool isBinary() const { return mType == "bin"; }

private:
	std::string mName;
	std::string mVersion;
	std::string mType = "bin";
	std::vector<Dependency> mDeps;
};

#endif // BLANG_PROJECT_CONFIG_H_
