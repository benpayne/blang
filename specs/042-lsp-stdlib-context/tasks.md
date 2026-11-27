---

description: "Task list for U2 — blangd stdlib/prelude context + stdlib discovery"
---

# Tasks: blangd stdlib/prelude context + stdlib discovery

**Input**: Design documents from `specs/042-lsp-stdlib-context/`:
- [plan.md](plan.md) (D-1 to D-6, verification protocol);
- [spec.md](spec.md);
- [research.md](research.md) (R1 to R13);
- [data-model.md](data-model.md);
- [contracts/stdlib-discovery.md](contracts/stdlib-discovery.md);
- [contracts/lsp-test-tools.md](contracts/lsp-test-tools.md);
- [quickstart.md](quickstart.md).

**Tests**: These are REQUIRED. The spec names them:
- the corpus check (FR-010);
- the latency tool (FR-011);
- the `parser_no_leak` loop (FR-013);
- the 5 discovery goldens (FR-014);
- the `fail/sema` leg routing (FR-016).

The plan adds the `stdlib_locate` ctest. Every behavior task below is backed by one of these committed tests.

**Organization**: by user story. US1 (P1) is the stdlib context plus the corpus parity check. US2 (P2) is the discovery routes and their goldens. US3 (P2) is the latency floor. The `StdlibLocation` resolver and the server's `initialize` wiring are shared by all three stories, so they are Foundational.

## ⚠ Standing rules for EVERY task (plan constraints 1–4)

1. **No blangd-only rule.** `lsp/` code may only assemble a `CompilePlan`. The stdlib set, the import scan and the base-module list come from `StdlibSelect`. Routing, prelude, import diagnostics and Sema come from `ProjectCompile::run`. A task that seems to need a rule in `lsp/` is a STOP: raise it as a `BLOCKER:` question.
2. **KG-7 ownership.**
   - Every stdlib module, scope and AST a document's AST can resolve into MUST be owned by that document's `CompileResult` and MUST outlive it. This is the invariant from [041 kg7-resolution-refs.md](../041-shared-project-compile/checklists/kg7-resolution-refs.md).
   - No stdlib state is reused across reparses in this unit.
   - Any cache MUST be a `shared_ptr` held by every result built against it. Adding a cache is a STOP-and-ask.
