---

description: "Task list for U3 — blangd project context (blang.toml, sibling sources, path/fetched-git deps from source)"
---

# Tasks: blangd project context

**Input**: Design documents from `specs/043-lsp-project-context/`:
- [plan.md](plan.md) (D-1 to D-9, plus the "Success criteria → test" table);
- [spec.md](spec.md);
- [research.md](research.md) (R1–R12);
- [data-model.md](data-model.md);
- [quickstart.md](quickstart.md);
- contracts: [project-layout-api.md](contracts/project-layout-api.md), [project-context.md](contracts/project-context.md) and [lsp-test-tools-u3.md](contracts/lsp-test-tools-u3.md).

The binding audit is [`design-audit-U3.md`](../../docs/epics/modules-v2-lsp/design-audit-U3.md) (R1–R3).

**Tests**: REQUIRED. The spec names them in FR-015 to FR-020 and SC-001 to SC-007. Every behavior task below is backed by one of these committed tests.

**Organization**: by user story. US1 (P1) is project context with correct diagnostics. US2 (P1) is broken setups: one diagnostic, no crash, no process. US3 (P2) is unsaved buffers. US4 (P2) is latency.

The binding audit order is applied in Phase 2, before any story:
1. **R3** shared helpers, so that every later rule is shared from the start;
2. **R1** emitter provider;
3. **R2** isolated runs.

## ⚠ Standing rules for EVERY task (plan constraints 1–5)

1. **No blangd-only rule** (FR-007; audit R3). blangd may only call shared code that `bcc`, `qcc` or the frontend also calls. That covers `ProjectLayout`, `ProjectConfig`, `StdlibSelect`, `ProjectCompile::run` and `BmodEmitter::emit`. If a task seems to need a copy of a rule, that is a STOP: raise it as a `BLOCKER:` question.
2. **No process, no network** (FR-008). In `lsp/` and `ProjectLayout.cpp` there must be no `fork`/`exec*`/`popen`/`system`/socket call and no `runCommand`. The git fetch stays in `bcc.cpp` only. `blangd_no_subprocess` must pass after every commit.
3. **`qcc`/`bcc` frozen** (FR-021).
   - There are no `qcc`/`bcc` golden edits.
   - After every commit, `git diff --name-only --diff-filter=MDTRC 042-lsp-stdlib-context...HEAD -- test_files/` may list only `test_files/lsp/corpus.txt`. This is the U2 file whose entries legitimately change here (research R1).
   - Added files may appear only under `test_files/lsp/projects/` (including `warm_cache/`, analyze M2) and `test_files/lsp/project_*`.
4. **KG-7 ownership** (FR-014). Each `Run` is a resolver plus a context. Its resolver is declared first and destroyed last. `CompileResult` follows plan D-4's member order. Nothing is reused across reparses. Adding any cache is a STOP-and-ask (R10).
5. **Never crash, never write to stderr** (FR-010). Everything is reported as a published diagnostic.
6. House style: tabs, spaces inside parentheses, Allman braces, `m` members. The tools use python3 stdlib only.

---

## Phase 1: Setup (baseline + neutrality reference)

- [x] T001 Confirm the starting state and record it in `specs/043-lsp-project-context/checklists/baseline.md`:
  - the branch is `043-lsp-project-context`;
  - `git merge-base --is-ancestor dc39603 HEAD` succeeds;
  - the HEAD SHA.

  Also record U2's final gate counts as the pre-unit baseline: ctest 86/86 ×3, `run_tests` 251/244/251, `test_lsp` 78/0, `test_codegen` 172/0 with Leaks: 0, `run_build_tests` all pass, stdlib corpus 179/179 (FR-021, SC-007).
- [x] T002 Capture the pre-unit `bcc` reference for the shared-helper move (FR-021, SC-007).
  1. Copy `build/bcc`, `build/qcc` and `build/stdlib/` to `/tmp/u3-ref/bin/`.
  2. Copy `test_build/` to `/tmp/u3-ref/tree/` and set `XDG_CACHE_HOME=/tmp/u3-ref/cache-before`.
  3. For each of the 10 binaries (`aliasapp boxapp counterapp diamondapp myapp printapp sizeapp timerapp todoapp usebox`), save `bcc build -v <abs dir>` stdout, stderr and exit code to `/tmp/u3-ref/before/<name>.*`.
  4. Also save `bcc build` stderr for a scratch project whose `blang.toml` has a malformed line and an unpinned git dependency. This pins the warning and error texts.
  5. Save `bcc build` exit code and stderr for a scratch cycle pair (a↔b) and a scratch path dependency to a non-existent directory, and `bcc build -v` for `test_build/diamondapp`. These are the before-outputs for T007's audit-addendum H1 legs.

  These are scratch files and are not committed.

**Checkpoint**: baseline recorded.

---

## Phase 2: Foundational (blocking; audit R3 → R1 → R2)

### R3: shared project helpers that `bcc` itself calls (FR-007; plan D-1; contract project-layout-api.md)

