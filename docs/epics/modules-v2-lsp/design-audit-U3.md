# Design audit — U3 (`specs/043-lsp-project-context`), before specify

**Auditor**: Mira (architect). **Date**: 2026-11-26. **Constitution**: Principle VI.
**Tree read**: `042-lsp-stdlib-context` @ `dc39603` (stacked on U1 `041`).
**Inputs**: design.md D2/D6, C1–C7; workplan U3 + sequencing notes; `bcc.cpp`
(`discoverSourceFiles` :899, `resolvePath` :936, `gitCacheRoot`/`shortHashHex`/
`resolveGitDependency` :956–1057, `buildProject` :1092), `BmodEmitter.{h,cpp}`,
`ProjectCompile.{h,cpp}`, `qcc.cpp` (plan build :284, emit :345), `lsp/Compile.cpp`,
`ProjectConfig.cpp`, `BuildCache.cpp`, `QModule.cpp` `stampDefiningOrigin`.
D1–D17 are not reopened.

## Verdict: **PROCEED**, under conditions R1–R3 (binding on U3's plan)

The D2 mechanism (parse the dependency's sources, run `BmodEmitter::emit` in memory,
parse that text as the interface, and keep the source AST for navigation) **does
reach byte-for-byte `.bmod` parity**. Two conditions apply. First, blangd must
reproduce the dependency's *own* `qcc` invocation exactly (§1). Second, two
blangd-unsafe pieces of process-global state must be neutralised: the emitter's
disk line cache (R1) and global-scope namespace injection (R2). Neither condition
changes the `.bmod` format, `BuildCache` keys, D1–D17, or any `qcc`/`bcc` output.
No question is raised to the owner.

## 1. D2 interface parity

What `bcc` actually produces for a library dependency (`buildProject` :1223–1243):

```
qcc <sorted lib sources> <transitive-closure .bmods, topo order>
    --module-origin <realpath(libDir)> --bmod-origin <bmod>=<origin>...
    --emit-bmod <libDir>/<[project].name>.bmod          # NO --combine, NO stdlib
```

`qcc` emits only when the compile had no errors (`qcc.cpp` :314 returns first). It
emits every recorded non-extern module, with `ctx->bmodEmitScope()` as the emit scope.

Parity therefore holds when blangd builds **that same `CompilePlan`** and runs it
through `ProjectCompile::run`, then calls `BmodEmitter::emit` with the same module
list and scope:

- **Same plan**: non-combine; sources as in-memory `kSource` inputs in the order
  `discoverSourceFiles` returns them; `plan.moduleOrigin = realpath(libDir)`;
  `IdentityPolicy::kCanonical`.
- **Same interfaces**: each dependency's own closure interfaces, as in-memory
  `kInterface` inputs with an explicit `origin`.
- **Same interface path**: the path string is the one `bcc` would use,
  `<depDir>/<name>.bmod`. `moduleNameOf` derives the import name from the
  basename, and that name is the dependency's **`[project] name`, not its
  `[deps]` key**. A cache hit keeps that basename (`BuildCache::store` copies
  `extractFilename`).

Interfaces are derived recursively, leaves first. Within one compile, each origin
is derived once (`mergeDep` dedups by origin).

| Property | Parity? | Why |
|---|---|---|
| Privacy: `pub`-only items, opaque bodies, private `init` declared | ✅ | the emitter itself is reused |
| D7 name/use capability | ✅ | it comes from the interface's `grantsNameCapability` namespace plus the transitive closure. Both are reproduced by the same plan and the same closure order |
| D15 `table`/`@json` field metadata | ✅ | `isDataContractStruct` lives in the emitter |
| P9 filtering | ✅ | P9 is a Sema error at the library compile, so a violating library has **no** interface in `bcc` either (see R6) |
| `// foreign-type:` headers | ✅ | the digest comes from the defining module's stamped origin. The name comes from `getDefiningFile()`, the basename of the bmod path. Both are reproduced given the explicit `origin` and the path string above |
| Generic bodies (verbatim source slices) | ❌ **as is** → ✅ with **R1** | `sliceDefinitionSource` reads `loc.file` **from disk** through a process-global, never-invalidated `gSourceLineCache` |
| Format marker / format check | ✅ | the emitter writes it, and `run` probes in-memory text (`ProjectCompile.cpp` :475) |

