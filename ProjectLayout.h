#ifndef BLANG_PROJECT_LAYOUT_H_
#define BLANG_PROJECT_LAYOUT_H_

// modules-v2-lsp U3 (audit R3 + addendum H1): bcc's project rules, shared.
//
// Source discovery, dependency path resolution, the git-checkout location and
// the transitive dependency walk (cycle guard, always-recurse, closure-then-self
// fold) used to live only in bcc.cpp. They moved here VERBATIM so bcc and blangd
// run one copy of each rule (spec FR-007; design D6). bcc calls everything here;
// blangd calls it with no fetcher.
//
// Process-free: nothing here forks, execs, or touches the network. The walk is
// process-free; `fetch == nullptr` => it never fetches (a cold git dependency is
// reported, not cloned). The only fetch path is bcc's Fetcher, in bcc.cpp.
// Nothing here prints: every message is the visitor's.

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ProjectConfig.h"

namespace BlangProject
{

// Every .b file directly in `dir` (not recursive), lexicographically sorted —
// bcc's project source set and order.
std::vector<std::string> discoverSourceFiles( const std::string &dir );

// A dependency path relative to the DECLARING blang.toml's directory: absolute
// paths pass through; otherwise base/rel, realpath'd when it exists.
std::string resolvePath( const std::string &basePath, const std::string &relPath );

// $XDG_CACHE_HOME/blang/git > $HOME/.cache/blang/git > /tmp/.cache/blang/git.
std::string gitCacheRoot();

// First 8 bytes of SHA-256(s), lowercase hex.
std::string shortHashHex( const std::string &s );

// Where a git dependency's checkout lives, and whether it is already fetched.
struct GitLocation
{
	std::string dir;
	std::string pin;
	std::string error;   // "" or bcc's unpinned text (without the "error: " prefix)
	bool warm = false;   // stat(dir/blang.toml) == 0
};
GitLocation locateGitCheckout( const Dependency &dep );

// The resolved directory of one [deps] entry.
struct DepDir
{
	enum class Error { kNone, kNoPathOrGit, kUnpinned, kUnfetched, kFetchFailed, kNoBlangToml };
	std::string dir;
	std::string origin;         // realpath(dir), else dir (bcc's canonical origin)
	Error error = Error::kNone;
	std::string message;        // bcc's text for kNoPathOrGit / kUnpinned ("" otherwise)
	GitLocation git;            // for a git dependency
};

// A git fetcher (bcc only): given a pinned dependency and its location, make the
// checkout exist (or reuse a warm one) and return its dir; non-zero = failed
// (the fetcher prints its own messages).
using Fetcher = std::function<int( const Dependency &, const GitLocation &, std::string &outDir )>;

// Resolve one [deps] entry declared in `declaringDir`'s blang.toml. A git dep goes
// through `fetch` when one is given (bcc), else is located only: a cold checkout is
// kUnfetched, never cloned (blangd).
DepDir resolveDependencyDir( const std::string &declaringDir, const Dependency &dep,
	const Fetcher *fetch );

// The nearest ancestor of `startDir` (inclusive) holding a blang.toml; "" if none.
// blangd's walk-up rule (bcc has none — it builds the given directory).
std::string findProjectRoot( const std::string &startDir );

// realpath(p), else p.
std::string canonicalPath( const std::string &p );

// Merge into a closure, deduped by canonical origin (bcc's mergeDep).
template <class T, class OriginOf>
void mergeByOrigin( std::vector<T> &into, const T &d, OriginOf originOf )
{
	for ( const auto &e : into )
		if ( originOf( e ) == originOf( d ) )
			return;
	into.push_back( d );
}

// --- The dependency walk (audit addendum H1) -----------------------------

struct WalkNode
{
	std::string dir;                          // as reached (root arg, or the resolved dep dir)
	std::string canonDir;                     // realpath(dir), else dir
	const ProjectConfig *config = nullptr;    // loaded by the walk via WalkVisitor::load
	const std::vector<std::string> *onPath = nullptr;   // DFS stack of canonDirs
};

template <class A>
struct WalkVisitor
{
	virtual ~WalkVisitor() {}
	// Load `dir`'s blang.toml (bcc: loadFromDirectory; blangd: overlay + issue sink).
	// The walk owns and deletes the result. nullptr = no readable blang.toml.
	virtual ProjectConfig *load( const std::string &dir ) = 0;
	// The ROOT has no readable blang.toml (a dependency's is onDepError kNoBlangToml).
	virtual int onRootLoadError( const std::string &dir ) = 0;
	// Pre-order, after the node's config loaded (bcc: the "--- Building" trace).
	virtual int enter( const WalkNode & ) { return 0; }
	// A dependency of `parent` cannot be resolved. Non-zero aborts the walk with
	// that code; zero skips the dependency (it and anything reached only through
	// it stay out of the closure).
	virtual int onDepError( const WalkNode &parent, const Dependency &dep, const DepDir &dd ) = 0;
	// `node` is already on the DFS path. Non-zero aborts; zero skips the edge.
	virtual int onCycle( const WalkNode &node ) = 0;
	// Post-order, after the dependency loop: `closure` = the folded transitive
	// closure (deps first), `direct` = the node's direct deps' selves in [deps]
	// order. Fill `self`. Non-zero aborts.
	virtual int leave( const WalkNode &node, const std::vector<A> &closure,
		const std::vector<A> &direct, A &self ) = 0;
	virtual std::string originOf( const A & ) const = 0;
};

namespace detail
{

// One visit. `status` = 0 ok, else the abort code. `included` = false when the
// node was skipped (an error the visitor chose to continue past).
template <class A>
int visit( const std::string &dir, const WalkNode *parent, const Dependency *viaDep,
	const DepDir *viaDir, const Fetcher *fetch, WalkVisitor<A> &v,
	std::vector<std::string> &onPath, std::set<std::string> &onPathSet,
	std::vector<A> &outClosure, A &outSelf, bool &included )
{
	included = false;
	WalkNode node;
	node.dir = dir;
	node.canonDir = canonicalPath( dir );
	node.onPath = &onPath;

	// 1. Cycle: membership in the realpath'd on-path set (bcc's `building`).
	if ( onPathSet.count( node.canonDir ) )
		return v.onCycle( node );
	onPathSet.insert( node.canonDir );
	onPath.push_back( node.canonDir );
	// RAII erase on EVERY exit, including the visitor's early returns (KG-8).
	struct OnPathGuard
	{
		std::set<std::string> &mSet;
		std::vector<std::string> &mStack;
		const std::string mKey;
		~OnPathGuard() { mSet.erase( mKey ); mStack.pop_back(); }
	} guard{ onPathSet, onPath, node.canonDir };

	// 2a. Load; a dependency without a readable blang.toml is that dependency's
	// error, reported against its parent (after the cycle check, as bcc's order).
	std::unique_ptr<ProjectConfig> config( v.load( dir ) );
	if ( !config )
	{
		if ( parent == nullptr )
			return v.onRootLoadError( dir );
		DepDir dd = *viaDir;
		dd.error = DepDir::Error::kNoBlangToml;
		return v.onDepError( *parent, *viaDep, dd );
	}
	node.config = config.get();

	int ret = v.enter( node );
	if ( ret != 0 )
		return ret;

	// 2-4. Each dependency in [deps] order: resolve, always recurse, fold the
	// child's closure first and then the child itself, deduped by origin.
	std::vector<A> direct;
	for ( const auto &dep : config->getDependencies() )
	{
		DepDir dd = resolveDependencyDir( dir, dep, fetch );
		if ( dd.error != DepDir::Error::kNone )
		{
			ret = v.onDepError( node, dep, dd );
			if ( ret != 0 )
				return ret;
			continue;
		}
		std::vector<A> childClosure;
		A childSelf{};
		bool childIncluded = false;
		ret = visit( dd.dir, &node, &dep, &dd, fetch, v, onPath, onPathSet,
			childClosure, childSelf, childIncluded );
		if ( ret != 0 )
			return ret;
		if ( !childIncluded )
			continue;
		auto originOf = [&v]( const A &a ) { return v.originOf( a ); };
		for ( const auto &d : childClosure )
			mergeByOrigin( outClosure, d, originOf );
		mergeByOrigin( outClosure, childSelf, originOf );
		direct.push_back( childSelf );
	}

	// 5. Post-order.
	ret = v.leave( node, outClosure, direct, outSelf );
	if ( ret != 0 )
		return ret;
	included = true;
	return 0;
}

} // namespace detail

// Walk `projectDir`'s dependency graph exactly as bcc's buildProject does (audit
// addendum H1, semantics 1-6 in specs/043-lsp-project-context/contracts/
// project-layout-api.md). Returns 0, or the first non-zero visitor code.
template <class A>
int walkDependencies( const std::string &projectDir, const Fetcher *fetch, WalkVisitor<A> &v,
	std::vector<A> &outClosure, A &outSelf )
{
	std::vector<std::string> onPath;
	std::set<std::string> onPathSet;
	bool included = false;
	return detail::visit<A>( projectDir, nullptr, nullptr, nullptr, fetch, v, onPath, onPathSet,
		outClosure, outSelf, included );
}

} // namespace BlangProject

#endif // BLANG_PROJECT_LAYOUT_H_