- [x] T003 Create `ProjectLayout.h` and `ProjectLayout.cpp` (`namespace BlangProject`). Move these **verbatim** from `bcc.cpp`, keeping their comments:
  - `discoverSourceFiles` (bcc.cpp ~L899);
  - `resolvePath` (~L936);
  - `gitCacheRoot` (~L956);
  - `shortHashHex` (~L967).

  Add:
  - **`locateGitCheckout( const Dependency & )`** returning `GitLocation{dir, error, warm}`. This is the pure first half of `resolveGitDependency`:
    - the pin check uses `bcc`'s exact unpinned message;
    - `dir = gitCacheRoot() + "/" + name + "-" + shortHashHex( url + "@" + pin )`;
    - `warm = stat( dir + "/blang.toml" ) == 0`.
  - **`resolveDependencyDir( declaringDir, dep, Fetcher * )`** returning `DepDir{dir, origin, error, unfetched}`:
    - path deps use `resolvePath`;
    - git deps use `locateGitCheckout`, and the fetcher only when it is non-null and the checkout is cold;
    - keep `bcc`'s "neither a path nor a git url" text;
    - `origin = realpath( dir )`, else `dir`.
  - **`findProjectRoot( startDir )`**: the nearest ancestor (inclusive) that has a `blang.toml`, or `""`.
  - **`mergeByOrigin`**, which is `mergeDep` made generic.

  Includes are POSIX `dirent`, `stat`, `limits`, `cstdlib` and `sha256.h` only. There is **no** process call.
- [x] T004 In `bcc.cpp`:
  - delete the moved bodies and `#include "ProjectLayout.h"`;
  - `buildProject`'s dependency loop calls `BlangProject::resolveDependencyDir( projectDir, dep, &fetcher )`. The fetcher is a lambda that wraps the remaining fetch half of `resolveGitDependency` (`runCommand` git clone / checkout / rename), with every `cerr` text unchanged;
  - `mergeDep` becomes `BlangProject::mergeByOrigin( outClosure, d, []( const DepArtifact &a ){ return a.origin; } )`;
  - `discoverSourceFiles` calls become `BlangProject::discoverSourceFiles`.

  Error paths keep `bcc`'s exact `cerr` messages and return codes.