**Definition of parity**: it is defined against a **cold** `bcc build`. The
`BuildCache` key (sources + toml + dependency hashes) does not include the
project's location. So a cached `.bmod` built from an identical tree at another
path can carry other origins' digests in its foreign-type headers. This is
pre-existing `bcc` behaviour, it is out of scope, and it does not affect the
corpus, which builds in place.

## 2. D6 identity, origin and mangling rules

| Rule | `bcc` source | blangd must | Matches? |
|---|---|---|---|
| Project discovery | **none**: `bcc build` uses cwd/argv (`runBuild`). The walk-up is blangd's own rule | nearest ancestor `blang.toml` (stop at the first). The document is in the project **only if** it is in `discoverSourceFiles(projectDir)` (non-recursive). Otherwise use U2's single-file `bcc file.b` context (R5) | ✅ if R5 |
| Source set + order | `discoverSourceFiles`: `*.b` in the root only, lexicographically `sort`ed | the same function, shared (R3) | ✅ |
| Which module is the "user" module | combine mode: the **last** source in sorted order. The others become namespaced modules | the same plan whichever file is open. Publish only diagnostics located in the open document (C5) | ✅ |
| Path deps | `resolvePath(projectDir, dep.path)`, relative to the **declaring** toml, then `realpath` (falls back to the joined path) | shared `resolvePath` | ✅ |
| Git deps | `gitCacheRoot()` = `$XDG_CACHE_HOME/blang/git` > `$HOME/.cache/blang/git` > `/tmp/...`. Dir = `<name>-<shortHashHex(url@pin)>`. Warm iff `stat(dir/blang.toml)`. The pin (rev > tag) is required | a shared **pure** locator (no `runCommand`). A cold checkout or a missing pin is a diagnostic and is never fetched (C4) | ✅ if R3 splits locate from fetch |
| Origin | dependency: `realpath(depDir)`, else `depDir`. Self: `canonDir` = `realpath(projectDir)` | the same | ✅ |
| Topological order | DFS; fold each dependency's closure, then the dependency itself; `mergeDep` dedup by origin; consumers see the **full transitive closure** | the same algorithm, shared | ✅ |
| Bin vs lib plan | bin: `--combine` + `selectStdlibFiles(stdlib, scanImports(all sources))` iff the stdlib set is non-empty. **lib: never combine, never stdlib** | the same. **U2's stdlib context must not apply to a lib project's own files** | ✅ if R4 |
| Cycles | `building` on-path set, error | the same (a located diagnostic) | ✅ |

## 3. Conditions, risks and open items U3's spec/plan must settle

**Binding conditions (PROCEED depends on these):**

- **R1 — emitter source text.** `BmodEmitter::sliceDefinitionSource` must slice from
  the **same text the AST was parsed from**: the overlay text for an open file. It
  must not use a process-lifetime disk cache. Today an unsaved edit (done condition
  (d)) would slice disk lines at overlay line numbers, producing a wrong or
  unparseable interface, and a long-running blangd would serve stale bodies
  forever. Fix: an optional text provider (or a per-call cache) passed to `emit`.
  The default path stays byte-identical for `qcc`, proven by the
  `test_files/golden/bmod/` goldens and `run_build_tests.sh`. This edits the emitter
  but **not** the format.
- **R2 — I6, global-scope injection.** `injectBmodSymbols` does
  `gScope->findNamespace(name)` / `gScope->addNamespace` on the **global** scope.
  blangd's process-lifetime `sharedResolver()` would therefore *reuse* a dependency
  namespace across reparses, documents, and the nested library compiles that R1/D2
  require. That leaks (C3), duplicates symbols, and lets one compile's interface
  leak into another. U3 must give every `ProjectCompile::run` an isolated global
  scope. The recommended way, with no `qcc` change, is a fresh `Resolver` per run,
  owned by the `CompileResult`. Lifetimes must be strictly LIFO (the `Resolver`
  ctor/dtor swaps the `gScope` alias), each context must be destroyed before its
  resolver, and the source ASTs kept for navigation (C6) must be owned the same
  way. `parser_no_leak` (f) is the lock. Any change *inside* `ProjectCompile` here
  must be proven neutral, as in U1.
