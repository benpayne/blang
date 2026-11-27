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
