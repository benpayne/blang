# Implementation Plan: blangd stdlib/prelude context + stdlib discovery

**Branch**: `042-lsp-stdlib-context` (stacked on `041-shared-project-compile`, PR #158)

**Date**: 2026-11-26

**Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/042-lsp-stdlib-context/spec.md`

**Epic / unit**: `docs/epics/modules-v2-lsp/` U2

**Covers**: REQ-002, REQ-003, REQ-009 (single-file half)

**Binding record**: `docs/epics/modules-v2/overview.md` D1–D17 (not reopened). Grounded in the epic's `design.md`:
- **D1**: one shared entry point;
- **D3**: discovery precedence;
- **D5**: full recompile, no cache in v1;
- **C1**: one compile, two front ends;
- **C3**: ownership;
- **C4**: no subprocess or network;
- **C5**: diagnostics are about the open document;
- **C7**: transcript determinism.

---

## ⚠ The four constraints that govern every task

1. **No blangd-only rule.** blangd only *assembles* a `CompilePlan`. Each part of the compile comes from one of the U1 shared units:
   - the stdlib set and import scan from `StdlibSelect`;
   - routing, prelude, import diagnostics and Sema from `ProjectCompile::run`;
   - the base-module list from `StdlibSelect`.

   If a task seems to need a rule in `lsp/`, stop and raise a question (overview constraint).
2. **KG-7 ownership** ([041 kg7-resolution-refs.md](../041-shared-project-compile/checklists/kg7-resolution-refs.md)). Since U1, an expression's link to the definition it resolved to is **non-owning**: the defining scope owns the definition. A user AST that resolves `Map` therefore points into the stdlib module's scope **without keeping it alive**.

   The invariant this plan relies on:

   > **Every stdlib module, scope and AST that a document's AST can resolve into MUST be owned by that document's `CompileResult`, either directly through its `CompileContext` or through shared ownership of any cache that holds it, and MUST outlive that result.**

   U2 satisfies the invariant the simplest way: **no stdlib state is reused across reparses** (design D5). Each reparse builds its stdlib modules inside the new `CompileContext`, which the `CompileResult` owns through its existing `shared_ptr<CompileContext>`.

   Any future cache MUST be held through a `shared_ptr` that every `CompileResult` built against it also holds (D-4). Reusing parsed stdlib state any other way is a use-after-free and is forbidden.
3. **No process, no network** (C4). Discovery and compilation only call `access`/`stat`, open files read-only, and `readlink( "/proc/self/exe" )`. The U1 `blangd_no_subprocess` ctest (binary symbol check) keeps passing.
4. **`qcc`/`bcc` frozen** (C2). Two `bcc` helpers move into `StdlibSelect` verbatim (R6, R7), so they now have one shared copy. That move is the unit's only change to `bcc`'s code. U1's byte-identity method re-proves neutrality ([quickstart.md](quickstart.md) §5).

## Summary

blangd compiles every open document in `bcc file.b`'s context:
- the stdlib set chosen by the shared selection over the document's **in-memory** imports;
- combine-mode routing and the prelude;
- the whole-program import diagnostics.

All of it goes through `ProjectCompile::run`. Only diagnostics located in the document are published.

The stdlib directory is chosen once, at `initialize`, by this precedence:
1. `initializationOptions.stdlibPath`;
2. `BLANG_STDLIB_DIR`;
3. `<blangd-dir>/stdlib`.

**The first route that is set wins**, and a directory is usable only if it holds all five base modules. Otherwise every publish starts with one `stdlib-not-found` Warning at 1:1, and the document is compiled single-file exactly as today.

New tests and tools:
- `tools/lsp_diag_corpus.py`, which checks blangd against the reference compile over a committed corpus list;
- `tools/lsp_latency.py`;
- five discovery transcript goldens;
- a `stdlib_locate` ctest;
- a 50+ reparse stdlib loop in `parser_no_leak`.

`test_lsp.sh` runs all of them.

## Technical Context

**Language/Version**: C++17 (house style) for blangd. Python 3 stdlib only for the tools, the same constraint as `tools/lsp_client.py`.

**Primary Dependencies**: none new. The tools use the existing `BlangStdlib` (`StdlibSelect`), `ProjectCompile`, `Resolver`, `DiagnosticEngine` and `lsp::Server`/`Compile`.

**Storage**: none. Stdlib files are read from disk on each reparse.

**Testing**:
- `test_lsp.sh`: transcripts, the `fail/sema` leg, the new corpus leg and the new latency leg;
- `ctest`: `parser_no_leak` extended, new `stdlib_locate`, and the existing `project_compile` and `blangd_no_subprocess`;
- every pre-existing gate (FR-017).

**Target Platform**: Linux (CI), macOS. `/proc/self/exe` with the `argv[0]` fallback, as `bcc` does.

**Project Type**: compiler plus language server.

**Performance Goals**: median `didChange` → `publishDiagnostics` ≤ 500 ms on `examples/todo_app/main.b`. The measured cost is ~0.09 s (R5).

**Constraints**:
- no blangd-only rule;
- KG-7 ownership;
- no subprocess or network;
- strict stdio framing (no stray byte);
- `qcc`/`bcc` frozen.

**Scale/Scope**:
- `lsp/` (~150 changed lines);
- `StdlibSelect` (+2 shared helpers);
- `bcc.cpp` (−1 helper body, call sites switched);
- 2 new Python tools (~300 lines);
- `tools/lsp_client.py` (+~30 lines);
- `test_lsp.sh` (+2 legs);
- 1 new ctest;
- 5 new goldens plus 1 new fixture source;
- the corpus list (~185 entries).

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (below).*

| Principle / Gate | Status | How this plan satisfies it |
|---|---|---|
| I. One Right Way | PASS | One stdlib-selection rule, one base list and one exe-dir helper, all shared. Adds no language surface. |
| II. Test-Gated Changes | PASS | Every behavior is pinned: the corpus check (SC-001), 5 transcript goldens (SC-002), latency (SC-003), the leak loop (SC-004) and `stdlib_locate`. All prior gates stay green. |
| III. Reject, Don't Coerce | PASS | A misconfiguration is a located Warning, never a silent substitute stdlib (no fall-through). |
| IV. Memory/Thread Safety Verified | PASS | The KG-7 invariant is stated (constraint 2). There is no cache. `parser_no_leak` gains a ≥ 50-reparse stdlib loop under LSan, and blangd under ASan is exercised by the loop. |
| V. House Style | PASS | Existing conventions for C++ and the python3-stdlib tools. |
| VI. Principled Design | PASS | This plan is the design artifact. Per evaluation.md, U2 gets a spec audit, code review and **security review** (paths from `initializationOptions`/env). The design is a general seam: U3 reuses the same plan assembly with project inputs. |
| Gate 1 both build configs | PASS | blangd never links LLVM. The `bcc` helper move compiles in both builds. The corpus check runs in **both** build modes: a parse-only `qcc` given the printed reference line produces byte-identical stderr and exit code to the LLVM one on all 178 candidates (R13). CI's `lsp` job is parse-only, so it gains the `qcc` and `bcc` build targets. |
| Gate 5 docs | PASS | `CLAUDE.md` gets entries for the new tools and files. `editors/README.md` gets a stdlib-discovery section (`stdlibPath`, `BLANG_STDLIB_DIR`); the epic's docs check is verified in U5. |
| Gate 7 security | PASS (review planned) | Path input is untrusted. It is only `access`/`stat`/read. File names come from the fixed base list or import identifiers (`[A-Za-z0-9_]+`), so there is no traversal and no execution (D-2). |

No violations, so Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/042-lsp-stdlib-context/
├── spec.md          # /speckit-specify (3b093f6)
├── plan.md          # this file
├── research.md      # Phase 0: R1–R12 (measurements + decisions)
├── data-model.md    # Phase 1: StdlibLocation, DocumentCompilePlan, CompileResult, corpus entry
├── contracts/
│   ├── stdlib-discovery.md     # initializationOptions/env/relative routes, usability, the warning
│   └── lsp-test-tools.md       # lsp_diag_corpus.py / lsp_latency.py / lsp_client.py additions
├── quickstart.md    # Phase 1: validation run guide
├── checklists/requirements.md
└── tasks.md         # Phase 2 (/speckit-tasks; not created here)
```

### Source Code (repository root)

```text
StdlibSelect.h/.cpp        EDIT  + baseModuleNames() (R7) + executableDir(argv0) (R6, moved verbatim from bcc)
bcc.cpp                    EDIT  getExeDir body removed; calls BlangStdlib::executableDir (neutral move)
lsp/StdlibLocation.h/.cpp  NEW   StdlibLocation + locateStdlib(initOpt, env, exeDir) (pure; D-2)
lsp/Compile.h/.cpp         EDIT  compileDocument(path, text, const StdlibLocation &) overload (D-3);
                                 the existing 2-arg overload = unchanged single-file behavior (tests keep it)
lsp/Server.h/.cpp          EDIT  exeDir ctor arg; resolve StdlibLocation at `initialize`; pass it to compileDocument
lsp/blangd_main.cpp        EDIT  main(argc, argv) → Server(cin, cout, BlangStdlib::executableDir(argv[0]))
lsp/StdlibLocateTest.cpp   NEW   ctest `stdlib_locate` (precedence, empty-is-unset, usability, warning text)
ParserLeakTest.cpp         EDIT  + stdlib-context loop, ≥ 50 reparses (existing assertions untouched)
CMakeLists.txt             EDIT  StdlibLocation.cpp in blangd/test targets; stdlibLocateTest + add_test;
                                 BLANG_SOURCE_STDLIB_DIR define for parserLeakTest
tools/lsp_client.py        EDIT  scrub BLANG_STDLIB_DIR; `server_env` step; `--stdlib-path` for --sema-fixture
tools/lsp_diag_corpus.py   NEW   corpus parity check (R10)
tools/lsp_latency.py       NEW   latency floor (R11)
test_files/lsp/corpus.txt  NEW   committed corpus list (`stdlib <path>` lines)
test_files/lsp/sema_no_prelude.txt  NEW  fail/sema fixtures routed through the not-found route (buffer_without_prelude.b)
test_files/lsp/src/prelude_use.b    NEW  zero-import Map/Set/Buffer user (fixture source)
test_files/lsp/stdlib_{initopts,env,relative,missing,missing_env}.lsp.jsonl + .expected.out   NEW goldens
test_lsp.sh                EDIT  sema leg honors sema_no_prelude.txt; corpus leg; latency leg (sanitizer SKIP)
.github/workflows/ci.yml   EDIT  lsp job (parse-only) also builds the qcc + bcc targets the corpus leg needs (R13)
CLAUDE.md, editors/README.md  EDIT  docs (Gate 5)
```

**Structure decision.** Discovery stays in `lsp/`. It is blangd's own configuration surface: `bcc` has no `initializationOptions` and no env route, and design D3 scopes it to blangd. What discovery *checks against* (the base list) and how it finds its own directory (exe-dir) are shared helpers. So the editor cannot disagree with `bcc` about which files are the base set or where `<exe>/stdlib` is.

## Design

### D-1. Shared helpers moved into `StdlibSelect` (R6, R7)

```cpp
namespace BlangStdlib {
	// The modules every program is combined with, in dependency-safe order.
	const std::vector<std::string> &baseModuleNames();   // {sys, buffer, collections, fs, net}
	// Directory of the running executable: readlink(/proc/self/exe), else argv0's dir, else ".".
	std::string executableDir( const char *argv0 );       // verbatim bcc getExeDir
}
```

- `selectStdlibFiles` iterates `baseModuleNames()` instead of its literal list. The names and order are the same, so this is neutral.
- `bcc.cpp`'s `getExeDir` body is deleted, and its callers call `BlangStdlib::executableDir`. `<unistd.h>` is already included.

### D-2. `lsp::StdlibLocation` and `locateStdlib` (FR-006/007/009)

```cpp
namespace lsp {
	struct StdlibLocation {
		enum class Route { kInitOptions, kEnv, kRelative };
		Route route;
		std::string path;      // as given (route 1/2) or exeDir + "/stdlib" (route 3); never normalized
		bool usable = false;
		std::string missing;   // first absent base module ("<name>.b"), or "directory" — for the warning
	};
	// Pure: precedence + usability. `initOpt`/`env` empty == unset. No fall-through.
	StdlibLocation locateStdlib( const std::string &initOpt, const std::string &env,
		const std::string &exeDir );
	// The fixed warning text (research R8), and the route's display name.
	std::string stdlibNotFoundMessage( const StdlibLocation & );
}
```

**Selection.**
- `route = !initOpt.empty() ? kInitOptions : !env.empty() ? kEnv : kRelative`.
- The chosen route's path is evaluated **only**; no other route is consulted afterwards.

**Usability.**
- `stat( path )` must be a directory with read access; else `missing = "directory"`.
- Then `access( path + "/" + n + ".b", R_OK )` must succeed for each `n` in `baseModuleNames()` in order. The first failure sets `missing = n + ".b"`.

**Security (Gate 7).**
- The path is used only as a prefix for `stat`/`access`/read-only opens.
- The appended names are the fixed base list or `scanImports` identifiers (the `isalnum || '_'` run). They contain no `/` or `.`, so they cannot climb out of the directory.
- Symlinks are followed read-only, as `bcc` does.
- No execution, no network.
- Oversized or garbage stdlib files are handled exactly as `qcc` handles them (lexer/parser located errors). They are not a crash surface beyond today's.

### D-3. The document compile (FR-001–005, FR-008)

New overload `CompileResult compileDocument( const std::string &path, const std::string &text, const StdlibLocation &stdlib )`. The existing two-argument overload stays: it is today's single-file plan, unchanged, and `ResolverReuseTest`/`ParserLeakTest`'s existing cases use it.

```text
gScope = sResolver.globalScope();                     // caller installs the alias (U1 I3)
DiagnosticEngine engine;                              // fresh per reparse, never finish()ed
plan.identity = kNone                                 // FR-005 (U1 R4)
if stdlib.usable:
    plan.combine = true
    for f in BlangStdlib::selectStdlibFiles( stdlib.path, BlangStdlib::scanImports( text ) ):
        plan.inputs += { f, classifyInput( f ) }      // disk inputs, read as qcc reads them
    plan.inputs += { path, kSource, inMemory = true, text }   // user input = last
else:
    plan = today's single-file plan (combine = false, one in-memory input)
result.context = ProjectCompile::run( plan, sResolver, engine )
user = result.context->records().back()               // the user record is always last (no interfaces)
result.module = user.module;  result.fileScope = user.scope
result.diagnostics = engine.diagnostics() filtered to { d : d.location.file == path || d.location.file.empty() }   // C5 / FR-004
if !stdlib.usable: result.diagnostics.insert( begin, stdlib-not-found Warning @1:1, code "stdlib-not-found" )
```

- **Why `records().back()`.** In combine mode the user input is the last input after the interfaces-first reorder (U1 I5), and a U2 plan has no interface inputs.
- **The filter.** It keys on the exact path string the plan gave the user input, so every user-document diagnostic carries that string.
- **Compiles are equivalent.** The reference `qcc` compiles the same stdlib paths and the same user text, so the frontend produces the same diagnostic set (C1). Only the user path string and the read mode differ.
- **Unchanged callers.** `Server::definition`/`hover`/`documentSymbol` keep reading `result.module` (the user module) and `fileScope` (now the combine user scope). A definition into a stdlib symbol resolves to the stdlib file through the existing `pathToUri( target.file )`. That is not promised or tested until U4.

### D-4. Ownership (constraint 2, FR-012, C3)

- **What a `CompileResult` owns.** It owns `std::shared_ptr<CompileContext>`, which owns:
  - the prelude scope and combine scope;
  - every namespace scope, adopted by the combine scope through `setParent`;
  - every stdlib and user `Module`.

  `result.module` and `result.fileScope` co-own the user module and user scope. The stdlib modules the user AST resolves into are owned **only** through `context`, so `context` must live as long as `module` does.

  `Server::mCompiles[ uri ] = compileDocument( … )` replaces the whole result. The old context is released after the new one is built: two contexts at peak, then one.
- **Nothing on the global scope.** Combine mode adds namespaces to the context-owned combine scope, not the `Resolver`'s global scope. U1's I6 global adoption happens only for `.bmod` inputs, and U2 has none. So the process global scope does not grow across reparses.
- **Future caching rule (normative, recorded for the reviewer).**
  - A parsed-stdlib cache MUST be a `std::shared_ptr<StdlibCache>`.
  - Every `CompileResult` whose user AST was resolved against it MUST store a copy of that `shared_ptr`.
  - The cache MUST be released before the `static Resolver`.
  - Adding one is gated on a measured REQ-009 miss plus a question to the manager (sequencing notes). U2 adds none.
- **Lock.** `parser_no_leak` gains a loop: 50 iterations of `compileDocument( "leak_stdlib.b", <prelude + import net; source>, locateStdlib( BLANG_SOURCE_STDLIB_DIR, "", "" ) )`, dropping each result. Under `build-asan`, LeakSanitizer fails the process on any leak. The source uses `Map`/`Set`/`Buffer` so the user AST really resolves into the stdlib. The existing assertions are untouched.

### D-5. Server wiring (FR-006, FR-008, FR-014)

- **Construction.** `Server( std::istream &, std::ostream &, std::string exeDir = "." )`. `blangd_main.cpp` passes `BlangStdlib::executableDir( argv[0] )`.
- **At `initialize`.**
  - `initOpt` = `params.initializationOptions.stdlibPath` if it is a string, else empty;
  - `env` = `getenv( "BLANG_STDLIB_DIR" )` or empty;
  - `mStdlib = locateStdlib( initOpt, env, exeDir )`.

  This is fixed for the session. A non-string `stdlibPath` counts as unset, and the unusable result is visible through the warning.
- **Publishing.** `publishDiagnostics` calls `compileDocument( uriToPath( uri ), text, mStdlib )`. The JSON conversion is unchanged. The fallback warning is an ordinary `Diagnostic` with severity Warning, rendered as severity 2.

### D-6. Test harness and tools (FR-010, FR-011, FR-014, FR-016)

`tools/lsp_client.py`:
- **(a)** Always delete `BLANG_STDLIB_DIR` from the child environment.
- **(b)** Parse the script before spawning. If it contains `{"server_env": {...}}` steps, set those (`${ROOT}`-substituted) in the child environment. The step is otherwise a no-op.
- **(c)** `--sema-fixture` accepts `--stdlib-path DIR`, sent as `initializationOptions.stdlibPath`.

The goldens are in [contracts/lsp-test-tools.md](contracts/lsp-test-tools.md). All five open `test_files/lsp/src/prelude_use.b`:

| Golden | Setup | Expected | Proves |
|---|---|---|---|
| `stdlib_initopts` | `stdlibPath=${ROOT}/stdlib`, env=`${ROOT}/test_files/lsp/src` (unusable) | empty diagnostics | initOptions beats a set env |
| `stdlib_env` | env=`${ROOT}/stdlib` | empty | env route works |
| `stdlib_missing_env` | env=`${ROOT}/test_files/lsp/src` | one warning naming `BLANG_STDLIB_DIR`, then single-file errors | env beats the relative default (no fall-through) |
| `stdlib_relative` | nothing set | empty | `<blangd-dir>/stdlib` route |
| `stdlib_missing` | `stdlibPath=${ROOT}/test_files/lsp/src` | warning naming `initializationOptions.stdlibPath` + `missing sys.b`, then today's single-file errors (`unknown type 'Map'`, …) | fallback behavior |

The `stdlib_relative` route uses `BUILD_DIR`'s own `stdlib/` copy, as built by CMake.

`test_lsp.sh`:
- **Sema leg.** A fixture listed in `test_files/lsp/sema_no_prelude.txt` is run with `--stdlib-path "$SCRIPT_DIR/test_files/lsp/src"`; the rest use the default. Same assertions as today.
- **Corpus leg.** `python3 tools/lsp_diag_corpus.py --build-dir "$BUILD_DIR" --root "$SCRIPT_DIR" --corpus stdlib --check-list` adds one TOTAL/FAIL entry. It runs in both build modes (R13), and fails loudly, never skips, when `$BUILD_DIR/bcc` or `$BUILD_DIR/qcc` is missing.
- **Latency leg.** `python3 tools/lsp_latency.py --server "$BLANGD" --root "$SCRIPT_DIR" examples/todo_app/main.b`. It is skipped with a notice under a sanitizer build (R11).
- **Unchanged.** `--update-goldens` and `--selfcheck` are unchanged. The new legs are excluded under `--update-goldens`, as the sema leg is today.

The corpus list `test_files/lsp/corpus.txt` is generated once by `lsp_diag_corpus.py --print-qualifying`, then committed and hand-reviewed. Each line is `stdlib <repo-relative path>`.

## Verification protocol

Run at each phase checkpoint:

1. `./test_lsp.sh`:
   - all pre-existing goldens are unchanged (R3 predicts zero diffs; any diff gets a per-file justification or a STOP if it is text-only, per FR-015);
   - the 5 new goldens pass;
   - the sema leg passes (58, with `buffer_without_prelude` via the fallback route);
   - the corpus and latency legs are green.
2. `./test_lsp.sh --selfcheck` prints `SELFCHECK: OK`.
3. ctest:
   - `ctest --test-dir build` and `build-asan`: 86/86 (85 + `stdlib_locate`);
   - `parser_no_leak` LSan-clean with the loop;
   - `blangd_no_subprocess` passes, and its teeth leg still catches `bcc`.
4. `run_tests.sh` in all three build dirs; `test_codegen.sh` with `--leak-check` and `--selfcheck`; `run_build_tests.sh`; the reach-in gate; examples; demos. All green with no golden edits.
5. `qcc`/`bcc` neutrality for the D-1 move: re-run U1's byte-identity method for `bcc -v -S` on the six overview files and `test_build/run_build_tests.sh`. Since `getExeDir` and the base list only moved, any diff is a bug.

## Phasing (for /speckit-tasks)

1. **D-1**, the shared helpers, then a checkpoint (gates 4 and 5).
2. **D-2**, `StdlibLocation` plus the `stdlib_locate` ctest.
3. **D-3 + D-5**, compile and server wiring, then a checkpoint (gates 1–3). The pre-existing goldens must still be green before any new golden is added.
4. **D-6**, the harness additions and the five goldens plus the sema routing.
5. **D-6**, the corpus tool, list and leg.
6. **D-6**, the latency tool and leg.
7. **D-4**, the leak loop, then a checkpoint under `build-asan`.
8. **Docs**: `CLAUDE.md` and `editors/README.md`.
9. **Final**: the full verification protocol.

## Assumptions carried from the spec (UNCONFIRMED decisions, unchanged)

The plan relies on these spec decisions; each has a `DECISION:` record from the specify turn:
- no fall-through;
- "usable" means all five base modules;
- the warning is placed at 1:1, is repeated on every publish, and uses the R8 text;
- `buffer_without_prelude` runs via the fallback route.

If the PM overturns one, these parts of the plan change: D-2's selection or usability rule, the R8 text, `stdlib_missing*` setup, and `sema_no_prelude.txt`. No other part is affected.

## Constitution re-check (post-design)

All PASS, unchanged.
- No rule was added to `lsp/`; the plan only assembles inputs for the shared units.
- Two helpers moved to shared code; no logic copies were added.
- The ownership invariant is explicit and lock-tested.
- The input surface (a path) is read-only and confined.

## Complexity Tracking

No constitution violations to justify.