- **R3 — share, don't copy (D6, "no blangd-only rule").** Move `discoverSourceFiles`,
  `resolvePath`, `gitCacheRoot`, `shortHashHex` and the closure/topological
  assembly out of `bcc.cpp` into process-free shared code (precedent: U1's
  `StdlibSelect`). `bcc` must call them, behavior-neutrally. Split git *locate*
  (shared) from *fetch* (`bcc` only). Add an error sink to
  `ProjectConfig::loadFromFile`, which today writes `parse error` lines to `cerr`.
  That would break blangd's strict framing, and the default sink keeps `bcc`'s text
  byte-identical. `blangd_no_subprocess` must stay green.

**Spec decisions (decidable: record them as UNCONFIRMED/DECISION, do not block):**

- **R4** Lib projects: the plan has no stdlib and no combine (mirrors `bcc`).
  Opening a lib's own `.b` file gives the lib's own compile.
- **R5** Walk-up plus membership. A file in a subdirectory of a project is **not**
  in the project, because `bcc` wouldn't compile it there, so it falls back to U2's
  single-file context.
- **R6** A dependency whose own compile fails (including a P9 failure) has no
  interface. `bcc` aborts the whole build, so there is no reference. Recommended:
  treat it as an *unresolvable dependency* (C5: one diagnostic at the `import`,
  else at the toml line) and compile the consumer without it. The corpus only
  covers successfully built projects, so this does not affect parity.
- **R7** Malformed `blang.toml`: `bcc` only **warns** (`cerr`), then continues with
  the partial config. Recommended: one located **Warning** at the toml line, and
  continue as `bcc` does. A hard error would be a blangd-only rule. The workplan
  (b) asks for "one located diagnostic", which this satisfies.
- **R8** Where toml-located diagnostics are published: on the `blang.toml` URI,
  or on the document at 1:1. The spec must pick one and pin it with goldens.
- **R9** Environment: blangd reads `HOME`/`XDG_CACHE_HOME` from its own process
  environment. An editor launched with a different environment can see a different
  git cache. This is acceptable, and the spec should document it.
- **R10** Latency (e): every reparse re-derives the dependency chain (D5, no
  cache). usebox needs about three small compiles, which is expected to fit within
  500 ms. If it misses, **raise before caching** (sequencing note).

## 4. Summary

PROCEED. D2 reaches exact `.bmod` parity, and D6 matches `bcc build` rule for rule,
once R1 (emitter text provider), R2 (per-run isolated global scope, closing I6) and
R3 (helpers shared with `bcc`, plus the `ProjectConfig` error sink) are in U3's plan.
All three are behavior-neutral for `qcc` and `bcc`, and none touches the `.bmod`
format, `BuildCache` keys or D1–D17. R4–R10 are spec-level decisions with
recommended defaults.

---

# Addendum — ruling on the U3 `/speckit-analyze` findings (Vera), 2026-11-26

**Status: binding on the implementer.** The rulings below edit only `plan.md`,
`tasks.md` and `contracts/`. `spec.md` is frozen. Each **Edit** block is text to
apply as written. R1–R3 above still stand; this addendum sharpens them.

## H1 (R3): the dependency-graph walk → **EXTRACT to shared code** (no blangd copy)

R3 said that *the closure/topological assembly* moves out of `bcc.cpp`. T017 as
written copies the DFS, the on-path guard (KG-8) and the fold into
`lsp/ProjectContext.cpp`. That makes a second implementation of the rule whose
drift is the bug class this epic exists to remove (cycle and diamond handling,
fold order → interface order → foreign-type pre-scan order). A blangd copy is
**rejected**.

**Shape** (names indicative; the semantics are binding):

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

1. The on-path set uses `realpath`'d `canonDir`. Membership → `onCycle`. An RAII
   erase runs on **every** exit, including the visitor's early returns (KG-8).
