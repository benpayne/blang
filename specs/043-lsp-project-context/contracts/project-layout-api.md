# Contract: shared project helpers (`ProjectLayout.h`) and the `ProjectConfig` sink

These helpers were moved verbatim from `bcc.cpp` (audit R3). `bcc` and blangd both call them, and neither carries its own copy. The unit is process-free: it never calls `fork`/`exec`/`system`/`popen` or touches the network. The walk is process-free; `fetch == nullptr` ⇒ never fetches.

```cpp
namespace BlangProject {
	std::vector<std::string> discoverSourceFiles( const std::string &dir );   // root *.b, sorted
	std::string resolvePath( const std::string &base, const std::string &rel ); // abs passthrough; realpath else joined
	std::string gitCacheRoot();                    // $XDG_CACHE_HOME/blang/git > $HOME/.cache/blang/git > /tmp/.cache/blang/git
	std::string shortHashHex( const std::string &s );   // first 8 bytes of SHA-256, hex

	struct GitLocation { std::string dir; std::string error; bool warm = false; };
	GitLocation locateGitCheckout( const Dependency &dep );
	//   error = bcc's unpinned text when neither rev nor tag;
	//   dir   = gitCacheRoot() + "/" + name + "-" + shortHashHex(url + "@" + (rev ?: tag));
	//   warm  = stat(dir + "/blang.toml") == 0

	struct DepDir { std::string dir, origin, error; bool unfetched = false; };
	using Fetcher = std::function<int( const Dependency &, const GitLocation &, std::string &outDir )>;
	DepDir resolveDependencyDir( const std::string &declaringDir, const Dependency &dep, Fetcher *fetch );
	//   fetch == nullptr  → a cold git dep returns unfetched = true (blangd: never fetches)
	//   bcc passes its fetcher (runCommand git clone …) — the only process path, and it stays in bcc.cpp

	std::string findProjectRoot( const std::string &startDir );   // nearest ancestor with blang.toml, "" if none
	template <class T, class OriginOf> void mergeByOrigin( std::vector<T> &into, const T &d, OriginOf originOf );
}
```

`findProjectRoot` is the only helper that is not a `bcc` move. It is blangd's walk-up rule (audit §2: `bcc` has none), and it lives here so the rule has one home.

## `walkDependencies` (audit addendum H1, binding)

```cpp
namespace BlangProject {
  struct WalkNode {                      // one visit of one project dir
    std::string dir, canonDir;           // canonDir = realpath(dir), else dir (bcc's rule)
    const ProjectConfig *config;         // loaded by the walk via the ConfigLoader
    const std::vector<std::string> *onPath;   // DFS stack of canonDirs (for a cycle message)
  };
  template <class A> struct WalkVisitor {          // A = bcc DepArtifact | blangd interface record
    virtual ProjectConfig *load( const std::string &dir ) = 0;            // bcc: loadFromDirectory; blangd: overlay+sink
    virtual int  enter( const WalkNode & ) { return 0; }                  // pre-order (bcc: "--- Building" print)
    virtual int  onDepError( const WalkNode &, const Dependency &, const DepDir & ) = 0;   // bcc: print+return 1; blangd: record unresolvable, return 0 (skip)
    virtual int  onCycle( const WalkNode & ) = 0;                          // bcc: its exact text, return 1
    virtual int  leave( const WalkNode &, const std::vector<A> &closure, A &self ) = 0;    // post-order: bcc builds/cache-hits here
    virtual std::string originOf( const A & ) const = 0;
  };
  template <class A>
  int walkDependencies( const std::string &projectDir, Fetcher *fetch, WalkVisitor<A> &v,
                        std::vector<A> &outClosure, A &outSelf );
}
```

**Binding semantics**, each a verbatim lift of `buildProject` :1092–1201:

1. The on-path set uses `realpath`'d `canonDir`. Membership → `onCycle`. An RAII erase runs on **every** exit, including the visitor's early returns (KG-8).
2. Each dependency is resolved with `resolveDependencyDir( dir, dep, fetch )`. A non-empty `error` or `unfetched` → `onDepError`. A non-zero return aborts the walk with that code; zero skips the dependency, so neither it nor anything reached only through it joins the closure.
3. **The walk always recurses** into a dependency, even one already seen in a diamond, exactly as `bcc` does today. Memoizing by origin is the **visitor's** choice: blangd keeps a per-compile `origin → result` map and never keeps it across reparses (D5). `bcc` keeps its current behavior, which is a cache hit.
4. The fold is the child's closure entries first (`mergeByOrigin`), then the child's `self`, in `[deps]` order.
5. `leave` is called after the dependency loop, with the folded closure. `sources`, the empty-sources error, stdlib selection, the cache key and the build all stay in `bcc`'s `leave`. `bcc.cpp` keeps no recursion of its own.
6. `bcc`'s texts stay byte-identical, and they stay in `bcc.cpp`, inside its visitor: `error: circular dependency detected: <canonDir>`; `error: no blang.toml found in <dir>`; `error: dependency '<n>' has neither a path nor a git url`; the pin and fetch errors. The shared walk prints nothing.

## `BlangStdlib::scanImportsAll` (audit addendum H2)

`std::set<std::string> scanImportsAll( const std::vector<std::string> &texts );` (in `StdlibSelect.h/.cpp`): the union of `scanImports(t)` over the texts in order. `bcc`'s `buildProject` calls it in place of its `projImports` loop. Texts are never concatenated.

## `ProjectConfig`

```cpp
struct ConfigIssue { int line; std::string message; };
static ProjectConfig *loadFromText( const std::string &path, const std::string &text,
                                    std::vector<ConfigIssue> *issues );
static ProjectConfig *loadFromFile( const std::string &path );   // unchanged output
struct Dependency { …; int line = 0; };                           // new: toml line of the entry
```

- With `issues == nullptr`, the existing `cerr` lines are printed byte-identically, as `<path>:<line>: parse error: <message>`.
- With a sink, nothing is printed, and each issue is appended with the same message body.

## Neutrality obligations

- `test_build/run_build_tests.sh` passes in full, including the git-dependency leg, the diamond leg and the `.bmod` goldens.
- `bcc build -v` output for all 10 binaries is `cmp`-identical before and after.
- The missing-pin and missing-dependency error texts are unchanged.