3. **No process, no network.** Use only `stat`, `access`, read-only opens and `readlink`. The `blangd_no_subprocess` ctest must keep passing.
4. **`qcc`/`bcc` frozen.** Zero `qcc`/`bcc` golden or diagnostic-text change.
   - After every commit, `git diff --name-only 041-shared-project-compile...HEAD -- test_files/` (U1's tip `5d9b7b6`; analyze F1) may show only *new* files: `test_files/lsp/stdlib_*` (including `stdlib_didchange_import`), `test_files/lsp/src/prelude_use.b`, `test_files/lsp/src/timer_use.b`, `test_files/lsp/corpus.txt` and `test_files/lsp/sema_no_prelude.txt`.
   - A pre-existing LSP golden diff needs a per-file justification. A text-only diff is a STOP (FR-015).
5. House style: tabs, spaces inside parentheses, Allman braces, `m` members. The Python tools use the stdlib only.

## Format: `[ID] [P?] [Story] Description`

---

## Phase 1: Setup (baseline)

- [ ] T001 Confirm the starting state and record it in `specs/042-lsp-stdlib-context/checklists/baseline.md`:
  - the current branch is `042-lsp-stdlib-context`;
  - `git merge-base --is-ancestor 5d9b7b6 HEAD` succeeds (U1 is in the base);
  - the HEAD SHA.
- [ ] T002 Run the pre-unit gate list on the unchanged HEAD and record the counts in `specs/042-lsp-stdlib-context/checklists/baseline.md`. Report any pre-existing red as a `BLOCKER:`/`DECISION:` line and do not absorb it. The gates and expected counts are:
  - `./run_tests.sh` (251/0), `BUILD_DIR=build-nollvm ./run_tests.sh` (244/0) and `BUILD_DIR=build-asan ./run_tests.sh` (251/0);
  - `./test_codegen.sh` (172/0), `--leak-check` (Leaks: 0) and `--selfcheck`;
  - `test_build/run_build_tests.sh`;
  - `ctest` in `build`, `build-asan` and `build-nollvm` (85/85 each);
  - `./test_lsp.sh` (70/0) and `--selfcheck`.
- [ ] T003 Capture the pre-unit `bcc` reference for the D-1 neutrality check:
  - copy `build/bcc` and `build/qcc` to `/tmp/u2-ref/` (scratch, not committed);
  - for each of the six overview files, save the `bcc -v -S <file> -o /tmp/u2-ref/<n>.ll` stdout, stderr and exit code.

**Checkpoint**: baseline recorded.

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the shared helpers (D-1), the pure resolver (D-2) and the server's session-level resolution (D-5). Every story depends on these. None of them changes `qcc`/`bcc` behavior.

### D-1: the two `bcc` helpers move verbatim into `StdlibSelect` (R6, R7)

- [ ] T004 In `StdlibSelect.h` and `StdlibSelect.cpp`, add `const std::vector<std::string> &BlangStdlib::baseModuleNames()`, returning a function-static `{ "sys", "buffer", "collections", "fs", "net" }`. Change `selectStdlibFiles` to iterate it in place of its literal `{ "sys", "buffer", "collections", "fs", "net" }` initializer list. The names and order must stay the same.
- [ ] T005 Move `bcc.cpp`'s `static string getExeDir( const char *argv0 )` (L204–L225) verbatim into `StdlibSelect.cpp` as `std::string BlangStdlib::executableDir( const char *argv0 )`, declared in `StdlibSelect.h`:
  - it does `readlink( "/proc/self/exe" )`, falls back to `argv0`'s directory, then to `"."`;
  - delete the `bcc.cpp` body;
  - switch its two callers (L783 `runMigrate`, L1556 `main`) to `BlangStdlib::executableDir( argv[0] )`;
  - keep the comment explaining the fallback.
- [ ] T006 D-1 checkpoint:
  - rebuild `build`, `build-nollvm` and `build-asan`;
  - re-run the T003 commands with the new `bcc`/`qcc` and `cmp` against `/tmp/u2-ref/`, normalizing the build-dir prefix as U1 did; each must be identical;
  - `test_build/run_build_tests.sh` passes and `ctest --test-dir build -R project_compile` passes;
  - `grep -n "getExeDir" bcc.cpp` prints nothing;
  - commit.

### D-2: `lsp::StdlibLocation` + `stdlib_locate` ctest (FR-006/007/009)

- [ ] T007 [P] Create `lsp/StdlibLocation.h` and `lsp/StdlibLocation.cpp` exactly as in plan D-2 and [contracts/stdlib-discovery.md](contracts/stdlib-discovery.md).
  - **`struct StdlibLocation`**: `Route { kInitOptions, kEnv, kRelative }`, `path`, `usable`, `missing`.
  - **`locateStdlib( initOpt, env, exeDir )`**:
    - the first non-empty value of `initOpt`, then `env`, is selected; otherwise `exeDir + "/stdlib"`;
    - **only** the selected path is evaluated (no fall-through);
    - `stat` must report a readable directory, else `missing = "directory"`;
    - then `access( path + "/" + n + ".b", R_OK )` runs for each `n` in `BlangStdlib::baseModuleNames()`, and the first failure sets `missing = n + ".b"`;
    - `usable` is true iff `missing` stays empty.
  - **`routeName( Route )`** returns `initializationOptions.stdlibPath`, `BLANG_STDLIB_DIR` or `blangd-relative`.
  - **`stdlibNotFoundMessage( loc )`** returns exactly the research R8 / contract text.
  - Includes: `<sys/stat.h>` and `<unistd.h>` only. No process or network call.
- [ ] T008 [P] Create `lsp/StdlibLocateTest.cpp`, an assert harness that never compiles out under NDEBUG, in the `ProjectCompileTest.cpp` style. It takes `argv[1] = <source stdlib dir>` and `argv[2] = <an existing dir without sys.b>` (the test passes `<src>/test_files/lsp/src`). It asserts:
  - **(a)** precedence: a non-empty initOpt wins over env and the relative route; an empty initOpt falls to env; empty initOpt and env fall to `exeDir + "/stdlib"`;
  - **(b)** no fall-through: a set but unusable initOpt yields `route == kInitOptions, usable == false` even when env is usable;
  - **(c)** usability: the source stdlib is usable; the no-`sys.b` dir gives `missing == "sys.b"`; a nonexistent path and a regular file each give `missing == "directory"`;
  - **(d)** a temp dir holding only `sys.b` and `buffer.b` gives `missing == "collections.b"` (base order); create it under `/tmp` with `mkdtemp` and clean it up;
  - **(e)** the exact message text for one unusable case.

  Print `StdlibLocateTest: OK`.
- [ ] T009 In `CMakeLists.txt`:
  - add `lsp/StdlibLocation.cpp` to the `blangd` target and to `resolverReuseTest`/`parserLeakTest`, which link `lsp/Compile.cpp`;
  - add `add_executable( stdlibLocateTest lsp/StdlibLocateTest.cpp lsp/StdlibLocation.cpp StdlibSelect.cpp )` (CXX 17, include dir) and `add_test( NAME stdlib_locate COMMAND stdlibLocateTest ${CMAKE_CURRENT_SOURCE_DIR}/stdlib ${CMAKE_CURRENT_SOURCE_DIR}/test_files/lsp/src )`;
  - build all three trees. `ctest -R stdlib_locate` passes in each, and `ctest` reaches 86/86.

### D-5 (part 1): the server resolves the location once, at `initialize`

- [ ] T010 In `lsp/Server.h` and `lsp/Server.cpp`:
  - add the constructor `Server( std::istream &, std::ostream &, std::string exeDir = "." )` and members `std::string mExeDir; StdlibLocation mStdlib;`;
  - in `handleRequest`'s `initialize` branch, before replying:
    - read `params.get( "initializationOptions" ).get( "stdlibPath" )` and use it only if it is a string; a non-string or a missing value is empty;
    - read `getenv( "BLANG_STDLIB_DIR" )`, treating null as empty;
    - set `mStdlib = locateStdlib( initOpt, env, mExeDir )`.

  Check how `Json::get` behaves on a missing or non-object key in `lsp/Json.h`, and guard accordingly so a malformed `initializationOptions` never throws.

  In `lsp/blangd_main.cpp`, change to `int main( int argc, char *argv[] )` and construct `lsp::Server server( std::cin, std::cout, BlangStdlib::executableDir( argc > 0 ? argv[0] : "" ) )`, including `../StdlibSelect.h`. Add `StdlibSelect.cpp` to the `blangd` target if it is not already linked through `BLANG_FRONTEND_SOURCES`; it is, since U1, so verify and do not duplicate it.
- [ ] T011 Foundational checkpoint:
  - `./test_lsp.sh` stays 70/0, because compile behavior is still unchanged and `mStdlib` is unused so far;
  - `ctest` is 86/86 in all three trees, with `blangd_no_subprocess` passing;
  - commit.

**Checkpoint**: the shared helpers are in place, and every blangd session holds a resolved `StdlibLocation`.

---

## Phase 3: User Story 1 — Correct code is not flagged as broken (Priority: P1) 🎯 MVP

**Goal**: every open document compiles in `bcc file.b`'s stdlib/prelude context through `ProjectCompile::run`, and blangd publishes exactly the reference compile's diagnostics for that document (FR-001 to FR-005, FR-010, FR-012, FR-013, FR-016).

**Independent Test**: `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus stdlib --check-list` reports every file PASS, as does the same command with `--build-dir build-nollvm`.

### Compile in context (D-3)

- [ ] T012 [US1] In `lsp/Compile.h` and `lsp/Compile.cpp`:
  - add the overload `CompileResult compileDocument( const std::string &path, const std::string &text, const StdlibLocation &stdlib )`, which implements plan D-3 exactly:
    - `gScope = sResolver.globalScope();`;
    - a fresh `DiagnosticEngine`;
    - `identity = kNone`;
    - **if `stdlib.usable`**: `combine = true`, then one disk `CompileInput { f, classifyInput( f ) }` per `BlangStdlib::selectStdlibFiles( stdlib.path, BlangStdlib::scanImports( text ) )`, then the in-memory document input last;
    - **else**: today's single-file plan;
    - `result.context = ProjectCompile::run( … )`;
    - `module`/`fileScope` come from `result.context->records().back()`;
    - the diagnostics are filtered to `location.file == path || location.file.empty()`;
    - when the stdlib is not usable, insert at the front a `Diagnostic { Severity::Warning, {path, 1, 1}, stdlibNotFoundMessage( stdlib ), code "stdlib-not-found" }`.
  - keep the existing two-argument overload byte-for-byte in behavior, so tests that call it are unaffected. Factor the shared tail (run → result) into one static helper so the two overloads share code instead of duplicating it.
  - **One shared Resolver (analyze F2).** Both overloads MUST use the single function-local `static Resolver` reached through one helper (e.g. `static Resolver &sharedResolver()`). Never declare a second `static Resolver`: two would fight over the `gScope` alias.
  - **Destruction order (analyze F4, KG-7).** Declare `std::shared_ptr<QLang::CompileContext> context` as the **first** member of `CompileResult`, so it is destroyed **last**. The `module`/`fileScope` co-owners are then released first, and the stdlib modules that the user AST's non-owning links point into outlive them. The comment cites KG-7. T024's ASan loop locks this.
  - update the header comment: documents now compile in stdlib context; the KG-7 invariant (standing rule 2) is stated verbatim at `CompileResult::context`.
- [ ] T013 [US1] In `lsp/Server.cpp` `publishDiagnostics`, call `compileDocument( uriToPath( uri ), text, mStdlib )`. There is no other change: the JSON rendering already maps Warning to severity 2.
- [ ] T014 [US1] Run `./test_lsp.sh`. Per research R3, all 70 pre-existing transcripts must still match **without regeneration**.
  - If a transcript differs, follow FR-015: a diff caused by prelude context gets a per-file justification line recorded in `specs/042-lsp-stdlib-context/checklists/golden-diffs.md`; a text-only diff is a STOP (`BLOCKER:`).
  - Expect the `fail/sema` leg to fail **only** `buffer_without_prelude.b` (research R4); T016/T017 fix that by routing it.

### `fail/sema` leg routing (FR-016)

- [ ] T015 [P] [US1] In `tools/lsp_client.py`:
  - **(a)** spawn the server with `env = dict( os.environ )` minus `BLANG_STDLIB_DIR`;
  - **(b)** read and parse the whole `--script` into a step list **before** constructing `Driver`. Collect every `{"server_env": {...}}` step (`${ROOT}`-substituted) into the child environment, then replay the steps; `server_env` is a no-op during replay;
  - **(c)** add `--stdlib-path DIR`, valid only with `--sema-fixture` (`ap.error` otherwise). When given, the sema-fixture `initialize` params carry `"initializationOptions": {"stdlibPath": DIR}`;
  - **(d)** update the module docstring's step list.

  Strict framing, `${ROOT}` scrubbing and the exit codes stay unchanged (contract [lsp-test-tools.md](contracts/lsp-test-tools.md)).
- [ ] T016 [US1] Create `test_files/lsp/sema_no_prelude.txt`. It holds a header comment (why: the fixture asserts no-prelude behavior, research R4) and one line, `test_files/fail/sema/buffer_without_prelude.b`.

  In `test_lsp.sh`'s `fail/sema` leg, when the fixture's repo-relative path is listed in that file (ignoring `#` and blank lines), add `--stdlib-path "$SCRIPT_DIR/test_files/lsp/src"` to the `lsp_client.py --sema-fixture` call. All assertions stay as they are. No fixture or `.expected` file is edited.
- [ ] T017 [US1] Run `./test_lsp.sh`. It must report 70/0 plus all 58 sema fixtures green, with `buffer_without_prelude.b` rejected via the not-found route; its output has the warning first and then the `Buffer` error that matches its pattern. Commit.

### Corpus parity check (FR-010, R10, R13)

- [ ] T018 [US1] Create `tools/lsp_diag_corpus.py` (python3 stdlib only, executable) per [contracts/lsp-test-tools.md](contracts/lsp-test-tools.md).
  - **Options**: `--build-dir`, `--root`, `--corpus`, `--list` (default `ROOT/test_files/lsp/corpus.txt`), `--check-list`, `--print-qualifying`, `--only`.
  - **Exit 2** if `DIR/blangd`, `DIR/bcc` or `DIR/qcc` is missing.
  - **Reference compile**:
    - run `bcc -v -S ROOT/<path> -o <tmpdir>/r.ll` with cwd set to a `tempfile.mkdtemp()` directory;
    - take the first output line containing `/qcc `, `shlex.split` it, append `--json`, and run it;
    - parse stderr as the JSON diagnostic array (empty stderr means `[]`);
    - record the exit code.
  - **Premise**: every entry's `file` equals the path passed to `bcc`; otherwise FAIL `<path>: reference diagnostic outside the document`.
  - **blangd side**:
    - one session with `BLANG_STDLIB_DIR` removed from the environment and the default relative route;
    - `initialize`/`initialized`, then per file `didOpen` with `uri = file://ROOT/<path>` and the on-disk text;
    - wait for **that uri's** `publishDiagnostics`, then `didClose`, then wait for the clearing publish;
    - at the end, `shutdown`/`exit`.
    - Reuse the `lsp_client.py` framing by importing its `Driver` class; do not copy the framing code. If `lsp_client.py` needs a small refactor to be importable, keep it behavior-neutral and re-run `test_lsp.sh`.
  - **Compare** sorted multisets of `(severity, line, col, message)`, mapping LSP to 1-based. Print `PASS <path> (<n> diagnostics)` or `FAIL <path>: <first missing/extra diagnostic>`, then a summary.
  - **`--print-qualifying`** prints the qualifying list: the six overview files, `examples/*/main.b`, and every `test_files/codegen_*.b` whose reference exits 0, sorted, in list format.
  - **`--check-list`** fails if a qualifying file is absent from the list or the list has a duplicate.
- [ ] T019 [US1] Generate the committed corpus list:
  - run `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus stdlib --print-qualifying > test_files/lsp/corpus.txt`, and prepend a header comment giving the format, the regeneration command and the inclusion rule;
  - hand-check that the list contains the six overview files, all 6 `examples/*/main.b`, and 171 `codegen_*.b` entries (172 minus `codegen_nsprefix_arc.b`, per research R2); if the counts differ, investigate before committing;
  - commit.
- [ ] T020 [US1] Run the corpus check in **both** build modes:
  - `python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus stdlib --check-list`;
  - the same command with `--build-dir build-nollvm`.

  Every file must PASS. Fix any FAIL by correcting the blangd plan assembly in `lsp/Compile.cpp`, never by adding a rule or a filter exception.

  A FAIL that seems to need a blangd-only rule is a STOP. Record the per-file PASS output in `specs/042-lsp-stdlib-context/checklists/corpus-run.md`.
- [ ] T021 [US1] Wire the corpus leg into `test_lsp.sh`, after the `fail/sema` leg and only when no fixture arguments are given and not under `--update-goldens`:
  - run `python3 "$SCRIPT_DIR/tools/lsp_diag_corpus.py" --build-dir "$BUILD_DIR" --root "$SCRIPT_DIR" --corpus stdlib --check-list`;
  - count it as one TOTAL entry, FAIL on a non-zero exit, and print the tool's FAIL lines;
  - it never skips: a missing `bcc`/`qcc` FAILs the leg (exit 2).
- [ ] T022 [US1] In `.github/workflows/ci.yml`'s `lsp` job, change the build step's `--target blangd jsonTest` to `--target blangd jsonTest qcc bcc`, so the parse-only CI run has the reference compile (research R13). Change nothing else in the job.

### Ownership lock (FR-012, FR-013, plan D-4)

- [ ] T023 [US1] In `ParserLeakTest.cpp`, **append** a new block after the existing cases and before the final `printf`. The existing cases and asserts stay unmodified.
  - Build `lsp::StdlibLocation loc = lsp::locateStdlib( BLANG_SOURCE_STDLIB_DIR, "", "" );` and `assert( loc.usable )`.
  - Run 50 iterations. Each one calls `lsp::compileDocument( "leak_stdlib.b", kStdlibFixture, loc )` in its own block, asserts `res.module != nullptr`, asserts `res.diagnostics.empty()`, and drops the result.
  - `kStdlibFixture` is a clean zero-diagnostic program that **really resolves into the stdlib**: `import net;`, uses `net.http_ok(...)`, constructs `Map<string, int>()`, `Set<int>()` and `Buffer(8)`, and calls a method on each. Verify that it is clean with `qcc --parse-only --combine <base stdlib> <file>` before committing.
  - A comment states the KG-7 invariant: the user AST's non-owning links point into stdlib modules owned only by `res.context`, and the loop proves that each result frees its whole context.

  In `CMakeLists.txt`, add `target_compile_definitions( parserLeakTest PRIVATE BLANG_SOURCE_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib" )`.
- [ ] T024 [US1] Run `ctest --test-dir build-asan -R "parser_no_leak|project_compile|resolver_reuse|stdlib_locate|blangd_no_subprocess" --output-on-failure`. All must pass, and LeakSanitizer must report zero leaks in the 50-reparse loop.

  Add a teeth check, and do not commit it: temporarily store one `CompileResult` per iteration in a leaked `new` vector, confirm that LSan fails, then revert. Note the result in `specs/042-lsp-stdlib-context/checklists/corpus-run.md`. Commit.

**Checkpoint**: US1 complete. The editor agrees with the compiler on the stdlib corpus in both build modes, and memory is bounded.

---

## Phase 4: User Story 2 — The stdlib is found wherever blangd is installed (Priority: P2)

**Goal**: each discovery route, and the not-found fallback, is pinned by a transcript golden (FR-006 to FR-008, FR-014; contract [stdlib-discovery.md](contracts/stdlib-discovery.md)).

**Independent Test**: `./test_lsp.sh test_files/lsp/stdlib_*.lsp.jsonl`: all 5 match their goldens.

- [ ] T025 [P] [US2] Create `test_files/lsp/src/prelude_use.b`, a zero-import program using `Map<string,int>()`, `Set<int>()` and `Buffer(8)` with a method call on each. Its reference compile must be clean. Verify that with `bcc -v -S`'s `qcc` line exiting 0 with empty stderr, and verify that the single-file `qcc --parse-only` reports `unknown type 'Map'` (the fallback signal).
- [ ] T026 [US2] Create the five fixtures `test_files/lsp/stdlib_{initopts,env,missing_env,relative,missing}.lsp.jsonl`. Each follows `diag_open.lsp.jsonl`'s structure:
  - initialize, initialized, `{"open": "test_files/lsp/src/prelude_use.b"}`, `wait_method` publishDiagnostics, shutdown, exit;
  - a header comment states which route it proves (plan D-6 table);
  - the setup per fixture:
    - `stdlib_initopts`: `{"server_env": {"BLANG_STDLIB_DIR": "${ROOT}/test_files/lsp/src"}}` plus `initializationOptions.stdlibPath = "${ROOT}/stdlib"`;
    - `stdlib_env`: `server_env BLANG_STDLIB_DIR=${ROOT}/stdlib`;
    - `stdlib_missing_env`: `server_env BLANG_STDLIB_DIR=${ROOT}/test_files/lsp/src`;
    - `stdlib_relative`: nothing set;
    - `stdlib_missing`: `stdlibPath = "${ROOT}/test_files/lsp/src"`.
- [ ] T027 [US2] Generate the five goldens with `./test_lsp.sh --update-goldens test_files/lsp/stdlib_*.lsp.jsonl`. Only the new fixtures may be passed; never regenerate pre-existing goldens. Then **review each by hand** against the contract table:
  - `initopts`, `env` and `relative` have `"diagnostics": []`;
  - `missing_env` has the warning first, naming `BLANG_STDLIB_DIR` and `'${ROOT}/test_files/lsp/src'` with `(missing sys.b)`, then the single-file `unknown type 'Map'` errors;
  - `missing` has the same shape, naming `initializationOptions.stdlibPath`.

  Each `stdlib-not-found` warning has severity 2, code `stdlib-not-found` and range 0:0.
- [ ] T028 [US2] Run the full `./test_lsp.sh` and `./test_lsp.sh --selfcheck`. The total is 75 transcripts plus the sema leg plus the corpus leg, and selfcheck prints `SELFCHECK: OK`. Run the same suite with `BUILD_DIR=build-nollvm` as well. Commit.

- [ ] T038 [US2] Add the transcript golden `stdlib_didchange_import` (analyze F3, proving FR-002: imports are re-scanned from in-memory text on every change).
  - Create `test_files/lsp/src/timer_use.b`, which uses `timer.after(...)` **without** `import timer;`. Under stdlib context its reference compile reports an error at the use site.
  - Create `test_files/lsp/stdlib_didchange_import.lsp.jsonl`:
    - initialize, then `open` `timer_use.b`, then `wait_method` publishDiagnostics (an error is pinned);
    - send a `didChange` whose full text is the same file with `import timer;` prepended, then `wait_method` (`diagnostics: []` is pinned);
    - shutdown, exit.
  - Generate only this golden with `--update-goldens`, then hand-check both publishes.

**Checkpoint**: US2 complete. Precedence, no fall-through, the fallback, and the in-memory import rescan are each pinned.

---

## Phase 5: User Story 3 — Editing stays responsive (Priority: P2)

**Goal**: the median `didChange` → `publishDiagnostics` time on `examples/todo_app/main.b` is at most 500 ms (FR-011, SC-003).

**Independent Test**: `python3 tools/lsp_latency.py --server build/blangd --root "$PWD" examples/todo_app/main.b` exits 0.

- [ ] T029 [P] [US3] Create `tools/lsp_latency.py` (python3 stdlib only, executable) per [contracts/lsp-test-tools.md](contracts/lsp-test-tools.md).
  - **Options**: `--server`, `--root`, `--edits 20`, `--budget-ms 500`, `FILE...`.
  - **Per file**, one session: import `Driver` from `lsp_client.py`, remove `BLANG_STDLIB_DIR`, then initialize and `didOpen`, waiting for the publish.
  - **Edits**: send `--edits` full-text `didChange` notifications, version incrementing, where edit *i* appends `"\n// edit %d\n" % i` to the original text. Time `send` → the next publish for that uri with `time.monotonic()`.
  - **Output**: `<file>: median <ms> ms (min <ms>, max <ms>, n=<edits>)`.
  - **Exit codes**: 1 if any median exceeds the budget; 0 otherwise.
  - The tool itself never skips.
- [ ] T030 [US3] Wire the latency leg into `test_lsp.sh`, after the corpus leg, under the same no-fixture-args / not-`--update-goldens` condition:
  - if `$BUILD_DIR/CMakeCache.txt` has a non-empty `BLANG_SANITIZE` value, print `SKIP: latency leg (sanitizer build: $BUILD_DIR)` and count nothing;
  - else run `python3 "$SCRIPT_DIR/tools/lsp_latency.py" --server "$BLANGD" --root "$SCRIPT_DIR" examples/todo_app/main.b`, counting one TOTAL entry and FAIL on a non-zero exit;
  - accept a file list so that U3 only appends `test_build/usebox/main.b`.
- [ ] T031 [US3] Run `./test_lsp.sh`, and record the latency line in `specs/042-lsp-stdlib-context/checklists/corpus-run.md`; the median must be ≤ 500 ms.

  Run `BUILD_DIR=build-asan ./test_lsp.sh` and confirm the SKIP notice prints, with the rest of the suite green. If the median exceeds 500 ms, STOP and raise a question before adding any cache (sequencing note; standing rule 2). Commit.

**Checkpoint**: US3 complete.

---

## Phase 6: Polish & cross-cutting

- [ ] T032 [P] Update `CLAUDE.md`:
  - **Repository Structure**: add entries for `lsp/StdlibLocation.{h,cpp}` (stdlib discovery), `lsp/StdlibLocateTest.cpp`, `tools/lsp_diag_corpus.py`, `tools/lsp_latency.py`, `test_files/lsp/corpus.txt` and `test_files/lsp/sema_no_prelude.txt`;
  - the `StdlibSelect` entry gains `baseModuleNames`/`executableDir`;
  - the `test_lsp.sh` line mentions the corpus and latency legs;
  - **Known Issues**: one bullet for U2:
    - blangd compiles each document in `bcc file.b`'s stdlib/prelude context through `ProjectCompile::run`;
    - discovery is `stdlibPath` > `BLANG_STDLIB_DIR` > `<blangd-dir>/stdlib`, with no fall-through, and a `stdlib-not-found` warning otherwise;
    - the KG-7 invariant: any stdlib state is owned by the `CompileResult`, and there is no cache.
  - Do **not** yet remove "single-file" wording about project context; that is U3/U5.
- [ ] T033 [P] Update `editors/README.md` with a "Stdlib discovery" section that covers:
  - the three routes in precedence order, with a Neovim `init_options = { stdlibPath = … }` example and a VS Code / generic `initializationOptions` note;
  - the no-fall-through rule;
  - the `stdlib-not-found` warning text.
- [ ] T034 Security self-review, recorded in `specs/042-lsp-stdlib-context/checklists/security.md` against constitution Quality Gate 7. It is the input for the independent security review. For each item, cite the file and line that enforces it:
  - the path from `initializationOptions`/env is used only as a prefix for `stat`/`access`/read-only opens;
  - the appended names are only the base list or `scanImports` identifiers (`[A-Za-z0-9_]+`), so there is no traversal;
  - there is no execution and no network, shown by the `blangd_no_subprocess` ctest output;
  - a malformed `initializationOptions` (a non-object or a non-string `stdlibPath`) is ignored, never a crash;
  - a symlinked directory is followed read-only, as `bcc` does.
- [ ] T035 Final verification, the whole of plan §Verification protocol, run on `build`, `build-nollvm` and `build-asan`:
  - `./test_lsp.sh` (+ `--selfcheck`) in each tree;
  - ctest 86/86 ×3;
  - `run_tests.sh` ×3;
  - `test_codegen.sh` plus `--leak-check` (Leaks: 0) and `--selfcheck`;
  - `test_build/run_build_tests.sh`;
  - the reach-in gate (+ `--selfcheck`), `examples/*/test_*.sh` and `make -C demos run`;
  - the T006 `bcc` `cmp` re-check;
  - standing rule 4's `git diff 041-shared-project-compile...HEAD -- test_files/` check (analyze F1), where the only `test_files/` changes are the allowlisted new files.

  Record the post-unit counts in `specs/042-lsp-stdlib-context/checklists/baseline.md`. Confirm `git diff 3b093f6 HEAD -- specs/042-lsp-stdlib-context/spec.md` is empty. Commit.
- [ ] T036 Structural greps, recorded in `specs/042-lsp-stdlib-context/checklists/baseline.md`:
  - `grep -n "ProjectCompile::run" lsp/Compile.cpp` finds at least one call;
  - `grep -nE "\"sys\"|\"collections\"|\"buffer\"" lsp/*.cpp` prints nothing, because the base list is only in `StdlibSelect`;
  - `grep -nE "selectStdlibFiles|scanImports" lsp/Compile.cpp` finds the calls;
  - `grep -nE "popen|fork|exec[lv]|system\(|socket\(|connect\(" lsp/*.cpp tools/lsp_diag_corpus.py tools/lsp_latency.py` lists only the Python tools' `subprocess` use, since tools are not blangd. The binary-level check is `blangd_no_subprocess`.
- [ ] T037 Remove the `/tmp/u2-ref` scratch directory and confirm `git status` is clean.

---

## Dependencies & execution order

- **Phase 1 (T001 to T003)** comes first.
- **Phase 2**: T004 → T005 → T006 (D-1, one commit), then T007 ∥ T008 → T009 (D-2), then T010 → T011 (D-5 part 1). Phase 2 blocks every story.
- **US1 (Phase 3)**:
  - T012 → T013 → T014;
  - T015 ∥ T016 → T017;
  - T018 → T019 → T020 → T021, with T022 independent once T018 exists;
  - T023 → T024.

  T015 is a prerequisite of US2's server_env goldens and of the T018/T029 tools, which import `Driver`.
- **US2 (Phase 4)** depends on T012/T013 (the compile in context) and T015 (the `server_env` step). T025 ∥ T026 → T027 → T028.
- **US3 (Phase 5)** depends on T012/T013 and T015. T029 → T030 → T031. It can run alongside US2.
- **Polish (Phase 6)** comes after all stories. T032 ∥ T033 ∥ T034, then T035 → T036 → T037.

## Parallel examples

```text
# Phase 2 (after D-1 commit)
T007 lsp/StdlibLocation.{h,cpp}   ∥   T008 lsp/StdlibLocateTest.cpp

# US1
T015 tools/lsp_client.py          ∥   T016 test_files/lsp/sema_no_prelude.txt (+ test_lsp.sh routing after T015)

# US2 ∥ US3 (both after T013 + T015)
T025/T026 fixtures                ∥   T029 tools/lsp_latency.py

# Polish
T032 CLAUDE.md   ∥   T033 editors/README.md   ∥   T034 checklists/security.md
```

## Implementation strategy

1. **The MVP is US1**: the context, corpus parity and the ownership lock. It removes the false diagnostics (REQ-002) and proves it on the whole corpus in both build modes. Land the Phase 2 foundation first; the D-1 move is checked byte-for-byte against `/tmp/u2-ref`.
2. Then **US2**, the goldens that pin discovery (REQ-003), and **US3**, the latency floor (REQ-009 single-file half). These are independent, small increments.
3. **The converge gate** (workplan U2 (f)): `/speckit-converge` reports `converged`, or two converge→implement cycles leave only LOW/MEDIUM `unrequested` findings.

## Traceability

| Requirement | Tasks |
|---|---|
| FR-001/002 shared plan, buffer imports | T012 (plan assembly via `StdlibSelect` + `ProjectCompile::run`), T020, T036 |
| FR-003 exact diagnostics | T018 to T021 (corpus check) |
| FR-004 document-only diagnostics | T012 (filter), T018 (premise check) |
| FR-005 no-identity policy | T012 |
| FR-006/007 precedence, no fall-through, usability | T007, T008, T010, T026/T027 |
| FR-008 not-found warning | T007 (text), T012 (insertion), T027 (`stdlib_missing*`) |
| FR-009 no process/network | standing rule 3, T034, T036, `blangd_no_subprocess` |
| FR-010 corpus check + list | T018 to T022 |
| FR-011 latency | T029 to T031 |
| FR-012 KG-7 ownership | standing rule 2, T012 (comment), T023/T024 |
| FR-013 `parser_no_leak` ≥ 50 reparses | T023, T024 |
| FR-014 discovery goldens + shared harness | T015, T025 to T028 |
| FR-015 pre-existing goldens | T014, T017, T028 |
| FR-016 `fail/sema` leg routing | T015(c), T016, T017 |
| FR-017 `qcc`/`bcc` frozen | T003, T006, T035 |