- [x] T004b Extract `walkDependencies` + `WalkVisitor` into `ProjectLayout.{h,cpp}`. Rewrite `bcc.cpp buildProject` as a visitor with no recursion of its own. Grep gate: `grep -n 'buildProject(' bcc.cpp` shows only the definition and the `runBuild` call; `grep -nE 'building\.(insert|count|erase)' bcc.cpp` prints nothing. *(Audit addendum H1; FR-007, FR-021. Semantics 1–6 in contracts/project-layout-api.md: `bcc`'s texts and stop-at-first-failure stay in its visitor; the shared walk prints nothing.)* Also add `BlangStdlib::scanImportsAll( const std::vector<std::string> &texts )` to `StdlibSelect.h/.cpp`, and replace `buildProject`'s `projImports` loop with `scanImportsAll( texts-of(sources) )`, the texts read as today with `readFileToString` (audit addendum H2; byte-neutral, covered by T007).
- [x] T005 [P] In `ProjectConfig.h` and `ProjectConfig.cpp`:
  - add `struct ConfigIssue { int line; std::string message; }`;
  - add `static ProjectConfig *loadFromText( const std::string &path, const std::string &text, std::vector<ConfigIssue> *issues )`;
  - add `int line = 0` to `Dependency`, set to the toml line of the entry.

  The parsing loop moves into `loadFromText`, which reads from an `istringstream`. Each of the two `cerr` sites (~L124, ~L179) becomes "append a `ConfigIssue` if `issues`, else print the identical `cerr` line". `loadFromFile` reads the file (still returning `nullptr` if it cannot be opened) and calls `loadFromText( path, text, nullptr )`.
- [x] T005b [P] (`project_layout` ctest) two texts, `"import a;"` with no trailing newline followed by `"import b;\n"`, and `"x\n// c"` followed by `"import d;\n"`. `scanImportsAll` yields `{a,b}` and `{d}`. A teeth leg asserts that the concatenated-scan form yields a different set. *(Audit addendum H2; FR-003, FR-007.)*
- [x] T006 In `CMakeLists.txt`, add `ProjectLayout.cpp` and `ProjectConfig.cpp` to `BLANG_FRONTEND_SOURCES`, and `ProjectLayout.cpp` to the `bcc` target, which already has `ProjectConfig.cpp` and `sha256.c`. Rebuild `build`, `build-nollvm` and `build-asan`.
- [x] T007 R3 neutrality checkpoint (FR-021, SC-007):
  - re-run the T002 commands with the new `bcc`, under the same directory layout and stdlib path and a fresh `XDG_CACHE_HOME=/tmp/u3-ref/cache-after`;
  - `cmp` every output against `/tmp/u3-ref/before/`, normalizing only the cache-directory prefix; it must be identical;
  - `test_build/run_build_tests.sh` passes all legs, including the git dependency, diamond and `.bmod` goldens;
  - `grep -nE "^static .*(discoverSourceFiles|resolvePath|gitCacheRoot|shortHashHex|mergeDep)" bcc.cpp` prints nothing;
  - `blangd_no_subprocess` passes;
  - the cycle and missing-dependency negative legs (scratch toml pair a↔b; a path dependency to a non-existent dir): `bcc build` exit code and stderr `cmp`-identical to `/tmp/u3-ref`. The diamond leg (KG-8) is `cmp`-identical under `-v` (audit addendum H1). T002 captures the matching before-outputs for these three scratch projects too.

  Commit.

### R1: emitter slices from the parsed text (FR-005, FR-011; plan D-3(c); research R5)

- [x] T008 In `BmodEmitter.h` and `BmodEmitter.cpp`:
  - add `using SourceTextProvider = std::function<const std::string *( const std::string &path )>;`;
  - change the signature to `static void emit( const std::vector<Module*> &modules, std::ostream &out, Scope *scope, const SourceTextProvider *text = nullptr );`;
  - thread the provider to `sliceDefinitionSource( loc, text )` through a file-static `const SourceTextProvider *gActiveProvider`, set for the duration of `emit` with an RAII reset (no global state survives the call).

  **No provider** (`qcc`): today's disk-cache path, byte-identical. **With a provider**, `gSourceLineCache` is never read or written. A provider hit splits that text for this call only. A provider **miss** for a slice location is an emitter failure: `emit` records the path in `std::vector<std::string> *missing` (a new out-parameter, default `nullptr`) and emits nothing further for that definition. The caller must treat any non-empty `missing` as a failed derivation. The `qcc` call passes neither argument. *(Audit addendum H3.)*
- [x] T009 R1 neutrality checkpoint:
  - `test_build/run_build_tests.sh` passes, with the `.bmod` goldens under `test_files/golden/bmod/` unchanged;
  - for every `test_files/fail/xmodule/*/lib.b` and for `test_build/mathlib/*.b`, `qcc --parse-only --emit-bmod` output is `cmp`-identical against `/tmp/u3-ref/bin/qcc` (U1 method);
  - a `project_layout` or `project_compile` case where the provider deliberately omits one generic-defining source. It asserts `missing == {that path}` and that `gSourceLineCache` is untouched: a pre-seeded different disk text for the same path is **not** what was emitted (audit addendum H3).

  Commit.

### R2: one isolated run per compile (FR-013, FR-014; plan D-2; research R6)

- [x] T010 In `Resolver.h` and `Resolver.cpp`, add `enum class AliasMode { kInstall, kManual };` and `explicit Resolver( AliasMode mode )`. Under `kManual` the resolver neither saves, installs nor restores `gScope`. The existing default constructor is behaviorally identical: it equals `Resolver( AliasMode::kInstall )` and keeps the `mPrevGScope` save/restore. `resolver_component`, `resolver_reuse`, `project_compile` and `parser_no_leak` must stay green and unmodified.
- [x] T011 In a new file `lsp/Run.h`:
  - add `struct Run { std::shared_ptr<QLang::Resolver> resolver; std::shared_ptr<QLang::CompileContext> ctx; };`, with `resolver` declared **first**;
  - add `class GlobalAliasScope`, which saves `gScope`, sets it to `r.globalScope()`, and restores it in its destructor;
  - add a helper `Run runPlan( const QLang::CompilePlan &, QLang::DiagnosticEngine & )` that creates `std::make_shared<Resolver>( AliasMode::kManual )`, opens a `GlobalAliasScope` around exactly one `ProjectCompile::run`, and returns the `Run`.
- [x] T012 In `lsp/Compile.cpp`, replace U2's `sharedResolver()` and `runPlan` with the `Run`-based helper for **every** compile: the two-argument single-file overload, the U2 stdlib path and the U2 fallback.
  - `CompileResult` gains `std::shared_ptr<QLang::Resolver> resolver` as its **first** member, followed by `context`, a new `std::vector<std::shared_ptr<Run>> deps`, then `module`, `fileScope` and `diagnostics`, in plan D-4's order. Update `lsp/Compile.h` with a comment stating the ownership order and the KG-7 invariant. This supersedes U2's analyze F2 per audit R2.
  - Verify by grep that no code outside a `run` reads `gScope`: `grep -n "gScope" lsp/*.cpp lsp/AstLocator.cpp Sema.cpp`. Expect only the guard and the comments.
- [x] T013 R2 checkpoint:
  - `./test_lsp.sh` and `BUILD_DIR=build-nollvm ./test_lsp.sh` stay **78/0**, with the stdlib corpus 179/179 and all U2 goldens unchanged;
  - `ctest` is 86/86 in all three trees, and `parser_no_leak`'s U2 loop is LSan-clean in `build-asan`.

  Commit.

### Shared-helper unit test

- [x] T014 [P] Create `ProjectLayoutTest.cpp`, an assert harness that does not compile out under NDEBUG. It takes `argv[1] = <source root>`, uses scratch directories made with `mkdtemp`, and cleans up. It covers (FR-007):
  - **(a)** `findProjectRoot`: walk-up from a nested directory, stop at the first `blang.toml`, return `""` when there is none;
  - **(b)** `discoverSourceFiles`: root-only, sorted;
  - **(c)** `resolvePath`: relative, absolute and nonexistent paths;
  - **(d)** `locateGitCheckout`:
    - the unpinned error text;
    - the directory format `<root>/<name>-<16 hex>` under a scratch `XDG_CACHE_HOME`;
    - `warm` is false, then true after creating `<dir>/blang.toml`;
    - `rev` takes precedence over `tag`;
    - the committed `test_files/lsp/projects/warm_cache/blang/git/greet-<hash>/` directory name equals `locateGitCheckout` for `gitdep_warm`'s dependency under that `XDG_CACHE_HOME` (analyze M2: the fixture cannot drift from the shared rule);
  - **(e)** `mergeByOrigin` deduplicates by origin;
  - **(f)** `resolveDependencyDir` with a null fetcher: a cold git dependency gives `unfetched == true` and no fetch;
  - **(g)** `ProjectConfig::loadFromText`: issues are collected for a malformed line, nothing is written to `cerr`, `Dependency::line` is correct, and a `blang.toml` that `loadFromFile` opens still parses identically.

  Register it in `CMakeLists.txt` as `add_test( NAME project_layout COMMAND projectLayoutTest ${CMAKE_CURRENT_SOURCE_DIR} )`. ctest goes to 87/87.

**Checkpoint**: shared rules are in place and `bcc` is proven neutral. The emitter accepts parsed text, and every compile is isolated. U2 behavior is unchanged.

---

## Phase 3: User Story 1 — projects show exactly `bcc build`'s diagnostics (P1) 🎯 MVP

**Goal**: member documents compile with `bcc build`'s plan, with dependencies resolved from source, interfaces byte-identical, and identities and capabilities matching (FR-001 to FR-006, FR-014, FR-015, FR-020; SC-001, SC-002, SC-006).

**Independent Test**: `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus project --check-list` reports every `project` and `xmodule` entry PASS.

### Core

- [X] T015 [US1] In `lsp/DocumentStore.h`, add path-keyed overlay lookup: `const std::string *textForPath( const std::string &realPath ) const`. Maintain a `realpath( uriToPath( uri ) )` → uri index on open, change and close, falling back to the literal path when `realpath` fails. Add a small `Overlay` view type passed by reference to `compileDocument` (FR-011).
- [X] T016 [US1] Create `lsp/ProjectContext.h` and `lsp/ProjectContext.cpp`, following data-model.md "Project" and contract project-context.md "Discovery and membership":
  - `locateProject( docPath, overlay )` → `Project{root, config, issues, sources, isMember, origin, kind}`;
  - it uses `BlangProject::findProjectRoot`, `ProjectConfig::loadFromText` (with the overlay text when the toml is open) and `BlangProject::discoverSourceFiles`;
  - `isMember` is decided by `realpath` equality (R5; FR-001, FR-002);
  - for a member, the document's `CompileInput::path` is the **URI path exactly** (`uriToPath( uri )`), so the publish filter (`location.file == path`) and the `module`/`fileScope` lookup always match it, even through symlinks or unnormalized paths. Sibling sources keep their `discoverSourceFiles` path. Overlay lookups are keyed by `realpath`; the D-3 provider is keyed by `CompileInput::path` (analyze M6; C1/C5).
- [X] T017 [US1] In `lsp/ProjectContext.cpp`, implement the dependency graph (data-model "Dependency node"; plan D-4 step 3; FR-004, FR-006):
  - implement the blangd `WalkVisitor` over `BlangProject::walkDependencies( root, nullptr, v )`. `load` = `loadFromText` from the overlay or disk with the issue sink. No DFS, on-path set or fold code in `lsp/`. Grep gate: `grep -nE 'onPath|mergeByOrigin|building' lsp/*.cpp` matches only calls to and fields of the shared API. *(Audit addendum H1.)* The visitor's `onDepError`/`onCycle` return 0 so the walk continues; `leave` derives the interface (T018), memoized by origin for this compile only;
  - each failure recorded as `unresolvable{reason}`, where the reason is missing-path, unfetched-git, unpinned-git, no-path-or-git or cycle. The failed node, and nodes reachable only through it, are excluded from the closure.
- [X] T018 [US1] In `lsp/ProjectContext.cpp`, implement interface derivation (plan D-3; FR-005, FR-011), leaves first in fold order. For each resolved dependency:
  - **(a)** Build its plan:
    - non-combine, `kCanonical`, `moduleOrigin = origin`;
    - its sources as **in-memory** `kSource` inputs, with text from `overlay.textForPath( p )` or else a disk read;
    - its closure interfaces as in-memory `kInterface` inputs with path `<depDir>/<name>.bmod`, `origin` set, and the text derived earlier.
  - **(b)** Run it with `runPlan` (T011). If `ctx->hadError() || diag.hasErrors()`, mark the dependency `compile-failed{first error}` (R6).
  - **(c)** Emit with `BmodEmitter::emit( recorded non-extern modules, ostringstream, ctx->bmodEmitScope(), &provider, &missing )`, **inside** that run's `GlobalAliasScope` (analyze L3). The provider is a per-run `map<path, const std::string*>` over **every** `CompileInput` of that dependency's run: each source path and each closure-interface path (`<depDir>/<name>.bmod`), keyed exactly by `CompileInput::path` (the string `SourceLocation.file` carries). If `emit`'s `missing` is non-empty, mark the dependency `compile-failed` with `internal error: no source text for '<path>' while deriving the interface of '<name>'` (published per D-5, R6). It is never a crash or a silent fallback. *(Audit addendum H3.)*
  - **(d)** Store the interface text and keep the `Run` for `CompileResult::deps` (U4 navigation).
- [X] T019 [US1] In `lsp/Compile.cpp`, add the project branch of `compileDocument( path, text, stdlib, overlay )` (plan D-4 steps 1, 2, 5, 6; FR-003):
  - **Not a member or no project:** the U2 path, unchanged.
  - **Binary:**
    - combine, iff `BlangStdlib::selectStdlibFiles( stdlib.path, BlangStdlib::scanImportsAll( per-file texts of all project sources ) )` is non-empty, where each text is the overlay text or the disk text, and the document's own text is `text`. **Never concatenate** (audit addendum H2);
    - stdlib inputs come from disk;
    - project sources are in-memory, with the document's own text being `text`;
    - the resolved closure interfaces are in-memory;
    - `moduleOrigin = realpath(root)`, `kCanonical`.

    If the stdlib is unusable (U2), leave it out of the plan, and U2's warning still leads.
  - **Library:** non-combine, no stdlib (R4).

  Run with `runPlan`. Fill `resolver`, `context` and `deps`. Take `module` and `fileScope` from the record whose path is the document's path, not `records().back()`, because any member file may be open. Publish the run diagnostics located in `path`.
- [X] T020 [US1] In `lsp/Server.cpp`, pass the overlay from `mDocs` into `compileDocument`. Keep U2's erase-and-emplace replacement. Run `./test_lsp.sh`: U2's 78 must still pass.

### Fixtures, corpus and CI

- [X] T021 [P] [US1] Create the multi-file sibling fixture `test_files/lsp/projects/multifile/` (`blang.toml` with `type = "bin"` and two root `.b` sources) per research R8.
  - Author it so that `bcc build` (scratch copy, scratch cache) succeeds and its final `qcc` reference exits 0 with a **real cross-file resolution**, using the cross-file call shape `bcc`'s routing accepts.
  - Record a manual teeth check in `specs/043-lsp-project-context/checklists/corpus-run.md`: deleting the callee makes both `bcc` and blangd report an error.
- [X] T022 [US1] Extend `tools/lsp_diag_corpus.py` with `--corpus project` per contract lsp-test-tools-u3.md:
  - **Scratch tree:** copy `test_build/`, `examples/` and the needed `test_files` subtrees into `mkdtemp()`, set `XDG_CACHE_HOME=<scratch>/cache` for **both** `bcc` and blangd, and never write to the repository.
  - **`project <dir>` entries:**
    - the reference is `bcc build -v <abs scratch dir>`; its **last** `/qcc ` line is split with `shlex`, `--json` is appended, and it runs with `cwd = <scratch dir>`;
    - the premise is that the reference exits 0;
    - blangd opens `<scratch dir>/main.b` in one session;
    - compare sorted multisets for entries located in `main.b`;
    - premise: the reference reports no diagnostic located outside `main.b` (so a sibling source such as `multifile`'s cannot hide a mismatch; analyze L4).
  - **`xmodule <fixture>` entries:**
    - synthesize the lib and app layout (research R9);
    - the reference is `qcc --emit-bmod lib.bmod lib.b`, then `qcc --parse-only --json consumer.b lib.bmod`;
    - every reference **error** located in `consumer.b` must be present in blangd's publish, **and** blangd must publish no **error** in `consumer.b` that the reference lacks (SC-002 "the same located diagnostic"; analyze M4). Extra warnings are tolerated and reported (research R9);
    - built-in teeth: on a copy of one fixture with the reach-in line removed, the reference must have no error. That shows the assertion is not vacuous.
  - **Exit code 2** when there is no LLVM `qcc` or no `llc`/`llc-18`.
- [X] T023 [US1] In `tools/lsp_diag_corpus.py`, change the **stdlib** qualifying rule: exclude files whose `findProjectRoot` project has `[deps]` and that are members (the `test_build/*` binaries). The six `examples/*/main.b` stay (research R1). Extend `--check-list` for the project and xmodule sets (contract).

  Then update `test_files/lsp/corpus.txt`:
  - remove `stdlib test_build/myapp/main.b` and `stdlib test_build/counterapp/main.b`;
  - add `project` entries for the 10 binaries, the 6 `examples` and `test_files/lsp/projects/multifile`;
  - add `xmodule` entries for all 11 fixtures;
  - update the header comment.
- [X] T024 [US1] Run the project corpus:
  - `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus project --check-list` must have every entry PASS;
  - `myapp` and `counterapp` must show 0 diagnostics (SC-001);
  - all 11 xmodule entries must PASS (SC-002).

  Also run `--corpus stdlib --check-list` in `build` and `build-nollvm`: 177 PASS. SC-007 evidence = stdlib corpus **177/177** + `myapp` and `counterapp` **PASS in the project corpus** = **179/179 accounted**. Name the two moved entries in `checklists/corpus-run.md`. A file that drops out of both sets is a STOP (audit addendum M1).

  Fix any FAIL in `lsp/` plan assembly only. A FAIL that seems to need a new rule is a STOP. Record the per-entry output in `specs/043-lsp-project-context/checklists/corpus-run.md`.
- [X] T025 [US1] Add an interface byte-parity check (plan D-3 parity argument; FR-005) to the `project_layout` ctest, or as a `--interface-parity` mode of the corpus tool:
  - for every `test_build` **library**, the blangd-derived interface text must equal the `.bmod` that a cold in-place `bcc build` produced under a scratch cache, byte for byte;
  - run it in the LLVM build and record the result in `corpus-run.md`.
- [X] T026 [US1] Wire the project corpus into `test_lsp.sh` and CI (research R2; plan D-7):
  - **`test_lsp.sh`**: add a project-corpus leg after the stdlib corpus leg. It runs only when `$BUILD_DIR/CMakeCache.txt` shows LLVM enabled and `llc-18`/`llc` is on `PATH`. Otherwise it prints `SKIP: project corpus (needs llc + an LLVM qcc; runs in CI build-system)`, and when `CI=true` the skip counts as a FAIL. It accepts `--no-project-corpus`.
  - **`.github/workflows/ci.yml`**:
    - the `lsp` job's `./test_lsp.sh` invocations pass `--no-project-corpus`;
    - the `build-system` job gains, after `run_build_tests.sh`, a step `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus project --check-list`. That job already builds `blangd` in its full build.

### Ownership lock

- [X] T027 [US1] In `ParserLeakTest.cpp`, **append** a project-context loop (FR-020, SC-006); existing blocks stay unmodified.
  - Create a **fresh** `lsp::StdlibLocation loc = lsp::locateStdlib( BLANG_SOURCE_STDLIB_DIR, "", "" )` in the new block; U2's `loc` is scoped to its own block (analyze L2).
  - Run 50 iterations of `lsp::compileDocument( BLANG_SOURCE_ROOT "/test_build/usebox/main.b", <file text>, loc, emptyOverlay )`.
  - Assert in each that `res.deps.size() >= 2` (boxq and midx) and `res.diagnostics.empty()`, then drop the result.
  - Add a comment stating the D-4 ownership order.
  - Add `target_compile_definitions( parserLeakTest PRIVATE BLANG_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}" )`.

  Under `build-asan`, it must be LSan-clean. Do a teeth check without committing it: leak one `Run` per iteration and confirm LSan fails, then revert. Record the result in `corpus-run.md`. Commit.

**Checkpoint**: US1 complete. The 10 binaries, 6 examples, the multi-file fixture and the 11 xmodule layouts all match `bcc`/`qcc`. Memory is bounded.

---

## Phase 4: User Story 2 — broken setups: one diagnostic, never a crash, never a process (P1)

**Goal**: FR-008 to FR-010, FR-016 to FR-018; SC-003.

**Independent Test**: the four `project_*` negative goldens match, and the git-shim leg reports no marker.

- [X] T028 [US2] In `lsp/ProjectContext.cpp` and `lsp/Compile.cpp`, implement setup diagnostics exactly per plan D-5 and contract project-context.md:
  - **Unresolvable dependencies:** Error. Use the fixed message per reason. Place it at the document's first parsed `ImportStatement` whose `getModuleName()` equals the import name: the `[project] name` if readable, else the `[deps]` key. Otherwise place it at 1:1 with ` (declared at <toml>:<line>)`.
  - **Dependency compile failure:** Error, with message `dependency '<name>' failed to compile: <file>:<line>:<col>: <message>` (R6).
  - **Cycle:** Error, with message `circular dependency: a -> b -> a`.
  - **`blang.toml` issues:** each one is a Warning at 1:1 with message `<toml>:<line>: parse error: <message>` (R7/R8).
  - **Publish order:** the toml Warnings, then the setup Errors, then the run's diagnostics.
- [X] T029 [US2] In `lsp/Compile.cpp`, implement the cascade suppression (research R7; plan D-5). For each unresolvable module name `<n>`, drop run diagnostics whose message contains `'<n>'` or `'<n>.`. Keep everything else.
- [X] T030 [P] [US2] Create the negative fixtures under `test_files/lsp/projects/`. Each fixture's `main.b` **also** contains one unrelated real error (e.g. `int x = "s";`), which proves the suppression is narrow.
  - **`missing_path/`:** a bin with `nope = { path = "../does_not_exist" }`, and `main.b` that does `import nope;` and uses `nope.f()`.
  - **`gitdep_unfetched/`:** a bin with `greet = { git = "file:///nonexistent/greet.git", tag = "v1" }`, `import greet;`, and `greet.hi()`. Add `empty_cache/.keep`.
  - **`malformed_toml/`:** a bin whose `blang.toml` has one `this is not toml` line plus a valid `[project]`.
  - **`gitdep_warm/` + `warm_cache/blang/git/greet-<hash>/`** (analyze M2): a bin with `greet = { git = "file:///nonexistent/greet.git", tag = "v1" }` (the same dependency as `gitdep_unfetched`, so the hash matches), whose `main.b` uses `greet.hi()` cleanly; the committed fake checkout holds a `blang.toml` (`name = "greet"`, `type = "lib"`) and `lib.b` with `pub fn hi() -> int`. No git ever runs.
  - **`gitdep_unpinned/`** (analyze M5): a bin with `greet = { git = "file:///nonexistent/greet.git" }` (no tag/rev), `import greet;`.
  - **`dep_failed/{app,lib}/`** (analyze M5): `lib/lib.b` has a Sema error (e.g. `int x = "s";` inside a `pub fn`); `app/main.b` does `import lib;` and calls it.
  - **`cycle_a/` and `cycle_b/`:** two libraries that depend on each other, plus `main.b` in `cycle_a` importing `cycle_b`. If a library cannot hold `main.b` meaningfully, use a bin `cycle_app/` that depends on `cycle_a`.
- [X] T031 [US2] In `tools/lsp_client.py`, add `--server-env NAME=VALUE` (repeatable), applied after the script's `server_env` steps and `${ROOT}`-substituted. Update the docstring (contract lsp-test-tools-u3.md).
- [X] T032 [US2] Create `test_files/lsp/project_missing_path.lsp.jsonl`, `project_git_unfetched.lsp.jsonl` (with `{"server_env": {"XDG_CACHE_HOME": "${ROOT}/test_files/lsp/projects/empty_cache"}}`), `project_malformed_toml.lsp.jsonl`, `project_cycle.lsp.jsonl`, `project_git_warm.lsp.jsonl` (`server_env XDG_CACHE_HOME=${ROOT}/test_files/lsp/projects/warm_cache`; clean publish, analyze M2), `project_git_unpinned.lsp.jsonl` and `project_dep_compile_failed.lsp.jsonl` (analyze M5). Each opens its fixture's `main.b` and waits for the publish.

  Generate **only these** with `./test_lsp.sh --update-goldens test_files/lsp/project_*.lsp.jsonl`. Hand-review each against the D-5 table: exactly one setup diagnostic, the right severity and placement, the unrelated real error still present, and no `undefined symbol`/`unknown module` cascade for the bad dependency (FR-009, FR-016, FR-017(a–c), SC-003).
- [X] T033 [US2] Add a git-shim leg to `test_lsp.sh` (FR-018, SC-003):
  - create `shim=$(mktemp -d)` holding an executable `git` that does `touch "$shim/git-invoked"; exit 1`;
  - run `python3 tools/lsp_client.py --server "$BLANGD" --root "$SCRIPT_DIR" --script test_files/lsp/project_git_unfetched.lsp.jsonl --server-env "PATH=$shim:$PATH"`;
  - FAIL if `$shim/git-invoked` exists, or if the transcript lacks exactly one `git dependency 'greet' is not fetched` diagnostic;
  - remove `$shim`.

  Run it in both build modes. Also confirm that `sh tools/check_no_subprocess.sh build/blangd` still prints OK.
- [X] T034 [US2] Add a committed hostile-input leg to `test_lsp.sh` (FR-010, Gate 7; analyze M5: committed, not manual). In a `mktemp -d` scratch tree it builds three projects, then opens each one's `main.b` through `lsp_client.py --sema-fixture`-style sessions and FAILs unless the session exits 0 with strict framing (nothing on stderr) and at least one published diagnostic. The three projects are ones whose `blang.toml`:
  - is 5 MB of garbage;
  - is a directory;
  - names a dependency that is a symlink loop.

  The dependency-compile-failure path is pinned by the `project_dep_compile_failed` golden (T032). Record the leg's output in `specs/043-lsp-project-context/checklists/security.md`. Commit.

**Checkpoint**: US2 complete.

---

## Phase 5: User Story 3 — unsaved edits seen across the project (P2)

**Goal**: FR-011, FR-012; SC-004.

**Independent Test**: the `project_unsaved_dep` golden matches.

- [X] T035 [P] [US3] Create `test_files/lsp/projects/unsaved_dep/`, made of `lib/` (a lib whose `lib.b` has `pub fn helper() -> int`, a `pub fn base() -> int`, one **generic** `pub fn wrap<T>(T v) -> T` whose body calls `base()`, and a trailing `pub fn tail() -> int` (the N3 teeth target)) and `app/` (a bin with `lib = { path = "../lib" }`, whose `main.b` calls `lib.helper()` and the generic). With the files on disk it is clean.
- [X] T036 [US3] Create `test_files/lsp/project_unsaved_dep.lsp.jsonl`. It runs these steps:
  1. open `app/main.b` and wait for the publish, which must be clean;
  2. open `lib/lib.b`;
  3. `didChange` `lib/lib.b` to rename `helper` to `helper2`, without saving;
  4. `didChange` `app/main.b` with an identical full text, and wait for its publish, which must contain the missing-member error;
  5. `didChange` `lib/lib.b` back, then `didChange` `app/main.b`, and wait for its publish, which must be clean again;
  6. `didChange` `lib/lib.b` (unsaved) so that it **still compiles clean** but differs from disk in two ways: it inserts several comment lines **above** the generic, and it renames `base` to `base2` both at its definition and inside the generic's body. Then `didChange` `app/main.b` and wait: the publish must be **clean**. *(Rationale corrected per analyze N3.)* A slice taken from the disk text would not show the R6 diagnostic: the slice would start at the buffer's line number in the disk text. The fixture puts a second definition, `pub fn tail()`, at exactly that disk line, so the generic's "body" would become `tail()` and `lib.wrap` would drop out of the interface. The publish for `main.b` would then fail **at `lib.wrap`** (`Module 'lib' has no member 'wrap'`); this teeth check was confirmed by forcing a null provider. A clean publish is thus a corroborating end-to-end check that the generic body came from the buffer. **T009 (the `project_compile` C9 lock) remains the main lock on R1** (audit R1; analyze M3).

  Generate only this golden and hand-review it (FR-017(d), SC-004).
- [X] T037 [US3] Verify FR-012 by checking the golden: no step that only changes `lib/lib.b` produces a publish for `app/main.b`. Only the lib's own publish appears. Commit.

**Checkpoint**: US3 complete.

---

## Phase 6: User Story 4 — editing a project stays responsive (P2)

**Goal**: FR-019; SC-005.

- [X] T038 [US4] In `test_lsp.sh`, append `test_build/usebox/main.b` to `LATENCY_FILES`. Run `./test_lsp.sh` on `build` and record both medians in `corpus-run.md`; each must be ≤ 500 ms. If the `usebox` median is over budget, STOP and raise a question before adding any cache (R10). Commit.

**Checkpoint**: US4 complete.

---

## Phase 7: Polish & cross-cutting

- [ ] T039 [P] Update `CLAUDE.md`:
  - **Repository Structure**: add `ProjectLayout.{h,cpp}`, `ProjectLayoutTest.cpp`, `lsp/ProjectContext.{h,cpp}` and `lsp/Run.h`, and note `ProjectConfig::loadFromText`, the `BmodEmitter` text provider and `Resolver( AliasMode::kManual )`;
  - **Known Issues**: one U3 bullet covering blangd's project context (walk-up and membership, dependencies from source, interfaces via the shared emitter, per-run isolation closing U1 I6, setup diagnostics and suppression, the project corpus in CI `build-system`).

  The epic's docs-check wording, which removes "single-file" from the blangd lines, is U5's. Do not do it here, except where a line becomes factually wrong.
- [ ] T040 [P] Update `editors/README.md` with a "Projects" section covering:
  - `blang.toml` discovery and membership;
  - path and already-fetched git dependencies, with the note that the git cache is located from blangd's own `XDG_CACHE_HOME`/`HOME` (R9);
  - unsaved buffers;
  - the setup diagnostics.
- [ ] T041 Complete `specs/043-lsp-project-context/checklists/security.md` (plan D-9; Quality Gate 7), citing file and line for:
  - read-only access;
  - `resolvePath` parity;
  - git URL and pin used only as hash input and text;
  - the issue sink, with no stderr;
  - the absence of process calls (grep plus `blangd_no_subprocess` output);
  - the T034 robustness results.
- [ ] T042 Run the project corpus under the ASan blangd with `build-asan` (plan Verification §5). This is a local leg only; CI's `build-system` job runs the plain `build` corpus (analyze L5, accepted). Every clean project must be LSan-clean. Record the result. Documents with parse errors carry U2's known pre-existing parser leak, which is out of scope; note it if it is hit.
- [ ] T043 Final verification, the whole plan §Verification protocol on `build`, `build-nollvm` and `build-asan`:
  - `test_lsp.sh` and `--selfcheck` in each tree;
  - ctest 87/87 ×3;
  - `run_tests.sh` ×3;
  - `test_codegen.sh` with `--leak-check` (Leaks: 0) and `--selfcheck`;
  - `run_build_tests.sh`;
  - the T007 `bcc build -v` `cmp` and the T009 `--emit-bmod` `cmp`;
  - stdlib corpus 177/177 in both modes, and SC-007 evidence = stdlib corpus **177/177** + `myapp` and `counterapp` **PASS in the project corpus** = **179/179 accounted** (audit addendum M1);
  - the project corpus;
  - the reach-in gate, examples and demos;
  - standing rule 3's `git diff`.

  Record the counts in `baseline.md`. Confirm `git diff 285b362 HEAD -- specs/043-lsp-project-context/spec.md` is empty. Commit.
- [ ] T044 Structural greps, recorded in `baseline.md`:
  - `grep -n "ProjectCompile::run" lsp/Run.h lsp/*.cpp` finds the single call;
  - `grep -nE "runCommand|popen|fork\(|exec[lv]p?\(|system\(" ProjectLayout.cpp lsp/*.cpp lsp/*.h` prints nothing;
  - `grep -nE "opendir|\"blang.toml\"" lsp/*.cpp` finds no project rule copy (only calls into `BlangProject::`/`ProjectConfig::`);
  - `grep -n "sharedResolver" lsp/` prints nothing.
- [ ] T045 Remove `/tmp/u3-ref` and any scratch directories. Confirm `git status` is clean.

---

## Dependencies & execution order

- **Phase 1** (T001, T002) runs first.
- **Phase 2**, in the audit order. It blocks every story.
  - **R3:** T003 → T004 → T004b (shared walk + `scanImportsAll`), with T005 and T005b in parallel, then T006 → T007, one commit.
  - **R1:** T008 → T009.
  - **R2:** T010 → T011 → T012 → T013.
  - **T014** runs in parallel after T006.
- **US1 (Phase 3)**:
  - T015 → T016 → T017 → T018 → T019 → T020;
  - T021 is parallel with the above;
  - then T022 → T023 → T024 → T025 → T026;
  - then T027.
- **US2 (Phase 4)** depends on T019/T020. T030 and T031 are parallel, then T028 → T029 → T032 → T033 → T034.
- **US3 (Phase 5)** depends on T015, T018 and T019. T035 → T036 → T037.
- **US4 (Phase 6)** depends on US1.
- **Polish (Phase 7)** comes after all stories. T039, T040 and T041 are parallel, then T042 → T043 → T044 → T045.

## Parallel examples

```text
# Phase 2 (R3)
T003/T004 ProjectLayout + bcc   ∥   T005 ProjectConfig::loadFromText
T014 ProjectLayoutTest.cpp (after T006)

# US1
T021 multifile fixture   ∥   T015–T020 core

# US2
T030 negative fixtures   ∥   T031 lsp_client --server-env

# Polish
T039 CLAUDE.md   ∥   T040 editors/README.md   ∥   T041 security.md
```

## Implementation strategy

1. **Foundation first, in audit order** (R3 → R1 → R2). Each step has its own neutrality proof against `bcc`/`qcc` and U2, so later failures can only come from the new blangd code.
2. **MVP = US1.** Project corpus green, covering the 10 binaries, 6 examples, the multi-file fixture and the 11 xmodule layouts, plus interface byte-parity and the leak lock.
3. **Then US2**, the safety story: one diagnostic per problem, no process. **Then US3** (unsaved buffers, including the generic-body slice) and **US4** (latency).
4. **Converge gate** (workplan U3 (g)): `/speckit-converge` reports `converged`, or two cycles leave only LOW/MEDIUM `unrequested` findings.

## Traceability

| Requirement / SC | Tasks |
|---|---|
| FR-001/002 discovery, membership (R5) | T016, T014(a,b) |
| FR-003 plan (bin/lib, R4) | T019, T024 |
| FR-004 path + fetched git, transitive | T003, T004b, T017, T014(c,d,f), `project_git_warm` (T030/T032) |
| FR-005 interface parity | T018, T025 |
| FR-006 identities | T017, T018, T019, T024 |
| FR-007 one rule, one copy (R3) | T003–T007, T004b, T005b, T017 grep gate, T044 |
| FR-008 no process / network | T003 (no process call), T033, T044, `blangd_no_subprocess` |
| FR-009 one diagnostic, no cascade (R6) | T017, T028, T029, T032 |
| FR-010 never crash / no stderr | T005, T034, T041 |
| FR-011 overlay, including generic slices (R1) | T008, T015, T018, T036 |
| FR-012 no fan-out | T037 |
| FR-013 isolation (R2) | T010–T013 |
| FR-014 ownership | T012, T027 |
| FR-015 project corpus | T021–T026 |
| FR-016 toml diagnostics (R7/R8) | T005, T028, T032 |
| FR-017 goldens (a–d) | T032, T036 |
| FR-018 git shim | T031, T033 |
| FR-019 latency (R10) | T038 |
| FR-020 `parser_no_leak` | T027 |
| FR-021 `qcc`/`bcc` frozen | T002, T007, T009, T013, T043 |
| SC-001 / SC-002 | T024 |
| SC-003 | T032, T033 |
| SC-004 | T036 |
| SC-005 | T038 |
| SC-006 | T027, T042 |
| SC-007 | T007, T009, T013, T024, T043 |
