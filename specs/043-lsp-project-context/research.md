# Research: blangd project context (U3)

Measured on `043-lsp-project-context` @ `285b362`. The base is U2 at `dc39603`, and the binaries were built from `build/` (LLVM). The design audit is [`design-audit-U3.md`](../../docs/epics/modules-v2-lsp/design-audit-U3.md); its §1–§2 are not repeated here.

## R1. The project reference compile, and whether project context changes any U2 corpus file

- **Method.** I copied `examples/` and `test_build/` to a scratch tree and set `XDG_CACHE_HOME` to a scratch cache, so nothing is written into the repo. For each project I ran `bcc build -v <abs dir>` and took the **last** printed `/qcc ` line, which is the final invocation because dependencies build first. I ran that line from the project directory with `--json`, then compared it against the single-file reference (`bcc -v -S main.b`, U2).
- **Finding.**

  | Project | Project ref exit / diagnostics | Single-file ref | Same set? |
  |---|---|---|---|
  | `examples/{calculator,chat,kv,wordfreq,worker_pool}` | 0 / 0 | 0 / 0 | yes |
  | `examples/todo_app` | 0 / 2 (the `unused variable` warnings) | 0 / 2 | yes |
  | `test_build/myapp`, `counterapp`, `usebox` | **0 / 0** | 1 / errors | no (project context needed) |

- **Consequence for the six `examples`.** Every `examples/*/` directory has a `blang.toml` (single-source `bin`, no deps). Under U3 their `main.b` becomes a project member, and both references give the same diagnostic set. They stay in U2's stdlib corpus, where they still pass, and they also join the project corpus.
- **Consequence for the two `test_build` overview files.** These are `test_build/myapp/main.b` and `test_build/counterapp/main.b`. They **leave** the stdlib corpus, because their single-file reference no longer describes what blangd does, and enter the project corpus. U2's `lsp_diag_corpus.py` qualifying rule changes accordingly (D-7). The spec's assumption anticipated this, and SC-001's "zero errors for myapp/counterapp" is confirmed by the project reference exiting 0 with 0 diagnostics.

## R2. Where the project corpus can run (reference needs `llc`)

- **Finding.** `bcc build` builds each library dependency to a `.a` with `llc` (`buildProject`'s lib branch, `bcc.cpp`), and only then reaches the final `qcc` line. A parse-only build (CI's `lsp` job) has no `llc`, so `bcc build` aborts on the first library dependency, **before** the reference line is printed.

  Reconstructing the reference command in the test tool would be a second copy of `bcc`'s rules. Adding a dry-run flag to `bcc` would be a user-visible `bcc` change (epic: stop and ask).
- **Decision.** The project corpus runs where an LLVM toolchain exists:
  - CI's **`build-system`** job, which already installs `llvm-18`, builds `build`/`build-asan` and runs `run_build_tests.sh` with `CI=true`, gains a step `python3 tools/lsp_diag_corpus.py --build-dir build --corpus project --check-list`;
  - `test_lsp.sh` runs the project leg when `$BUILD_DIR/qcc` has LLVM and `llc-18`/`llc` is on `PATH`. Otherwise it prints `SKIP: project corpus (needs llc + an LLVM qcc; run in CI build-system)`;
  - under `CI=true` that skip is a **failure**, mirroring `run_build_tests.sh`'s ASan-leg rule, so the `lsp` job must not reach it (`--no-project-corpus` is passed there).

  This is recorded as a DECISION. The stdlib corpus keeps running in both build modes.

## R3. Shared project helpers (audit R3): what moves and where

`bcc.cpp` contains these process-free helpers:
- `discoverSourceFiles` (:899);
- `resolvePath` (:936);
- `gitCacheRoot` (:956);
- `shortHashHex` (:967);
- the pure *locate* half of `resolveGitDependency` (:990), which covers the pin check, the checkout path and the warm `stat`;
- `mergeDep` (:1081) and the closure fold.

**Decision.** Move these verbatim into a new dependency-free shared unit, **`ProjectLayout.{h,cpp}`**. It links the standard library, POSIX `opendir`/`realpath`/`stat`/`getenv`, and `sha256.c`, which the frontend already links through `blang_sha256`.

`bcc` calls these helpers, so `bcc` and blangd share one copy of each:
- `resolveGitDependency` becomes `locateGitCheckout` (shared) plus the fetch (still in `bcc`, the only `runCommand` user).
- `mergeDep` becomes `BlangProject::mergeByOrigin`.