2. Each dependency is resolved with `resolveDependencyDir( dir, dep, fetch )`. A
   non-empty `error` or `unfetched` → `onDepError`. A non-zero return aborts the
   walk with that code; zero skips the dependency, so neither it nor anything
   reached only through it joins the closure.
3. **The walk always recurses** into a dependency, even one already seen in a
   diamond, exactly as `bcc` does today. Memoizing by origin is the **visitor's**
   choice: blangd keeps a per-compile `origin → result` map and never keeps it
   across reparses (D5). `bcc` keeps its current behavior, which is a cache hit.
4. The fold is the child's closure entries first (`mergeByOrigin`), then the
   child's `self`, in `[deps]` order.
5. `leave` is called after the dependency loop, with the folded closure.
   `sources`, the empty-sources error, stdlib selection, the cache key and the
   build all stay in `bcc`'s `leave`. `bcc.cpp` keeps no recursion of its own.
6. `bcc`'s texts stay byte-identical, and they stay in `bcc.cpp`, inside its
   visitor:
   - `error: circular dependency detected: <canonDir>`;
   - `error: no blang.toml found in <dir>`;
   - `error: dependency '<n>' has neither a path nor a git url`;
   - the pin and fetch errors.

   The shared walk prints nothing.

**Edits.**
- **plan.md D-1, add a bullet:** "`walkDependencies( projectDir, Fetcher*, WalkVisitor<A>& )`: `buildProject`'s on-path/KG-8 DFS, resolution, always-recurse and closure-then-self `mergeByOrigin` fold, lifted verbatim. `bcc`'s `buildProject` becomes a `WalkVisitor<DepArtifact>` whose `leave` holds the build body. Messages stay in the visitors."
- **plan.md D-4 step 3:** "Resolve the graph with `BlangProject::walkDependencies( root, nullptr, visitor )`. blangd's visitor turns `onDepError`/`onCycle` into setup diagnostics (D-5), returns 0 so the walk continues, and derives the interface in `leave` (D-3), memoized by origin for this compile only."
- **tasks.md:**
  - Add **T004b** after T004: "Extract `walkDependencies` + `WalkVisitor` into `ProjectLayout.{h,cpp}`. Rewrite `bcc.cpp buildProject` as a visitor with no recursion of its own. Grep gate: `grep -n 'buildProject(' bcc.cpp` shows only the definition and the `runBuild` call; `grep -nE 'building\.(insert|count|erase)' bcc.cpp` prints nothing."
  - **T007, add:** "the cycle and missing-dependency negative legs (scratch toml pair a↔b; a path dependency to a non-existent dir): `bcc build` exit code and stderr `cmp`-identical to `/tmp/u3-ref`. The diamond leg (KG-8) is `cmp`-identical under `-v`."
  - **T017, replace** the first four sub-bullets with: "implement the blangd `WalkVisitor` over `BlangProject::walkDependencies( root, nullptr, v )`. `load` = `loadFromText` from the overlay or disk with the issue sink. No DFS, on-path set or fold code in `lsp/`. Grep gate: `grep -nE 'onPath|mergeByOrigin|building' lsp/*.cpp` matches only calls to and fields of the shared API."
- **contracts/project-layout-api.md:** add the `walkDependencies` / `WalkVisitor` declaration with semantics 1–6, and add "the walk is process-free; `fetch == nullptr` ⇒ never fetches" to the process-free sentence.

## H2 (R3): per-file import scan → **shared `BlangStdlib::scanImportsAll`**, which `bcc` calls

Concatenating breaks the line model. A file that ends without a newline merges
its last line with the next file's first line: `import a;` + `import b;` reads as
one line, so `b` is dropped. A file whose last line is a `//` comment swallows the
next file's first `import`. Either way the stdlib set diverges from `bcc`, which
scans **each source separately** and unions the results (`bcc.cpp` :1225–1230).

