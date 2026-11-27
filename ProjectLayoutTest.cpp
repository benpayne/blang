// modules-v2-lsp U3 — ProjectLayoutTest: the shared project rules (ProjectLayout,
// ProjectConfig::loadFromText, BlangStdlib::scanImportsAll), driver-free. Usage:
//   projectLayoutTest <source root>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "ProjectConfig.h"
#include "ProjectLayout.h"
#include "StdlibSelect.h"

using namespace BlangProject;

static int gFailures = 0;
#define CHECK( cond ) \
	do { if ( !( cond ) ) { std::fprintf( stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); gFailures++; } } while ( false )

static void writeFile( const std::string &p, const std::string &text )
{
	std::ofstream( p ) << text;
}

static std::string mkd( const std::string &p )
{
	mkdir( p.c_str(), 0755 );
	return p;
}

// A visitor that records the walk (for semantics 1-6) and never builds anything.
struct RecVisitor : WalkVisitor<std::string>
{
	std::vector<std::string> events;
	bool continueOnError = true;
	ProjectConfig *load( const std::string &dir ) override
	{
		std::ifstream probe( dir + "/blang.toml" );
		if ( !probe.is_open() )
			return nullptr;   // no readable blang.toml (as loadFromFile)
		return ProjectConfig::loadFromText( dir + "/blang.toml", readAll( dir + "/blang.toml" ), &issues );
	}
	int onRootLoadError( const std::string &dir ) override { events.push_back( "rootload:" + dir ); return 1; }
	int enter( const WalkNode &n ) override { events.push_back( "enter:" + n.config->getName() ); return 0; }
	int onDepError( const WalkNode &p, const Dependency &d, const DepDir &dd ) override
	{
		events.push_back( "deperr:" + p.config->getName() + ":" + d.name + ":" +
			std::to_string( (int)dd.error ) );
		return continueOnError ? 0 : 1;
	}
	int onCycle( const WalkNode &n ) override { events.push_back( "cycle:" + n.canonDir ); return continueOnError ? 0 : 1; }
	int leave( const WalkNode &n, const std::vector<std::string> &closure,
		const std::vector<std::string> &direct, std::string &self ) override
	{
		std::string c;
		for ( const auto &x : closure )
			c += ( c.empty() ? "" : "," ) + x.substr( x.rfind( '/' ) + 1 );
		events.push_back( "leave:" + n.config->getName() + "[" + c + "]#" + std::to_string( direct.size() ) );
		self = n.canonDir;
		return 0;
	}
	std::string originOf( const std::string &a ) const override { return a; }
	std::vector<ConfigIssue> issues;
	static std::string readAll( const std::string &p )
	{
		std::ifstream in( p );
		if ( !in.is_open() )
			return "";
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
};

static ProjectConfig *loadOrNull( const std::string &dir )
{
	return ProjectConfig::loadFromDirectory( dir );
}

int main( int argc, char *argv[] )
{
	if ( argc < 2 )
	{
		std::fprintf( stderr, "FAIL usage: projectLayoutTest <source root>\n" );
		return 1;
	}
	const std::string src = argv[1];
	char tmpl[] = "/tmp/u3_layout_XXXXXX";
	const std::string t = mkdtemp( tmpl );

	// (a) findProjectRoot: walk-up, stop at the first, "" when none.
	{
		mkd( t + "/outer" );
		mkd( t + "/outer/inner" );
		mkd( t + "/outer/inner/deep" );
		writeFile( t + "/outer/blang.toml", "[project]\nname = \"outer\"\n" );
		writeFile( t + "/outer/inner/blang.toml", "[project]\nname = \"inner\"\n" );
		CHECK( findProjectRoot( t + "/outer/inner/deep" ) == canonicalPath( t + "/outer/inner" ) );
		CHECK( findProjectRoot( t + "/outer" ) == canonicalPath( t + "/outer" ) );
		CHECK( findProjectRoot( "/" ) == "" || findProjectRoot( "/" ) == "/" );
		mkd( t + "/none" );
		CHECK( findProjectRoot( t + "/none" ) == "" ||
			findProjectRoot( t + "/none" ).find( t ) == std::string::npos );
	}
	// (b) discoverSourceFiles: root-only, sorted.
	{
		mkd( t + "/src" );
		mkd( t + "/src/sub" );
		writeFile( t + "/src/z.b", "" );
		writeFile( t + "/src/a.b", "" );
		writeFile( t + "/src/sub/m.b", "" );
		writeFile( t + "/src/note.txt", "" );
		auto f = discoverSourceFiles( t + "/src" );
		CHECK( f.size() == 2 && f[0] == t + "/src/a.b" && f[1] == t + "/src/z.b" );
	}
	// (c) resolvePath.
	{
		CHECK( resolvePath( t + "/outer", "/abs/x" ) == "/abs/x" );
		CHECK( resolvePath( t + "/outer", "inner" ) == canonicalPath( t + "/outer/inner" ) );
		CHECK( resolvePath( t + "/outer", "../nope" ) == t + "/outer/../nope" );
	}
	// (d) locateGitCheckout + (f) resolveDependencyDir without a fetcher.
	{
		setenv( "XDG_CACHE_HOME", ( t + "/cache" ).c_str(), 1 );
		Dependency g;
		g.name = "greet";
		g.gitUrl = "file:///nonexistent/greet.git";
		GitLocation un = locateGitCheckout( g );
		CHECK( un.error == "git dependency 'greet' must pin a version: add tag = \"...\" or rev = \"...\"" );
		g.tag = "v1";
		GitLocation loc = locateGitCheckout( g );
		CHECK( loc.error.empty() && !loc.warm );
		CHECK( loc.dir == t + "/cache/blang/git/greet-" + shortHashHex( "file:///nonexistent/greet.git@v1" ) );
		CHECK( loc.dir.size() - loc.dir.rfind( '-' ) - 1 == 16 );
		DepDir cold = resolveDependencyDir( t, g, nullptr );
		CHECK( cold.error == DepDir::Error::kUnfetched && cold.dir == loc.dir );
		mkd( t + "/cache" );
		mkd( t + "/cache/blang" );
		mkd( t + "/cache/blang/git" );
		mkd( loc.dir );
		writeFile( loc.dir + "/blang.toml", "[project]\nname = \"greet\"\ntype = \"lib\"\n" );
		CHECK( locateGitCheckout( g ).warm );
		DepDir warm = resolveDependencyDir( t, g, nullptr );
		CHECK( warm.error == DepDir::Error::kNone && warm.dir == loc.dir );
		g.rev = "abc123";
		CHECK( locateGitCheckout( g ).pin == "abc123" );   // rev wins over tag
		Dependency none;
		none.name = "x";
		CHECK( resolveDependencyDir( t, none, nullptr ).message ==
			"dependency 'x' has neither a path nor a git url" );

		// analyze M2: the committed warm-cache fixture's directory name IS the
		// shared rule's name for gitdep_warm's dependency.
		setenv( "XDG_CACHE_HOME", ( src + "/test_files/lsp/projects/warm_cache" ).c_str(), 1 );
		Dependency w;
		w.name = "greet";
		w.gitUrl = "file:///nonexistent/greet.git";
		w.tag = "v1";
		GitLocation wl = locateGitCheckout( w );
		struct stat st;
		if ( stat( ( src + "/test_files/lsp/projects/warm_cache" ).c_str(), &st ) == 0 )
			CHECK( wl.warm );
		unsetenv( "XDG_CACHE_HOME" );
	}
	// (e) mergeByOrigin.
	{
		std::vector<std::string> v;
		auto id = []( const std::string &s ) { return s; };
		mergeByOrigin( v, std::string( "a" ), id );
		mergeByOrigin( v, std::string( "b" ), id );
		mergeByOrigin( v, std::string( "a" ), id );
		CHECK( v.size() == 2 );
	}
	// (g) ProjectConfig::loadFromText: a sink, no cerr, Dependency::line.
	{
		std::vector<ConfigIssue> issues;
		ProjectConfig *c = ProjectConfig::loadFromText( "x/blang.toml",
			"[project]\nname = \"p\"\nthis is not toml\n[deps]\nm = { path = \"../m\" }\nbad = \"x\"\n", &issues );
		CHECK( c != nullptr && c->getName() == "p" );
		CHECK( issues.size() == 2 && issues[0].line == 3 && issues[0].message == "expected key = value" );
		CHECK( issues[1].line == 6 && issues[1].message == "expected inline table for dependency 'bad'" );
		CHECK( c->getDependencies().size() == 1 && c->getDependencies()[0].line == 5 );
		delete c;
		ProjectConfig *f = ProjectConfig::loadFromFile( src + "/test_build/myapp/blang.toml" );
		CHECK( f != nullptr && f->getName() == "myapp" && !f->getDependencies().empty() );
		delete f;
	}
	// T005b (audit addendum H2): per-file scan, never a concatenation.
	{
		std::vector<std::string> t1 = { "import a;", "import b;\n" };
		std::vector<std::string> t2 = { "x\n// c", "import d;\n" };
		CHECK( ( BlangStdlib::scanImportsAll( t1 ) == std::set<std::string>{ "a", "b" } ) );
		CHECK( ( BlangStdlib::scanImportsAll( t2 ) == std::set<std::string>{ "d" } ) );
		// Teeth: the concatenated form really is different (so the shared per-file
		// rule matters).
		CHECK( BlangStdlib::scanImports( t1[0] + t1[1] ) != BlangStdlib::scanImportsAll( t1 ) );
		CHECK( BlangStdlib::scanImports( t2[0] + t2[1] ) != BlangStdlib::scanImportsAll( t2 ) );
	}
	// walkDependencies semantics (audit addendum H1): diamond fold + always
	// recurse, a missing dependency (2a), a cycle — continuing visitor.
	{
		std::string w = mkd( t + "/w" );
		auto proj = [&]( const std::string &n, const std::string &deps ) {
			mkd( w + "/" + n );
			writeFile( w + "/" + n + "/blang.toml", "[project]\nname = \"" + n + "\"\n[deps]\n" + deps );
		};
		proj( "leaf", "" );
		proj( "l", "leaf = { path = \"../leaf\" }\n" );
		proj( "r", "leaf = { path = \"../leaf\" }\n" );
		proj( "top", "l = { path = \"../l\" }\nr = { path = \"../r\" }\nmiss = { path = \"../nope\" }\n" );
		RecVisitor v;
		std::vector<std::string> closure;
		std::string self;
		CHECK( walkDependencies( w + "/top", nullptr, v, closure, self ) == 0 );
		std::string all;
		for ( auto &e : v.events )
			all += e + ";";
		// leaf is visited twice (always recurse), folded once (dedup by origin),
		// closure-then-self order; the missing dep is a kNoBlangToml dep error.
		CHECK( all.find( "enter:top;enter:l;enter:leaf;leave:leaf[]#0;leave:l[leaf]#1;"
			"enter:r;enter:leaf;leave:leaf[]#0;leave:r[leaf]#1;" ) == 0 );
		CHECK( all.find( "deperr:top:miss:5;" ) != std::string::npos );
		CHECK( all.find( "leave:top[leaf,l,r]#2;" ) != std::string::npos );
		CHECK( closure.size() == 3 );

		proj( "ca", "cb = { path = \"../cb\" }\n" );
		proj( "cb", "ca = { path = \"../ca\" }\n" );
		RecVisitor cv;
		closure.clear();
		CHECK( walkDependencies( w + "/ca", nullptr, cv, closure, self ) == 0 );
		bool sawCycle = false;
		for ( auto &e : cv.events )
			sawCycle = sawCycle || e == "cycle:" + canonicalPath( w + "/ca" );
		CHECK( sawCycle );

		RecVisitor sv;            // a stopping visitor aborts with the code (bcc)
		sv.continueOnError = false;
		closure.clear();
		CHECK( walkDependencies( w + "/top", nullptr, sv, closure, self ) == 1 );
		(void)loadOrNull;
	}

	std::string rm = "rm -rf '" + t + "'";
	if ( std::system( rm.c_str() ) != 0 )
		std::fprintf( stderr, "warning: could not remove %s\n", t.c_str() );

	if ( gFailures != 0 )
	{
		std::fprintf( stderr, "ProjectLayoutTest: %d FAILURE(S)\n", gFailures );
		return 1;
	}
	std::printf( "ProjectLayoutTest: OK\n" );
	return 0;
}
