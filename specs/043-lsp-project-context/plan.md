# Implementation Plan: blangd project context — blang.toml, sibling sources, path/git dependencies from source

**Branch**: `043-lsp-project-context` (stacked on U2 `042-lsp-stdlib-context` @ `dc39603`, PR #159)

**Date**: 2026-11-26

**Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/043-lsp-project-context/spec.md` (`285b362`)

**Epic / unit**: `docs/epics/modules-v2-lsp/` U3

**Covers**: REQ-004, REQ-005, REQ-006, REQ-009 (project half)

**Binding**:
- the design audit [`design-audit-U3.md`](../../docs/epics/modules-v2-lsp/design-audit-U3.md) (`057edb6`), verdict PROCEED. Its R1–R3 bind this plan;
- design.md D2/D6, C1–C7;
- D1–D17, not reopened.

---

## ⚠ Constraints that govern every task

1. **No blangd-only rule.** Each project rule blangd applies comes from code that `bcc` also calls (D-1):
   - source selection, path resolution, git cache location, the closure order, origins and `blang.toml` parsing;
   - module routing, prelude, `.bmod` injection, import diagnostics and Sema, which run through U1's unchanged `ProjectCompile::run`;
   - interfaces, which come from the same `BmodEmitter::emit` that writes `.bmod` files.

   Anything that seems to need a blangd-only rule is a STOP-and-ask.
2. **No process, no network.** blangd only `stat`s and reads files. Git dependencies are *located* in the cache and never fetched. `blangd_no_subprocess` (U1) keeps passing, and the git-shim test (D-8) proves it end to end.
3. **`qcc`/`bcc` frozen.** Every shared-code move and the emitter change are proven byte-neutral. The proof is `run_build_tests.sh` (all legs, including the git dependency and the `.bmod` goldens), a `bcc build -v` before/after `cmp` on all 10 binaries, and U1's `--emit-bmod` sweep. There are no `qcc`/`bcc` golden edits.
4. **KG-7 ownership.** A document's result owns every run it made. Each run is a resolver, a context and its ASTs. The result releases them in an order that never leaves a live reference into freed state (D-4).
5. **One setup diagnostic per problem, never a crash.** This covers malformed or hostile input, and blangd never writes to stderr.

## Summary

blangd gives each member document of a `blang.toml` project the context `bcc build` gives it:

1. walk up to the nearest `blang.toml`;
2. check membership (the document is a root `.b` file);
3. resolve `[deps]` recursively and transitively, using path dependencies and **already-fetched** git checkouts;
4. derive each dependency's **interface from source**: compile it with its own `bcc` plan, emit its `.bmod` text in memory with the shared emitter (fed the parsed text: buffers first), and keep its source AST for U4;
5. compile the document with the plan of `bcc build`'s final `qcc` line, through `ProjectCompile::run`.

Every run gets its own resolver, so compiles are isolated. Unresolvable setups yield one located diagnostic, with no cascade.

The work follows audit R1–R3:
- **R1:** the emitter takes a source-text provider;
- **R2:** a manual-alias `Resolver` per run;
- **R3:** `bcc`'s project helpers move to a shared `ProjectLayout` unit, and `ProjectConfig` gains an issue sink.

Tests:
- a `--corpus project` mode in `lsp_diag_corpus.py`, covering the 10 binaries, the 6 examples, a new multi-file fixture and the 11 synthesized `fail/xmodule` layouts;
- four negative goldens plus an unsaved-dependency golden;
- a git-shim leg;
- `usebox` latency;
- a 50-reparse project loop in `parser_no_leak`.

## Technical Context

**Language/Version**: C++17 (house style); Python 3 stdlib for the tools.

**Primary Dependencies**: none new. The new shared unit uses POSIX `opendir`/`realpath`/`stat`/`getenv` plus `sha256.c`, which is already linked through `blang_sha256`. `ProjectConfig.cpp` joins the frontend sources.

**Storage**: none. Files are read on each reparse, and nothing persists across reparses.

**Testing**:
- `test_lsp.sh`: goldens, the stdlib corpus, the project corpus (when an LLVM toolchain is present), the git-shim leg and latency;
- CI `build-system` (project corpus, with LLVM);
- `ctest` (`parser_no_leak` plus `project_layout`, new);
- every pre-existing gate.

**Target Platform**: Linux (CI) and macOS (POSIX).

**Project Type**: compiler + language server.

**Performance Goals**: median ≤ 500 ms on `test_build/usebox/main.b` (estimated well under; R11).

**Constraints**: the five above.

**Scale/Scope**:
- new `ProjectLayout.{h,cpp}` (~200 lines moved from `bcc.cpp`) and `lsp/ProjectContext.{h,cpp}` (~400 lines);
- edits to `bcc.cpp`, `ProjectConfig.*`, `BmodEmitter.*`, `Resolver.*`, `lsp/Compile.*` and `lsp/Server.*`;
- tools: `lsp_diag_corpus.py` (+project mode), `lsp_client.py` (+`--server-env`);
- about 7 new fixture projects and 5 goldens.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (below).*

| Principle / Gate | Status | How |
|---|---|---|
| I. One Right Way | PASS | One copy of each project rule, shared with `bcc`. Interfaces come from the one emitter. |
| II. Test-Gated | PASS | Every SC maps to a committed test (see the § "Success-criteria → test" table). Existing gates are green with zero `qcc`/`bcc` golden edits. |
| III. Reject, Don't Coerce | PASS | Broken setups are located diagnostics. A missing dependency is never silently substituted. |
| IV. Memory Safety Verified | PASS | Ownership order is explicit (D-4). There is no cross-reparse reuse. A 50-reparse LSan loop over a transitive project, plus an ASan run of the project corpus. |
| V. House Style | PASS | |
| VI. Principled Design | PASS | This plan instantiates the architect's design audit (PROCEED, R1–R3 folded into D-1, D-3 and D-2/D-4). Security review: untrusted toml, paths and git checkouts (Gate 7, D-9). |
| Gate 1 both build configs | PASS | All new code is frontend/LSP-only, with no LLVM. The project corpus needs `llc` for its *reference*, so it runs in CI's LLVM `build-system` job (research R2). Goldens and the shim leg run in both modes. |
| Gate 5 docs | PASS | `CLAUDE.md` gets entries. `editors/README.md` gets a project-context section and the git cache note (R9). |
| Gate 7 security | PASS (review planned) | Read-only file access, `bcc`'s exact path resolution, no execution, located diagnostics on malformed input. |

There are no violations.

## Project Structure

### Documentation

```text
specs/043-lsp-project-context/
├── spec.md · plan.md · research.md (R1–R12) · data-model.md · quickstart.md
├── contracts/
│   ├── project-layout-api.md      # shared helpers moved from bcc (ProjectLayout.h) + ProjectConfig sink
│   ├── project-context.md         # blangd behavior: discovery, membership, plan, setup diagnostics, overlay
│   └── lsp-test-tools-u3.md       # --corpus project, xmodule synthesis, --server-env, shim leg
└── checklists/requirements.md
```

### Source code

```text
ProjectLayout.h/.cpp        NEW   discoverSourceFiles, resolvePath, gitCacheRoot, shortHashHex,
                                  locateGitCheckout, resolveDependencyDir, mergeByOrigin  (moved verbatim from bcc)
bcc.cpp                     EDIT  calls ProjectLayout; resolveGitDependency = locate (shared) + fetch (local)
ProjectConfig.h/.cpp        EDIT  loadFromText(path, text, issues*) + Dependency::line; loadFromFile unchanged output
BmodEmitter.h/.cpp          EDIT  emit(..., const SourceTextProvider *text = nullptr) — R1
Resolver.h/.cpp             EDIT  Resolver(AliasMode::kManual) — R2 (default ctor unchanged)
lsp/ProjectContext.h/.cpp   NEW   ProjectLocator (walk-up, membership) + DependencyGraph + interface derivation
lsp/Compile.h/.cpp          EDIT  compileDocument(path, text, stdlib, overlay): per-run resolver; project branch
lsp/Server.cpp              EDIT  passes the DocumentStore overlay (path → text)
lsp/DocumentStore.h         EDIT  path-keyed lookup for the overlay
ParserLeakTest.cpp          EDIT  + project-context loop (≥ 50 reparses, usebox)
ProjectLayoutTest.cpp       NEW   ctest project_layout (walk-up, resolvePath, git locate, mergeByOrigin)
CMakeLists.txt              EDIT  ProjectLayout.cpp + ProjectConfig.cpp in frontend; bcc links them; new ctest
tools/lsp_diag_corpus.py    EDIT  --corpus project (+ xmodule synthesis); stdlib qualifying excludes project members
tools/lsp_client.py         EDIT  --server-env NAME=VALUE
test_files/lsp/corpus.txt   EDIT  `project` entries; test_build/{myapp,counterapp} leave `stdlib`
test_files/lsp/projects/    NEW   multifile/, missing_path/, gitdep_unfetched/, empty_cache/.keep, malformed_toml/,
                                  cycle_a/ + cycle_b/, unsaved_dep/{app,lib}/
test_files/lsp/project_*.lsp.jsonl + .expected.out   NEW goldens
test_lsp.sh                 EDIT  project-corpus leg (LLVM-gated, CI-strict), git-shim leg, usebox latency
.github/workflows/ci.yml    EDIT  build-system job: + project corpus step
CLAUDE.md, editors/README.md EDIT docs
```

## Design

### D-1. Shared project helpers (audit R3; FR-007)

`ProjectLayout.h`, in `namespace BlangProject`, holds:
- `discoverSourceFiles( dir )`;
- `resolvePath( base, rel )`;
- `gitCacheRoot()`;
- `shortHashHex( s )`. All four are moved verbatim.
- `struct GitLocation { std::string dir; std::string error; bool warm; }` and `GitLocation locateGitCheckout( const Dependency & )`. This covers the pin check (`bcc`'s exact error text), `<root>/<name>-<shortHashHex(url@pin)>`, and the warm test `stat( dir/blang.toml )`;
- `struct DepDir { std::string dir, origin, error; }` and `DepDir resolveDependencyDir( projectDir, dep, bool allowFetch, Fetcher * )`:
  - `bcc` passes its fetcher;
  - blangd passes none, so a cold git dependency reports `not fetched`;
  - `origin = realpath(dir)`, else `dir`;
  - `bcc`'s "neither a path nor a git url" text is kept.
- `mergeByOrigin( std::vector<T> &, const T &, origin-of )`, which is `mergeDep` made generic.

`bcc.cpp` deletes those bodies and calls them. `resolveGitDependency` keeps only the fetch, which is the only `runCommand` path, and runs it after `locateGitCheckout` says the checkout is cold.

**Neutrality proof** (Verification §2) uses `bcc build -v` byte-identity on all 10 binaries, `run_build_tests.sh` (including the git leg and the KG-8 diamond leg), and the error texts for a missing pin and a missing dependency.

`ProjectConfig` changes:
- `loadFromText( path, text, std::vector<ConfigIssue> *issues )`, where `issues == nullptr` means the current `cerr` lines, byte-identical;
- `loadFromFile` reads the file and calls it;
- `Dependency::line`.

### D-2. Isolation: one resolver per run (audit R2; FR-013)

- **Manual-alias resolver.** `Resolver( AliasMode::kManual )` does not touch `gScope`. The default constructor is unchanged, so `qcc` is byte-identical.
- **Alias guard.** `lsp::GlobalAliasScope g( resolver )` sets `gScope = resolver.globalScope()` and restores the previous value in its destructor. It wraps exactly one `ProjectCompile::run` call (U1 I3: the caller installs the alias).
- **Every run is a `Run`.** Each blangd compile, including the U2 stdlib and single-file paths, is a `Run { shared_ptr<Resolver> resolver; shared_ptr<CompileContext> ctx; }`.
- **Effects.**
  - Dependency namespaces from `.bmod` injection land on that run's own global scope. This closes U1 I6.
  - Nothing crosses runs, documents or reparses.
  - U2's `sharedResolver()` is removed, superseding U2's analyze F2 by audit R2.
- **Grep check.** `gScope` is read by no post-compile code: `AstLocator`, `Server` and `Sema` contain no reads.

### D-3. Interfaces from source (D2, audit R1; FR-005/FR-011)

For each dependency, leaves first, in `bcc`'s fold order (each dependency's closure, then itself, deduplicated by origin):

- **(a) Plan.** It mirrors `bcc`'s lib-branch `qcc` line:
  - non-combine;
  - `kCanonical`;
  - `moduleOrigin = origin`;
  - the dependency's `discoverSourceFiles` as **in-memory** `kSource` inputs. The text is the overlay buffer if the file is open, else the disk read;
  - its closure's interfaces as in-memory `kInterface` inputs: path `<depDir>/<name>.bmod`, `origin` set, text from (c).
- **(b) Run.** `ProjectCompile::run` with a fresh `Run` (D-2). If `ctx->hadError() || diag.hasErrors()`, the dependency is **unresolvable (compile failed)** (R6). Its first error is kept for the message.
- **(c) Emit.** `BmodEmitter::emit( recorded non-extern modules, os, ctx->bmodEmitScope(), &provider )`, where `provider(path)` returns the exact text that module was parsed from. The `ostringstream` is the interface text.
- **(d) Keep.** The `Run` is kept in the result, because its source AST is the navigation target for U4 (C6).

**Parity argument.** The audit's §1 table holds: the same plan, the same emitter, the same closure order, the same interface path and the same origin. Interface parity is checked directly by an equality test: for every `test_build` library, the blangd-derived interface text must equal the `.bmod` that `run_build_tests.sh` builds in place (cold), byte for byte. The test is a `ProjectLayoutTest`/corpus helper.

### D-4. The document compile and ownership (FR-003, FR-014)

`compileDocument( path, text, stdlib, overlay )`:

1. **Locate.**
   - `BlangProject::findProjectRoot( dirname(path) )` walks up and stops at the first `blang.toml`.
   - With no project, or when the document is not a member (`path ∉ discoverSourceFiles(root)`, compared by `realpath`; R5), the U2 path is used unchanged.
2. **Load** `blang.toml` with `loadFromText`, from the overlay buffer if it is open, collecting issues. Each issue becomes one `blang.toml` Warning (R7/R8, D-5).
3. **Resolve the graph.** Run DFS over `[deps]` with an on-path set (cycle → one setup diagnostic) and the `bcc` fold.
   - Resolution uses `resolveDependencyDir( …, allowFetch = false )`.
   - Each failure (missing path, unfetched, unpinned, failed compile, cycle) marks that dependency **unresolvable**, records one setup diagnostic, and removes it and everything only it reached from the closure.
4. **Derive interfaces** per D-3.
5. **Plan.**
   - **Binary:** combine; `selectStdlibFiles( stdlib.path, scanImports( all project sources' texts ) )` (iff non-empty, as `bcc`); the sources (in-memory); the resolved closure interfaces; `moduleOrigin = realpath(root)`; `kCanonical`.
   - **Library** (R4): non-combine, no stdlib.
   - **Unusable stdlib** (U2) for a binary: the plan omits the stdlib, and U2's warning still leads.
6. **Run** `ProjectCompile::run` (D-2) and keep it.
7. **Publish set** = setup and toml diagnostics, then the run's diagnostics located in `path`, minus the suppression in D-5.

The ownership layout is shown below. Members are destroyed in **reverse** order, so `module`/`fileScope` go first, then `deps`, then `context`, then `resolver`:

```text
struct CompileResult {
    std::shared_ptr<QLang::Resolver>       resolver;   // FIRST  → destroyed LAST (global scope outlives everything)
    std::shared_ptr<QLang::CompileContext> context;    // the document's run (stdlib + interfaces + sources)
    std::vector<std::shared_ptr<Run>>      deps;       // each dep's run (its own resolver+context; source AST for U4)
    SmartPtr<Module> module;  SmartPtr<Scope> fileScope;
    std::vector<Diagnostic> diagnostics;
};
```

- **Why this order is safe.** The user AST's non-owning links point into modules owned by `context`: interfaces parsed from the emitted text, plus the stdlib. They never point into a dependency's `Run`, which is a separate compile whose text was re-parsed. So `deps` may die before or after `context`.
- **Each `Run`** declares its resolver before its context, and so destroys the context first.
- **`Server`** still replaces a result with erase plus emplace (U2 L1).
- **No reuse across reparses.** Every reparse rebuilds every `Run` (D5).

### D-5. Setup diagnostics (FR-009, FR-016; spec R6–R8; research R7)

| Problem | Severity | Message (fixed form) |
|---|---|---|
| missing path dependency | Error | `dependency '<key>' cannot be resolved: '<resolved path>' has no blang.toml (declared at <toml>:<line>)` |
| unfetched git dependency | Error | `git dependency '<key>' is not fetched (expected a checkout at '<dir>'); run 'bcc build' to fetch it` |
| unpinned git dependency | Error | `bcc`'s exact text: `git dependency '<key>' must pin a version: add tag = "..." or rev = "..."` |
| dependency compile failed | Error | `dependency '<name>' failed to compile: <file>:<line>:<col>: <first error>` |
| cycle | Error | `circular dependency: <a> -> <b> -> <a>` |
| malformed `blang.toml` line | Warning | `<toml>:<line>: parse error: …`, the same body `bcc` prints |

**Placement.**
- An Error is placed at the document's first `ImportStatement` whose module name is the dependency's import name: its `[project] name` if readable, else the `[deps]` key.
- Otherwise it is placed at 1:1 with ` (declared at <toml>:<line>)` appended.
- Warnings are placed at 1:1 (R8).

**Suppression.** Run diagnostics whose message contains `'<name>'` or `'<name>.` for an unresolved module name are dropped (research R7, UNCONFIRMED). Each negative golden also carries an unrelated real error, which proves the filter is narrow.

**Robustness.** Unreadable files, symlink loops (handled by `realpath`), and huge or garbage toml or sources go through the same paths, and never crash or write to stderr. The issue sink plus the U2 diagnostics filter guarantee that.

### D-6. Overlay (FR-011)

`DocumentStore` gains `const std::string *textForPath( realPath )`, keyed by `realpath(uriToPath(uri))` at open/change time.

`compileDocument` reads **every** project, dependency and `blang.toml` file through `overlay.textForPath(p) ?: readFile(p)`. The same string is used both as the in-memory input and through the D-3(c) provider, so the verbatim generic bodies come from the buffer (R1).

The stdlib keeps U2's disk reads, which is unchanged behavior: the stdlib is not project source.

### D-7. Tests and tools (FR-015 to FR-020)

**Corpus tool, project mode.** `lsp_diag_corpus.py --corpus project` (contract [lsp-test-tools-u3.md](contracts/lsp-test-tools-u3.md)) covers:
- **(i) Project entries** (`project <dir>`): the 10 `test_build` binaries, the 6 `examples`, and `test_files/lsp/projects/multifile`.
  - The tool copies `test_build/`, `examples/` and the fixture into a scratch tree and sets `XDG_CACHE_HOME` to scratch.
  - It runs `bcc build -v <abs dir>`, takes the last `/qcc ` line, and runs it from `<dir>` with `--json`.
  - blangd opens the scratch `main.b` with the same `XDG_CACHE_HOME`.
  - It compares sorted multisets, as U2 does.
- **(ii) xmodule** (`xmodule <fixture>`): research R9's synthesized layout. Every reference error must appear in blangd's publish.
- **`--check-list`** enforces both sets. The stdlib qualifying rule now **excludes** files that are members of a `blang.toml` project with dependencies (`test_build/*`); the examples stay in it (research R1).

**Goldens** (`test_files/lsp/project_*.lsp.jsonl`):
- `project_missing_path`;
- `project_git_unfetched` (`server_env XDG_CACHE_HOME=${ROOT}/test_files/lsp/projects/empty_cache`);
- `project_malformed_toml`;
- `project_cycle`;
- `project_unsaved_dep`: open `app/main.b` (clean), open `lib/lib.b`, `didChange` the lib to remove the called `pub fn`, then `didChange` main to trigger a publish with the error, then restore the lib and publish clean.

**Git-shim leg.** In `test_lsp.sh` (research R10), plus `lsp_client.py --server-env`.

**Latency.** The leg's `LATENCY_FILES` gains `test_build/usebox/main.b`.

**`parser_no_leak`.** A new loop of 50 × `compileDocument( <src>/test_build/usebox/main.b, …, overlay{} )`, asserting `res.deps.size() >= 2` and zero diagnostics.

**`project_layout` ctest** covers:
- walk-up and stop-at-first;
- membership;
- `resolvePath`;
- `locateGitCheckout`: name/hash format, warm vs cold, and the unpinned text;
- `mergeByOrigin`;
- interface byte-parity for the `test_build` libraries, against a cold in-place build under a scratch cache. This is skipped with a notice when no `.bmod` has been built.

**CI.** The `build-system` job gains `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus project --check-list` after `run_build_tests.sh`. The `lsp` job's `test_lsp.sh` passes `--no-project-corpus` (research R2).

### D-8. No-subprocess proof

The proof has three layers:
- **Binary level:** `blangd_no_subprocess`, unchanged. `ProjectLayout`'s shared code contains no `runCommand`/`fork`. The fetch stays in `bcc.cpp`.
- **Behavioral:** the git-shim leg.
- **Source level:** `grep -n "runCommand\|popen\|fork\|exec" ProjectLayout.cpp lsp/*.cpp` prints nothing.

### D-9. Security review inputs (Gate 7)

| Input | Handling |
|---|---|
| `[deps]` paths | Resolved only via the shared `resolvePath` relative to the declaring toml, as `bcc` does. Absolute paths and `..` behave as in `bcc` (no extra policy, per the parity rule). Only `stat`/`opendir`/read are used. |
| git URLs and pins | Used only as hash input and message text. They are never executed or passed to a shell. |
| toml text | Parsed by `ProjectConfig`. Issues go to the sink, and huge files are bounded by the same per-line parse. |
| Dependency sources | Parsed like any source: located errors, a bounded `Run`, released per reparse. |

Each item will be recorded in `checklists/security.md`, with citations.

## Success criteria → test

| SC | Test |
|---|---|
| SC-001 (10 binaries, myapp/counterapp at zero errors) | project corpus (i); `checklists/corpus-run.md` per-file output |
| SC-002 (11 xmodule fixtures) | project corpus (ii) |
| SC-003 (setup diagnostics, shim) | `project_missing_path`, `project_git_unfetched`, `project_malformed_toml`, `project_cycle` goldens, plus the git-shim leg |
| SC-004 (unsaved dependency) | `project_unsaved_dep` golden |
| SC-005 (latency) | `lsp_latency.py` on usebox |
| SC-006 (leaks) | `parser_no_leak` project loop under `build-asan` |
| SC-007 (no regressions) | the full gate list, the `bcc build -v` and `--emit-bmod` byte-identity, and the U2 stdlib corpus (now 177 entries plus the 2 that moved) |

## Verification protocol

At each phase checkpoint:
1. `./test_lsp.sh` and `--selfcheck` in `build` and `build-nollvm`. The project leg runs in `build` only.
2. **`bcc` neutrality:** `bcc build -v` stdout/stderr/exit for all 10 binaries (scratch copy, scratch cache), before vs after, with `cmp`. Then `test_build/run_build_tests.sh`, including the `.bmod` goldens and the git leg.
3. ctest 87/87 × 3 (86 + `project_layout`). `parser_no_leak` is LSan-clean.
4. `run_tests.sh` × 3; `test_codegen.sh` with `--leak-check` and `--selfcheck`; the reach-in gate; examples; demos.
5. The project corpus also runs under the ASan blangd. Every clean project must be LSan-clean, because its documents have no parse errors.

## Phasing (for /speckit-tasks)

1. **D-1** (shared helpers and the `ProjectConfig` sink), then a `bcc` neutrality checkpoint.
2. **D-3(c) emitter provider and D-2 manual resolver**, both default-path-neutral, then a neutrality checkpoint (the `.bmod` goldens).
3. **D-2 per-run resolver** across all blangd compiles, which supersedes U2's `sharedResolver`. The U2 corpus and goldens must stay green.
4. **D-4/D-6** locate, graph, interfaces and plan. **The project corpus (i) goes green** here (MVP).
5. **D-5** setup diagnostics and suppression, plus the negative goldens and the shim leg.
6. **D-6** the unsaved-dependency golden.
7. **D-7** xmodule synthesis, latency, the `parser_no_leak` loop, the `project_layout` ctest and CI.
8. Docs and the security checklist, then the final verification.

## Decisions carried from the spec (UNCONFIRMED, as designed)

| Decision | Design |
|---|---|
| R4 lib projects: no stdlib, no combine | D-4 step 5 (library branch) |
| R5 non-member falls back to single-file | D-4 step 1 |
| R6 failed dependency compile is unresolvable | D-3(b) → D-5 |
| R7 malformed toml is a Warning, and compilation continues | D-4 step 2, D-5 |
| R8 toml diagnostics go on the document at 1:1 | D-5 placement |
| R9 cache from blangd's environment | `gitCacheRoot()` shared, unchanged |
| R10 no caching; raise on a latency miss | D-4 (rebuild every reparse) |

**New in this plan (UNCONFIRMED):**
- the cascade-suppression filter (research R7);
- the project corpus running in CI `build-system`, not `lsp`, because its reference needs `llc` (research R2).

## Constitution re-check (post-design)

All PASS, unchanged.
- **One copy of each rule.** The helpers move to shared code and `bcc` calls them. Interfaces use the one emitter. `ProjectCompile` is untouched.
- **Neutral by construction.** The default paths of every edited shared unit are byte-neutral and proven.
- **Explicit ownership.** Ownership is spelled out in D-4 and locked by LSan.
- **No new execution surface.**

## Complexity Tracking

There are no constitution violations to justify.
