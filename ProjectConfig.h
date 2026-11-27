#ifndef BLANG_PROJECT_CONFIG_H_
#define BLANG_PROJECT_CONFIG_H_

#include <string>
#include <vector>

struct Dependency
{
	std::string name;
	std::string path;
	std::string gitUrl;
	std::string tag;   // git: tag or branch name (cloned with --branch)
	std::string rev;   // git: exact commit hash (full clone + checkout)
	int line = 0;      // blang.toml line of this [deps] entry (modules-v2-lsp U3: located diagnostics)
};

// A blang.toml problem the parser recovered from (modules-v2-lsp U3). With no
// issue sink, loadFromText prints "<path>:<line>: parse error: <message>" to
// cerr exactly as bcc always has; with one, it appends here and prints nothing.
struct ConfigIssue
{
	int line;
	std::string message;
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
	// Parse blang.toml TEXT (an editor buffer, or a file loadFromFile read).
	// `issues` == nullptr keeps bcc's cerr output byte-identical.
	static ProjectConfig *loadFromText( const std::string &path, const std::string &text,
		std::vector<ConfigIssue> *issues );
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