**Superseded by audit addendum H1:** the whole walk (on-path/KG-8 guard, resolution, always-recurse, closure-then-self fold) is extracted as `BlangProject::walkDependencies`, and both `bcc` and blangd are visitors of it. Neither keeps its own DFS. The per-file import scan is likewise shared as `BlangStdlib::scanImportsAll` (addendum H2).

Neutrality is proven by `run_build_tests.sh` (all legs, including the git-dependency leg and the `.bmod` goldens), plus a `bcc build -v` byte-identity check before and after on all 10 binaries. This follows U1's method.

## R4. `ProjectConfig` error sink and dependency lines (audit R3)

- **Finding.** `ProjectConfig::loadFromFile` writes `"<path>:<line>: parse error: …"` to `cerr` at two sites (`ProjectConfig.cpp:124,179`) and continues. A `Dependency` records no line.
- **Decision.**
  - Add `ProjectConfig::loadFromText( path, text, std::vector<ConfigIssue> *issues )`. `loadFromFile` reads the file and calls it with `issues == nullptr`, which keeps today's `cerr` text byte-identical.
  - Add `int line` to `Dependency` (the toml line of its entry).
  - `ConfigIssue { int line; std::string message; }` carries the same message body.
  - `ProjectConfig.cpp` joins `BLANG_FRONTEND_SOURCES`; it has no dependencies.
  - An open `blang.toml` buffer is read through `loadFromText`, so the overlay wins (FR-011 applied to the manifest, too).

## R5. Emitter source text (audit R1)

- **Finding.** `BmodEmitter::sliceDefinitionSource` reads `loc.file` through a process-global, never-invalidated `gSourceLineCache` (`BmodEmitter.cpp:13`). It is only reached for generic functions and generic struct methods, the verbatim bodies.
- **Decision.**
  - `BmodEmitter::emit( modules, out, scope, const SourceTextProvider *text = nullptr )`, where `using SourceTextProvider = std::function<const std::string *( const std::string &path )>`.
  - With a provider, slicing splits the provided text, without caching across calls. Without one (`qcc`), the existing disk cache path is unchanged.
  - blangd's provider returns exactly the text each module was parsed from: the overlay, or the disk text read once for that compile.
  - **Proof of neutrality.** The `test_files/golden/bmod/` goldens and `run_build_tests.sh` pass, and U1's `--emit-bmod` byte-identity sweep (the xmodule libs plus mathlib) shows zero diffs.

## R6. Isolation (audit R2): one fresh global scope per run

- **Finding.**
  - `injectBmodSymbols` registers dependency namespaces on **`gScope`**, the running resolver's global scope (`ProjectCompile.cpp:105–115`).
  - U2's process-static `sharedResolver()` would therefore accumulate them across reparses and documents.
  - `Resolver`'s constructor and destructor swap the process `gScope` alias LIFO (`Resolver.cpp:12–30`). That breaks if resolvers are owned by results that die in arbitrary order (`Server::mCompiles`).
- **Decision.**
  - Add `Resolver( AliasMode::kManual )`, which neither installs nor restores `gScope`. The default constructor, which `qcc` and the tests use, is unchanged.
  - blangd creates **one fresh manual resolver per `ProjectCompile::run`**: one for the document compile and one per dependency compile. The resolver is owned (`shared_ptr`) next to that run's context.
  - A small RAII guard, `lsp::GlobalAliasScope`, sets `gScope` to that resolver's global for the duration of `run` only, then restores the previous value. This satisfies U1's I3 ("the caller installs the alias").
  - Nothing after the compile reads `gScope`: `AstLocator`, `Server` and Sema were grepped and contain no reads. So the alias need not outlive the run.
  - `ProjectCompile` itself is **unchanged**, so nothing in `qcc` moves.
  - U1's known I6 item closes, because every run's namespaces live on that run's own global scope.
- **The U2 path changes too.** `sharedResolver()` is replaced by the per-run resolver for stdlib/single-file compiles as well, so one rule covers every compile, superseding U2's analyze F2.
  - **Cost.** `createGlobalScope` runs per run. It is small next to the stdlib parse (confirmed at implementation by `lsp_latency`).
  - **Unchanged tests.** `resolver_reuse` and `parser_no_leak` call the two-argument `compileDocument` inside their own default-mode resolver. The manual guard restores their alias, so their assertions are unchanged.

## R7. Unresolvable-dependency presentation (FR-009: one diagnostic, no cascade)

