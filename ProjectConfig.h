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

// A named database connection from [database.<name>].
struct DbConnConfig
{
	std::string name;
	std::string driver;   // "sqlite" / "postgres"
	std::string url;      // connection string; may be "env:VAR" (resolved at runtime)
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

	// [database] default connection (empty when no [database] section present).
	const std::string &getDbDriver() const { return mDbDriver; }
	const std::string &getDbUrl() const { return mDbUrl; }
	// [database.<name>] named connections for @db("name") routing.
	const std::vector<DbConnConfig> &getNamedDbConns() const { return mNamedDbConns; }

	bool isLibrary() const { return mType == "lib"; }
	bool isBinary() const { return mType == "bin"; }

private:
	std::string mName;
	std::string mVersion;
	std::string mType = "bin";
	std::vector<Dependency> mDeps;
	std::string mDbDriver;
	std::string mDbUrl;
	std::vector<DbConnConfig> mNamedDbConns;
};

#endif // BLANG_PROJECT_CONFIG_H_