**Edits.**
- **StdlibSelect.h/.cpp:** add `std::set<std::string> scanImportsAll( const std::vector<std::string> &texts );`, the union of `scanImports(t)` over the texts in order. `bcc.cpp buildProject` replaces its `projImports` loop with `scanImportsAll( texts-of(sources) )`. The texts are read as today with `readFileToString`, so the change is byte-neutral. It is covered by T007.
- **plan.md D-4 step 5 and tasks.md T019:** replace `scanImports( concatenated texts of all project sources )` with `BlangStdlib::scanImportsAll( per-file texts of all project sources )`, where each text is the overlay text or the disk text, and the document's own text is `text`. **Never concatenate.**
- **tasks.md, new T005b [P]** (`project_layout` ctest): two texts, `"import a;"` with no trailing newline followed by `"import b;\n"`, and `"x\n// c"` followed by `"import d;\n"`. `scanImportsAll` yields `{a,b}` and `{d}`. A teeth leg asserts that the concatenated-scan form yields a different set.
- **contracts/project-context.md "Plan":** `scanImports( all source texts )` → `scanImportsAll( per-file source texts )`.

## H3 (R1): emitter provider miss → **loud failure; never the global cache, never a disk read**

T008 as written falls back to `gSourceLineCache` when the provider returns
`nullptr`. That is the stale process-global state R1 forbids. A fresh uncached
disk read is **also rejected**: it can differ from the text that was parsed (an
unsaved buffer). And the emitter's existing "empty slice → signature-only"
fallback would then silently produce a **different interface**, a parity break
nobody sees.

**Edits.**
- **T008, replace** the last paragraph with: "**No provider** (`qcc`): today's disk-cache path, byte-identical. **With a provider**, `gSourceLineCache` is never read or written. A provider hit splits that text for this call only. A provider **miss** for a slice location is an emitter failure: `emit` records the path in `std::vector<std::string> *missing` (a new out-parameter, default `nullptr`) and emits nothing further for that definition. The caller must treat any non-empty `missing` as a failed derivation. The `qcc` call passes neither argument."
- **T018(c), replace** with: "The provider is a per-run `map<path, const std::string*>` over **every** `CompileInput` of that dependency's run: each source path and each closure-interface path (`<depDir>/<name>.bmod`), keyed exactly by `CompileInput::path` (the string `SourceLocation.file` carries). If `emit`'s `missing` is non-empty, mark the dependency `compile-failed` with `internal error: no source text for '<path>' while deriving the interface of '<name>'` (published per D-5, R6). It is never a crash or a silent fallback."
- **T008/T009 test, add:** a `project_layout` or `project_compile` case where the provider deliberately omits one generic-defining source. It asserts `missing == {that path}` and that `gSourceLineCache` is untouched: a pre-seeded different disk text for the same path is **not** what was emitted.
- **plan.md D-3(c) and contracts/project-context.md "Overlay":** add "A provider miss is a failed derivation (internal-error diagnostic), never a fallback."

## M1: SC-007 counted as "177 + 2" → **ACCEPT, with an explicit 179 accounting**

The spec (frozen) says the U2 stdlib corpus stays 179/179. Two members,
`test_build/myapp/main.b` and `counterapp/main.b`, legitimately change context:
they are project members with `[deps]`. Their correct reference is now
`bcc build -v`, the stricter one, so they move to the project corpus. That meets
SC-007's intent (no regression). Each place the number appears must still account
for all 179.

**Edit**, in tasks.md T024 and T043 and in plan.md's SC-007 row: "SC-007 evidence = stdlib corpus **177/177** + `myapp` and `counterapp` **PASS in the project corpus** = **179/179 accounted**. Name the two moved entries in `checklists/corpus-run.md`." A file that drops out of both sets is a STOP.

## M2–M6, L1

The findings' text was not in this hand-off. I have not seen M2–M6/L1, so I do
not rule on their content. **Standing rule:**

- **Required before `/speckit-implement`:** any M finding that touches R1–R3,
  C1–C7, FR-021 (neutrality), the no-subprocess guarantee, or KG-7 ownership.
  Fix it in `plan.md`/`tasks.md` first.
- **Otherwise:** fix it in the same pre-implement edit pass if it is a pure
  wording/traceability fix; else carry it as a `DECISION:` line and let converge
  catch it.
- **L1:** at the implementer's discretion.

If the manager forwards the M/L text, I will rule on each item.
