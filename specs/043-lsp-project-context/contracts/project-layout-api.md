# Contract: shared project helpers (`ProjectLayout.h`) and the `ProjectConfig` sink

These helpers were moved verbatim from `bcc.cpp` (audit R3). `bcc` and blangd both call them, and neither carries its own copy. The unit is process-free: it never calls `fork`/`exec`/`system`/`popen` or touches the network.

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