- **Finding.** If a dependency is dropped from the plan, the consumer's `import mathlib;` becomes `unknown module 'mathlib'` (the import pass), and every `mathlib.x(...)` becomes `undefined symbol 'mathlib'`. That is a cascade.
- **Decision.** The consumer is compiled **without** the dependency. blangd publishes the one setup diagnostic and **suppresses consumer diagnostics that are about that module**: those whose message names the unresolved module as a quoted identifier (`'mathlib'`) or qualifier (`'mathlib.`). Every other diagnostic is published.

  This is a presentation filter over the shared compile's output, not a compile rule, so it copies no `bcc`/`qcc` rule. It is pinned by the missing-path and unfetched-git goldens, each of which also carries an unrelated real error that must survive the filter.

  **UNCONFIRMED.** The alternative is to inject an empty stub interface so the import resolves. That would still cascade on member uses, and it would make up an interface `bcc` never emits.
- **Where the setup diagnostic goes.**
  - It is placed at the first `import <name>` in the document. `<name>` is the dependency's `[project]` name when its toml is readable, else its `[deps]` key.
  - Otherwise it goes on the document at 1:1, with the message naming `<toml>:<line>` (spec R8).
  - The import location is taken from the document's own parsed `ImportStatement` (`getLocation()`, matched by `getModuleName()`), which the shared compile already produces. No text scan is added.

## R8. Multi-source project semantics (the sibling-source fixture)

- **Finding.** `bcc build` with stdlib combines **all** root sources in sorted order. The **last** file is the user scope, and earlier ones become namespaced modules (U1 routing). So "a sibling file of the same project" behaves exactly as the shared routing says.
- **Decision.** The new fixture (`test_files/lsp/projects/multifile/`, two sources) is authored so that `bcc build` builds it and its reference exits 0. Its exact cross-file call shape is whatever `bcc` accepts, determined empirically at implementation, with no new rule. The corpus check then proves parity.

  Its reference must be verified to exercise a real cross-file resolution: deleting the callee must make both `bcc` and blangd report an error. That verification is a recorded manual step.

## R9. The `fail/xmodule` fixtures as projects

- **Decision.** Per fixture, the harness synthesizes `<scratch>/lib/{blang.toml,lib.b}` (`[project] name = "lib"`, `type = "lib"`) and `<scratch>/app/{blang.toml,consumer.b}` (`type = "bin"`, `lib = { path = "../lib" }`).
- **Reference.** `qcc --parse-only consumer.b lib.bmod`, where `lib.bmod` is emitted by `qcc --emit-bmod lib.b`, exactly as `run_tests.sh` does.
- **Assertion.** Every **error** in the reference located in `consumer.b` appears in blangd's publish for `consumer.b`, with the same line, column and message. This is SC-002's "same located diagnostic".
- **Why errors only.** blangd compiles the consumer as a binary project (combine plus stdlib) while the reference is non-combine. Extra warnings are tolerated and reported, never errors.
- **Teeth.** Removing the reach-in from a copied fixture must turn the check green-to-red, as a selfcheck in the tool.

## R10. Git fixtures without network

- **Unfetched golden.** `test_files/lsp/projects/gitdep_unfetched/blang.toml` declares `greet = { git = "file:///nonexistent/greet.git", tag = "v1" }`. The golden runs with `server_env XDG_CACHE_HOME=${ROOT}/test_files/lsp/projects/empty_cache`, a committed empty directory with `.keep`, so the cache is deterministically cold and the checkout path in the message is `${ROOT}`-scrubbed.
- **Shim test.** `test_lsp.sh` creates a temp directory holding an executable `git` that touches `<tmp>/git-invoked`. It runs `lsp_client.py --script gitdep_unfetched.lsp.jsonl --server-env PATH=<tmp>:$PATH`, a new driver option, and fails if the marker exists or the one diagnostic is absent.
- **Warm path.** It is exercised by `parser_no_leak`'s project loop and an optional transcript. A fixture checkout is created under a scratch `XDG_CACHE_HOME` by copying a fixture library into `<cache>/blang/git/<name>-<shortHashHex(url@tag)>`, so no git is run. This uses the shared `locateGitCheckout` to compute the directory name.

## R11. Latency and cost

- **Estimate.** `usebox` means 2 dependency compiles (`boxq`, `midx`) and 2 emits, plus the user combine compile (5 base stdlib modules plus `main.b`). U2 measured ~77 ms for the stdlib + todo_app compile, and the dependency compiles are tiny non-combine single-file compiles. The estimate is well under 500 ms.
- **Decision.** There is no cache (D5, R10). A miss is a STOP-and-ask.

## R12. Display names (D4)

No new rendering of foreign names is introduced. Every consumer diagnostic is the shared compile's text, which is identical to `qcc`'s. The new setup and toml texts name modules by their declared `[deps]`/`[project]` names. D4 remains U4's.
