---

description: "Task list for U1 — shared project-compile entry point (behavior-neutral extraction)"
---

# Tasks: Shared Project-Compile Entry Point (behavior-neutral extraction)

**Input**: Design documents from `specs/041-shared-project-compile/`:
- [plan.md](plan.md), including the design-audit amendments F1–F7;
- [spec.md](spec.md);
- [research.md](research.md);
- [data-model.md](data-model.md);
- [contracts/project-compile-api.md](contracts/project-compile-api.md);
- [contracts/stdlib-select-api.md](contracts/stdlib-select-api.md);
- [quickstart.md](quickstart.md).

**Tests**: REQUIRED. The spec requires the `project_compile` ctest (FR-019). Audit F3 makes `blangd_no_subprocess` (+ teeth) required. Every pre-existing gate must also stay green (FR-001/SC-001).

**Organization**: by user story. US2 (the shared entry point) is implemented first, because US1 (neutrality) is the verification of US2's change and US3 (the component ctest) drives US2's API. All three are required for the unit.

## ⚠ Standing rules for EVERY task (plan constraints 1–3)

1. **Behavior-neutral.** No change to exit codes, diagnostic text/location/severity/order/count (human and `--json`, with and without `-Werror`/`--debug-compiler`), stdout/stderr (including `-v`, `--dump-locations`), emitted IR/`.bmod`/schema, or LSP transcripts.
2. **Zero golden edits (F7).** After every commit, both of these must print nothing:

   ```bash
   git diff --no-renames --name-only --diff-filter=MDTRC epic/modules-v2-lsp...HEAD -- test_files/
   git diff --no-renames --name-only --diff-filter=A     epic/modules-v2-lsp...HEAD -- test_files/ | grep -v '^test_files/project_compile/'
   ```

   Never run any `--update-goldens`. Never touch the `.bmod` format version, `BuildCache` keys, the quarantine list or D1–D17.
3. **Stop rule.** If any gate goes red in a way that seems to *require* a golden, diagnostic or output change: stop, do not edit the golden, and end the turn with `BLOCKER: U1 <gate> <file> <diff summary> <why it looks forced>`.
4. **Move, don't rewrite** (research R1). Code is lifted verbatim, in its original order. The R3 ordering checklist is the review contract.
5. House style: tabs, spaces inside parentheses, Allman braces, `m`-prefixed members, `nullptr`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on incomplete tasks)
- **[Story]**: US1 / US2 / US3 (spec.md user stories)

---

## Phase 1: Setup (baseline: required before any code change)

**Purpose**: Measure the base and capture pre-unit reference binaries for the byte-identity check (quickstart §1; plan Verification gate 8).

- [ ] T001 Confirm the precondition and branch.
  - `git branch --show-current` prints `041-shared-project-compile`.
  - `git merge-base --is-ancestor efb099a HEAD` succeeds.
  - Record `git rev-parse epic/modules-v2-lsp` (expected `2f68efe`) in `specs/041-shared-project-compile/checklists/baseline.md`.
- [ ] T002 Build the pre-unit reference tree.
  - `git worktree add /tmp/u1-base epic/modules-v2-lsp`.
  - Configure and build `/tmp/u1-base/build` (LLVM; targets `qcc bcc blangd`) and `/tmp/u1-base/build-nollvm` (`-DBLANG_ENABLE_LLVM=OFF`).
  - Record the two build paths in `specs/041-shared-project-compile/checklists/baseline.md`.
- [ ] T003 Run every gate on the current branch HEAD, which has no code changes yet, and record each count in `specs/041-shared-project-compile/checklists/baseline.md`:
  - `./run_tests.sh` (expected 251/0) and `BUILD_DIR=build-nollvm ./run_tests.sh` (expected 244/0);
  - `./test_codegen.sh` (172/0), `--leak-check` (Leaks: 0) and `--selfcheck` (prints `SELFCHECK: OK`);
  - `test_build/run_build_tests.sh`;
  - `ctest --test-dir build` and `ctest --test-dir build-asan` (82/82 each);
  - `./test_lsp.sh` (70/0) and `./test_lsp.sh --selfcheck`;
  - `tools/check_no_field_reachins.sh`;
  - `examples/*/test_*.sh`;
  - `make -C demos run`.

  Any pre-existing red is **reported as a `BLOCKER:`/`DECISION:` line and not absorbed** (evaluation.md "Regression protection").
- [ ] T004 Capture the reference outputs from the `/tmp/u1-base/build` binaries into `/tmp/u1-ref/`, one file per command (stdout, stderr, exit code). Use exactly the command list in quickstart §3:
  - `qcc --parse-only` on the six overview files;
  - the `qcc` lines `bcc -v -S <file>` prints for the same six;
  - one `--json`;
  - one `-Werror` on `test_files/fail/warn/*`;
  - `--dump-locations test_files/pass/func_simple.b`;
  - `--emit-bmod` over `test_build/mathlib/*.b`.

  These are scratch files outside the repo. Do not commit them.

**Checkpoint**: baseline recorded; reference binaries and outputs exist.

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: Pure moves and declarations that every story's code depends on. None changes behavior.

- [ ] T005 Move the in-memory reader into the frontend (research R7).
  - Create `StringLexerReader.h` at the repo root. Its content is `lsp/StringLexerReader.h`'s class, verbatim, in `namespace QLang`, with include `"FileLexer.h"`.
  - Rewrite `lsp/StringLexerReader.h` as `#include "../StringLexerReader.h"` plus `namespace lsp { using StringLexerReader = QLang::StringLexerReader; }`, keeping its include guard.
  - `ParserLeakTest.cpp`, `ResolverReuseTest.cpp` and `lsp/Compile.cpp` must compile **unmodified**.
- [ ] T006 [P] Create `ProjectCompile.h` at the repo root exactly as in `contracts/project-compile-api.md`:
  - `CompileInput` (`Kind { kSource, kInterface }`, `path`, `inMemory`, `text`, `origin`);
  - `IdentityPolicy { kCanonical, kNone }`;
  - `CompilePlan` (`inputs`, `combine`, `moduleOrigin`, `identity`, `lexerTrace`);
  - `ModuleRole { kUser, kPreludeProvider, kNamespaced, kInterface, kStandalone }`;
  - `ModuleRecord`: `path`, `moduleName`, `role`, `scope` set at routing, `bool recorded = false`, and `SmartPtr<Module> module` assigned only when recorded (F1);
  - `CompileContext`, with accessors `records()`, `record(i)`, `moduleIndices()` (recorded-only, F1), `hadError()`, `userScope()`, `preludeScope()`, `bmodEmitScope()`, `isPreludeProvider(i)`, `codegenPrefix(i)` and `suppressedPrelude()`. Its private members are `std::vector<SmartPtr<Scope>> mOwnedModuleScopes`, the records, the index list, `std::set<std::string> mPreludeModuleNames`, `mPreludePrefixExempt`, `mSuppressedPrelude`, `std::map<std::string, Scope*> mModuleNamespaces`, `mHadError`, the scope pointers, and `friend class ProjectCompile`;
  - `ProjectCompile::run( const CompilePlan &, Resolver &, DiagnosticEngine & ) -> std::unique_ptr<CompileContext>`;
  - the free functions `classifyInput( path )` and `moduleNameOf( path )`.

  Header comments state invariants I1–I9 in one line each, including the reworded I3: "the CALLER installs gScope; run asserts it, never writes it".
- [ ] T007 [P] Create `StdlibSelect.h` at the repo root exactly as in `contracts/stdlib-select-api.md`: `namespace BlangStdlib { scanImports; selectStdlibFiles; }`, with the verbatim F5 rule in the doc comment. Includes are `<set> <string> <vector>` only.
- [ ] T008 Update `CMakeLists.txt`:
  - append `ProjectCompile.cpp` and `StdlibSelect.cpp` to `set(BLANG_FRONTEND_SOURCES …)`;
  - append `StdlibSelect.cpp` to `add_executable(bcc bcc.cpp ProjectConfig.cpp BuildCache.cpp sha256.c …)`.

  Create stub `ProjectCompile.cpp` / `StdlibSelect.cpp` containing only their includes, so all three build directories (`build`, `build-nollvm`, `build-asan`) configure and build green before any logic moves.

**Checkpoint**: all build directories compile; the full gate list from T003 is green and unchanged; the zero-golden-edit commands print nothing. Commit.

---

## Phase 3: User Story 2: one shared entry point, called by the compiler and the editor (Priority: P1) 🎯 core

**Goal**: A single shared implementation of the stdlib selection, the import scan, the scope routing, prelude LOAD/PROMOTE, `.bmod` handling, origin/digest stamping, Sema and the import-diagnostics pass. `qcc.cpp` and `lsp/Compile.cpp` both call it, and `bcc.cpp` calls the stdlib selection.

**Independent Test**: the quickstart §4 structural greps:
- `ProjectCompile::run` appears in both `qcc.cpp` and `lsp/Compile.cpp`;
- none of `isUserFile|isPreludeModule|injectBmodSymbols|authoritativeModules|Sema::analyze` remain in `qcc.cpp`;
- neither `resolveStdlibFiles` nor `parseImports` remains in `bcc.cpp`;
- `BlangStdlib::` appears at exactly four `bcc.cpp` call sites.

### D-1: `StdlibSelect` (FR-016/017/018; F4/F5)

- [ ] T009 [US2] Implement `BlangStdlib::scanImports( const std::string &sourceText )` in `StdlibSelect.cpp`.
  - Copy `bcc.cpp`'s `parseImports` body (L965) **verbatim**. Replace only `readFileToString( path )` with `sourceText`.
  - Keep each step of the rule exactly:
    1. `std::getline` per line;
    2. truncate at the first `"//"`;
    3. skip `isspace( (unsigned char)c )`;
    4. require `line.compare( i, 7, "import " ) == 0` (literal single ASCII space);
    5. skip `isspace` again;
    6. take the maximal `isalnum || '_'` run and insert it if non-empty.
- [ ] T010 [US2] Implement `BlangStdlib::selectStdlibFiles( const std::string &stdlibDir, const std::set<std::string> &imports )` in `StdlibSelect.cpp`.
  - Copy `resolveStdlibFiles` (bcc.cpp L1002) verbatim, except that the candidate becomes `stdlibDir + "/" + name + ".b"` (research R5: identical string when `stdlibDir == exeDir + "/stdlib"`).
  - Keep the `access( F_OK )` test, the `handled` set, the base list `sys, buffer, collections, fs, net`, the gated list `timer, math, time, random, env, cli`, and the trailing any-other-import loop.
  - Include `<unistd.h>`; no other dependency.
- [ ] T011 [US2] Switch `bcc.cpp` to the shared functions (F4 call-site labels). Add `#include "StdlibSelect.h"`, then replace:
  - (a) `compileAndRunTestFile` (L571, `bcc test`) with `BlangStdlib::selectStdlibFiles( exeDir + "/stdlib", BlangStdlib::scanImports( readFileToString( file ) ) )`;
  - (b) `runMigrate` (L798–L800, `bcc migrate`): keep the union loop over `sourceFiles`, change only `parseImports( f )` to `BlangStdlib::scanImports( readFileToString( f ) )`, and change `resolveStdlibFiles( exeDir, imports )` to `BlangStdlib::selectStdlibFiles( exeDir + "/stdlib", imports )`;
  - (c) `buildProject` (L1336–L1339, `bcc build`): the same two substitutions, with the union loop kept;
  - (d) `main`'s single-file compile (L1698, `bcc file.b`): `BlangStdlib::selectStdlibFiles( exeDir + "/stdlib", BlangStdlib::scanImports( readFileToString( opts.inputFile ) ) )`.

  Delete both `parseImports` forward declarations (L555, L739), both `resolveStdlibFiles` forward declarations (L556, L740), the `parseImports` body (L965) and the `resolveStdlibFiles` body (L1002). Keep `readFileToString` in `bcc.cpp`, because the new call sites still use it to read each file. Where the deleted `parseImports` forward declarations were, add `static string readFileToString( const string & );` (analyze L1) so the new call sites at L571/L798, which precede `readFileToString`'s definition, compile.
- [ ] T012 [US2] D-1 checkpoint. Run the T003 gate list (especially `test_build/run_build_tests.sh`, `bcc test` via `test_codegen.sh`'s bcc legs, and examples), the zero-golden-edit commands, and `grep -nE "resolveStdlibFiles|parseImports" bcc.cpp`, which must print nothing. Commit.

### D-2: `ProjectCompile::run` (FR-004–013; F1/F2/F6; research R3)

- [ ] T013 [US2] Implement `classifyInput` and `moduleNameOf` in `ProjectCompile.cpp`.
  - `classifyInput` is `kInterface` iff `path.size() >= 5 && path.substr( path.size() - 5 ) == ".bmod"`.
  - `moduleNameOf` is qcc's basename block: strip through the last `'/'`, then from the last `'.'`. qcc repeats this block at L462, L719 and L746, and it must give the identical string in every case.
- [ ] T014 [US2] Implement the `run` prologue in `ProjectCompile.cpp`:
  - assert (debug) `gScope == resolver.globalScope()`, and never write `gScope` (I3, F2);
  - save `gDiag`, then set `gDiag = &diag`;
  - create the context;
  - apply the stable interfaces-first reorder of a local copy of `plan.inputs` (R3.1);
  - in combine mode, create `preludeScope = resolver.newModuleScope()` and `combineScope = resolver.newModuleScope( preludeScope )`, both pushed onto `mOwnedModuleScopes` (R3.2);
  - add a scope-exit restore of `gDiag` on every return path.
- [ ] T015 [US2] Lift `injectBmodSymbols` (qcc.cpp ~L311–L377) **verbatim** into `ProjectCompile.cpp` as a file-static function `injectBmodSymbols( Module *bmod, const std::string &moduleName )`. It still uses `gScope->findNamespace` / `addNamespace` / `setParent( gScope )` / `setGrantsNameCapability( true )`. This is the global-scope adoption preserved by A3 / I6; leave a `// U3 (I6): must move under a context-owned dependency scope` comment.
- [ ] T016 [US2] Lift the per-input loop (qcc.cpp L455–L795) into `run`, verbatim and in order (R3.3–R3.12). For each input:
  - **(a)** Route. Combine mode with a source input: the user file if `fileIdx == inputs.size()-1`; a prelude provider (`buffer`/`collections`: holding namespace `new Scope( kScope_Namespace )`, `setParent( combineScope )`, `addNamespace`, record in `mModuleNamespaces` / `mPreludeModuleNames` / `mPreludePrefixExempt`); otherwise a namespaced module. Anything else: `resolver.newModuleScope()` adopted into `mOwnedModuleScopes`. Append a `ModuleRecord { path, moduleName, role, scope }` with `recorded = false` and `module` null.
  - **(b)** Interface foreign-type pre-scan. Keep the malformed-header error text and the inner `continue`. After the scan, keep the **global-`hadError`** skip, the quirk preserved by R3.6.
  - **(c)** Lex with `LexerReader( path )` for disk inputs, or `StringLexerReader( text, path )` for in-memory ones, applying `setTraceEnabled( plan.lexerTrace )`. Then `Module::Parse`. On null: set `hadError` and `continue`.
  - **(d)** `stampDefiningOrigin( mod, path )`.
  - **(e)** Unless `plan.identity == kNone`: origin selection (interface: `input.origin`; source: `plan.moduleOrigin`; else `realpath`, whose failure is the same located error at 1:1, then `continue`), then `stampModuleDigest`.
  - **(f)** For an interface: the 8-line format probe and the mismatch error (`continue`), then `setExtern( true )`, `setFromInterface( true )` on every struct, and `injectBmodSymbols( mod, moduleName )`. Drop the write-only `bmodMap` (R3.14).
  - **(g) RECORDED (F1).** Only here assign `record.module = mod` and `record.recorded = true`, append the index to the recorded-index list, and for a source input set `mBmodEmitScope = scope`. This is the qcc L737 point.
  - **(h)** `PARSE_TRACE( "Completed parse" )`.
  - **(i)** For a source input: `if ( !Sema::analyze( mod, scope, *gDiag ) ) hadError = true;`.
  - **(j)** Eager PROMOTE: in combine mode, when the just-recorded module's name is in `mPreludeModuleNames`, add each `isPreludeTypeName` struct to `preludeScope` via `addSymbol` + `addType( new Type )`.

  The in-memory interface path runs (b)/(f)'s line scans over an `std::istringstream` through the same helper as the `std::ifstream` disk path (R6). The disk path keeps its own two `ifstream` opens exactly as today.
- [ ] T017 [US2] Lift the import-diagnostics pass (qcc.cpp L803–L824) into `run`, after the loop.
  - The gate is `plan.combine || <any input with kind == kInterface>`. That counts inputs, recorded or not, exactly as `!bmodFiles.empty()` does today.
  - Iterate **only `moduleIndices()`** (F1/I9) and skip extern modules.
  - Use the same `findNamespace( realName )` → unknown-module **error** text, and `wasModuleUsed( localQ )` → `unused-import` **warning** text and slug.
- [ ] T018 [US2] Compute `mSuppressedPrelude` in `run` after the import pass, lifting the non-LLVM half of qcc.cpp L918–L935 verbatim but over **recorded modules only** (F1/F6):
  - for each recorded module `mn` with `!mPreludeModuleNames.count( mn ) && !mModuleNamespaces.count( mn )`, which is name-keyed and therefore admits `.bmod` modules and the user file, collect its struct names;
  - keep those that satisfy `isPreludeTypeName`.

  Implement `isPreludeProvider( i )` as `mPreludeModuleNames.count( record( i ).moduleName )`. Implement `codegenPrefix( i )` as `( mModuleNamespaces.count( name ) && !mPreludePrefixExempt.count( name ) ) ? name : ""`. Both are **name-keyed**, not role-keyed (R10). Store `mHadError` and return the context.
- [ ] T019 [US2] In `ProjectCompile.cpp`, verify by inspection that `run` never calls `diag.finish()`, writes nothing to stdout/stderr besides `PARSE_TRACE` and lexer trace, and contains no process or network calls (I2/I8). The binary-level proof is T031.

### D-3: `qcc.cpp` becomes a thin caller (FR-014)

- [ ] T020 [US2] Rewrite `qcc.cpp` `main()` between the `Resolver resolver;` line (L262) and the error exit.
  - Keep `setParseTraceEnabled( verbose && !dumpLocations )` at its current point, before the plan is built.
  - Build a `CompilePlan`: one `CompileInput { path, classifyInput( path ) }` per `inputFiles` entry, in command-line order; for interface inputs, `origin` is `bmodOrigins[path]` if present; `combine = combineMode`; `moduleOrigin`; `lexerTrace = verbose && !dumpLocations`; `kCanonical`.
  - Declare `auto ctx = ProjectCompile::run( plan, resolver, diagnostics );` **after** `resolver`, so the KG-7 destruction order is preserved.
  - Keep qcc's own `bmodFiles` / `sourceFiles` partition (the combined IR name uses `sourceFiles.back()`).
  - Delete:
    - `ownedModuleScopes` and `adoptModuleScope`;
    - `modules`, `moduleScopes` and `bmodMap`;
    - `injectBmodSymbols`;
    - the routing branch with `isUserFile` / `isPreludeModule`;
    - the pre-scan, the format check and the origin block;
    - `Sema::analyze`;
    - eager PROMOTE;
    - the import-diagnostics block with `authoritativeModules`;
    - the LLVM-side `userTypeNames` / `suppressedPrelude` computation.
- [ ] T021 [US2] Re-point qcc's output stages at `ctx` in `qcc.cpp`, logic unchanged:
  - **Error exit**: `if ( ctx->hadError() || gDiag->hasErrors() ) { gDiag->finish(); return 1; }`. Both halves are needed (F6).
  - **`--dump-locations`, `--emit-schema`**: iterate `ctx->moduleIndices()`, using `record( i ).module`.
  - **`--emit-bmod`**: non-extern recorded modules plus `ctx->bmodEmitScope()`.
  - **LLVM**:
    - `allStructs` skips structs with `ctx->isPreludeProvider( i ) && ctx->suppressedPrelude().count( name )`;
    - `setModulePrefix( ctx->codegenPrefix( i ) )`;
    - per-module IR file names and "Code generation failed for …" messages use `ctx->record( i ).path`, replacing `inputFiles[idx]` (equivalent on an error-free compile).
- [ ] T022 [US2] D-2/D-3 checkpoint. Run:
  - the full T003 gate list on `build`, `build-nollvm` and `build-asan`;
  - the zero-golden-edit commands;
  - the quickstart §3 byte-identity loop against `/tmp/u1-ref/` (every `cmp` identical, including `--json`, `-Werror`, `--dump-locations` and `--emit-bmod`);
  - `grep -nE "isUserFile|isPreludeModule|injectBmodSymbols|authoritativeModules|Sema::analyze" qcc.cpp`, which must print nothing.

  On any red, follow the stop rule. Commit.

### D-4: `lsp/Compile.cpp` through the same entry point (FR-015; F2)

- [ ] T023 [US2] Add `std::shared_ptr<QLang::CompileContext> context;` to `lsp::CompileResult` in `lsp/Compile.h`, keeping `module`, `fileScope` and `diagnostics` (research R9). Include `"../ProjectCompile.h"`. Update the header comment: the compile goes through the shared `ProjectCompile::run`, still single-file in U1.
- [ ] T024 [US2] Rewrite `lsp::compileDocument` in `lsp/Compile.cpp`:
  - keep `static Resolver sResolver;`;
  - **keep `gScope = sResolver.globalScope();` on every call** (F2: a test-local `Resolver` in `resolver_reuse` / `parser_no_leak` restores `gScope` on destruction);
  - keep a fresh `DiagnosticEngine engine;` that is never `finish()`ed;
  - build the plan: one input `{ path, kSource, inMemory = true, text, origin = "" }`, `combine = false`, `identity = IdentityPolicy::kNone`;
  - set `result.context = ProjectCompile::run( plan, sResolver, engine )`;
  - set `result.module = result.context->record( 0 ).module` (null only on a catastrophic parse) and `result.fileScope = result.context->record( 0 ).scope`;
  - set `result.diagnostics = engine.diagnostics()`;
  - remove the now-internal `gDiag` save/restore (it is done inside `run`);
  - rewrite the stale "module-graph methods are present-but-dormant" comment to say blangd routes through `ProjectCompile::run` (single-file in U1; U2/U3 add context).
- [ ] T025 [US2] D-4 checkpoint:
  - `./test_lsp.sh` (70/0, no golden diff) and `./test_lsp.sh --selfcheck`;
  - `ctest --test-dir build-asan -R "parser_no_leak|resolver_reuse|resolver_component"`, unmodified and LSan-clean;
  - the full T003 gates;
  - the zero-golden-edit commands;
  - `grep -n "ProjectCompile::run" qcc.cpp lsp/Compile.cpp`, which must show one call in each.

  Commit.

**Checkpoint**: US2 complete. One implementation of every rule, called from both drivers.

---

## Phase 4: User Story 1: the compiler behaves exactly as before (Priority: P1)

**Goal**: Prove neutrality across every gate and on real programs (SC-001, SC-005). No implementation of its own; it verifies Phase 3.

**Independent Test**: every T003 gate at the baseline counts, with zero golden edits and byte-identical outputs against `/tmp/u1-ref/`.

- [ ] T026 [US1] Re-run the quickstart §3 byte-identity check in full against `/tmp/u1-ref/`, and record each `cmp` result in `specs/041-shared-project-compile/checklists/baseline.md` ("post-unit" column). The check covers:
  - the six overview files via `qcc --parse-only`;
  - the same six via the `qcc` line `bcc -v -S` prints (stdlib paths normalized to each build's prefix);
  - `--json`;
  - `-Werror` on `test_files/fail/warn/*`;
  - `--dump-locations test_files/pass/func_simple.b`;
  - `--emit-bmod` over `test_build/mathlib/*.b`.
- [ ] T027 [US1] Add a non-combine multi-file + `.bmod` spot check to the T026 run:
  - `qcc --parse-only test_files/fail/xmodule/unknown_module_import/consumer.b <emitted lib.bmod>`, which exercises the authoritative gate via an interface input;
  - `qcc --parse-only test_files/pass/import_basic.b`, which must stay lenient (no import diagnostics).

  Both must be byte-identical (exit code + stderr) to the reference binaries. Record the results in `specs/041-shared-project-compile/checklists/baseline.md`.

**Checkpoint**: neutrality proven on suites and real programs.

---

## Phase 5: User Story 3: the entry point is testable without either driver (Priority: P2)

**Goal**: The `project_compile` ctest (C1–C7) and the binary-level `blangd_no_subprocess` ctest (+ teeth) (FR-019; F1/F3/F6).

**Independent Test**: `ctest --test-dir build -R "project_compile|blangd_no_subprocess" --output-on-failure`, and the same in `build-asan` (LSan-clean) and `build-nollvm`.

### Fixtures (new files only, under `test_files/project_compile/`; not globbed by any existing runner, R8)

- [ ] T028 [P] [US3] Create `test_files/project_compile/diag_parity.b` for C1. It must contain:
  - an unused `import net;` (→ unused-import warning);
  - an `import nonesuch;` (→ unknown-module error);
  - one Sema type error (e.g. `int x = "s";` inside `fn main()`).

  Also create `test_files/project_compile/prelude_use.b` for C2/C3: zero imports, with `Map<string,int> m = Map<string,int>();` and `Set<int> s = Set<int>();` in `fn main() -> int`, compiling clean under combine. Also create `test_files/project_compile/prelude_shadow.b` for C4: a user `struct Map` with a distinct field and method, used in `main`, plus a `Set<int>` from the prelude, compiling clean.
- [ ] T029 [P] [US3] Create the C7 fixtures (F1):
  - `test_files/project_compile/wrong_format.bmod`: first line `// blang-bmod-format: 4` (≠ `BlangBmod::kFormatVersion` = 5); syntactically valid interface content (e.g. `pub fn helper(int a) -> int;`); and the lines `import net;` and `import nonesuch;`, so wrongly recording it would add an unknown-module error plus an unused-import warning;
  - `test_files/project_compile/dropped_user.b`: a clean user file that uses nothing from it.

  Confirm by hand that `build/qcc --parse-only --combine <base stdlib> test_files/project_compile/dropped_user.b test_files/project_compile/wrong_format.bmod` reports exactly one error, the format mismatch at `wrong_format.bmod:1:1`.

### `project_compile` ctest

- [ ] T030 [US3] Create `ProjectCompileTest.cpp` at the repo root, in the assert-harness style of `ResolverTest.cpp`.
  - **Arguments**: `argv[1]` is the qcc oracle path and `argv[2]` is the source root. If the oracle is missing, print `FAIL` and exit non-zero; **never skip**.
  - **Helpers**:
    - `runCase( plan, json )`: construct a **fresh `Resolver` in a nested block**, run, render through `DiagnosticEngine( ostringstream )` with `setJson( json )` and `finish()`, and destroy the context before the resolver (KG-7);
    - `oracle( args, json )`: `popen` `ASAN_OPTIONS=detect_leaks=0 <qcc> --parse-only --combine [--json] <paths> 2>&1 1>/dev/null`, capturing stderr and the exit status. The `ASAN_OPTIONS=detect_leaks=0` prefix is on the popen command string, so it applies to the **child only** (analyze H1): under `build-asan` the qcc oracle leaks on its error exit (C1/C7 are error cases; a pre-existing qcc error-path leak outside U1) and a LeakSanitizer report on its stderr would break the byte comparison. The test process itself keeps leak detection on, so SC-004/LSan still applies to `run`.
  - **Cases**:
    - **C1**: inputs = `selectStdlibFiles( <src>/stdlib, scanImports( fixture text ) )` + `diag_parity.b`. Rendered text == oracle text, human and `--json`. `( ctx->hadError() || diag.hasErrors() )` == ( oracle exit ≠ 0 ) (F6).
    - **C2**: `prelude_use.b`. `preludeScope()->findType( "Map" )` and `( "Set" )` are non-null. User-scope lookup of `Map`/`Set` is pointer-equal to the prelude scope's symbol, and the user scope has no own entry. No diagnostics.
    - **C3**: same inputs. `userScope()->findNamespace( "net" )` is non-null. The `stdlib/net.b` record is `kNamespaced` with `codegenPrefix == "net"`. The `buffer` / `collections` records are `kPreludeProvider` with an empty prefix and `isPreludeProvider` true.
    - **C4**: `prelude_shadow.b`. User-scope `Map` lookup is pointer-equal to the user module's own `StructDefinition`. `suppressedPrelude()` contains `"Map"` and not `"Set"`. No diagnostics.
    - **C5**: `scanImports` over the literal text `"import cli;\nimport timer;\n// import math;\n   import   env.x;\nimport zzz_unknown;\nimport\tmath;\n"` == `{cli, timer, env, zzz_unknown}`. The tab case is excluded per F5. `selectStdlibFiles( <src>/stdlib, that set )` == `[sys, buffer, collections, fs, net, timer, env, cli]` as full `<src>/stdlib/<n>.b` strings.
    - **C6**: one in-memory `kSource` input at the nonexistent path `/nonexistent/u1_kNone.b`, `kNone`, `combine = false`. No "cannot resolve a canonical origin" diagnostic. `record( 0 ).scope` is non-null. The diagnostics' count and messages equal a direct `Module::Parse` (`StringLexerReader`) + `stampDefiningOrigin` + `Sema::analyze` on a fresh `resolver.newModuleScope()` in a separate fresh resolver.
    - **C7 (F1)**: first assert the premise (the `wrong_format.bmod` marker value ≠ `BlangBmod::kFormatVersion`). Inputs = base stdlib + `dropped_user.b` + `wrong_format.bmod`. Rendered text == oracle, human and `--json`. Exactly one error diagnostic. The `.bmod` record has `recorded == false` and `module == nullptr`, and its index is absent from `moduleIndices()`. `( hadError() || diag.hasErrors() )` == ( oracle exit ≠ 0 ).
  - Print `ProjectCompileTest: OK` at the end.
- [ ] T031 [US3] Register the test in `CMakeLists.txt`, next to the resolver tests:
  - `add_executable( projectCompileTest ProjectCompileTest.cpp ${BLANG_FRONTEND_SOURCES} )`, with CXX 17, include directory `${CMAKE_CURRENT_SOURCE_DIR}`, linked to `blang_sqlgen blang_sha256` (as `resolverTest`);
  - `add_test( NAME project_compile COMMAND projectCompileTest $<TARGET_FILE:qcc> ${CMAKE_CURRENT_SOURCE_DIR} )`;
  - `add_dependencies( projectCompileTest qcc )`, so the oracle is always built.

### `blangd_no_subprocess` ctest (F3, required)

- [ ] T032 [P] [US3] Create `tools/check_no_subprocess.sh <binary>` (POSIX sh, executable).
  - Run `nm -D --undefined-only "$1"` and strip `@…` version suffixes.
  - Fail (exit 1, listing the offenders) if any symbol is **exactly** one of: `popen system fork vfork execl execlp execle execv execvp execvpe execve fexecve posix_spawn posix_spawnp socket connect getaddrinfo`.
  - Also fail if the undefined-symbol list is empty (static-link guard), or if `nm` is missing or fails.
  - Otherwise print `no-subprocess: OK <binary>` and exit 0.
- [ ] T033 [US3] Register the check in `CMakeLists.txt`:

  ```cmake
  add_test(NAME blangd_no_subprocess COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_subprocess.sh $<TARGET_FILE:blangd>)
  add_test(NAME blangd_no_subprocess_teeth COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_subprocess.sh $<TARGET_FILE:bcc>)
  set_tests_properties(blangd_no_subprocess_teeth PROPERTIES PASS_REGULAR_EXPRESSION "fork")
  ```

  The teeth leg passes only when the script's offender listing names `fork` (analyze L2), not on a bare non-zero exit, so a script that fails for an unrelated reason (missing `nm`, bad path) cannot satisfy it. Verify locally that `sh tools/check_no_subprocess.sh build/bcc` exits non-zero (it imports `fork`/`execvp`/`popen`/`system`) and that `build/blangd` and `build-asan/blangd` exit 0.
- [ ] T034 [US3] D-5/D-6 checkpoint:
  - `ctest --test-dir build`, `ctest --test-dir build-asan` and `ctest --test-dir build-nollvm`, each green at **85/85** (82 + `project_compile`, `blangd_no_subprocess` and `blangd_no_subprocess_teeth`);
  - `project_compile` LSan-clean under `build-asan`;
  - the zero-golden-edit commands, where the only additions are the files under `test_files/project_compile/`;
  - the full T003 gates.

  Commit.

**Checkpoint**: US3 complete. The seam is locked independently of both drivers.

---

## Phase 6: Polish & cross-cutting

- [ ] T035 [P] Update `CLAUDE.md` (docs gate, constitution Gate 5):
  - **Repository Structure**: add entries for `ProjectCompile.h / ProjectCompile.cpp` (the shared project-compile entry point used by qcc and blangd), `StdlibSelect.h / StdlibSelect.cpp` (stdlib-set selection + import scan, shared with bcc), the root `StringLexerReader.h`, `ProjectCompileTest.cpp` and `tools/check_no_subprocess.sh`;
  - **Build targets table**: add `StdlibSelect.cpp` to `bcc`'s row;
  - **Known Issues**: one bullet noting U1 (behavior-neutral; the `Resolver` bullet's "blangd … calls only globalScope()/newModuleScope()" becomes "routes through ProjectCompile::run, single-file in U1"; the I6 dependency-namespace item is deferred to U3).

  Do **not** remove the existing "single-file" wording; that is U5's done-condition.
- [ ] T036 [P] Add `specs/041-shared-project-compile/checklists/neutrality.md`: the R3 ordering checklist (items 1–14 plus 13a), one checkbox per item, each ticked with the qcc.cpp → ProjectCompile.cpp line pair that proves it was lifted in order. This is the reviewer's checklist for the maintainability-focused code audit.
- [ ] T037 Run the final full gate list on `build`, `build-nollvm` and `build-asan`, and the quickstart §4 structural greps, including `sh tools/check_no_subprocess.sh build/blangd`. Record the post-unit counts in `specs/041-shared-project-compile/checklists/baseline.md`. Confirm that `git diff epic/modules-v2-lsp...HEAD -- specs/041-shared-project-compile/spec.md` is empty (the spec is frozen intent). Commit.
- [ ] T038 Remove the `/tmp/u1-base` worktree (`git worktree remove /tmp/u1-base`) and verify that `git status` is clean.

---

## Dependencies & execution order

- **Phase 1 (T001–T004)** must finish first. It gives the baseline and the reference outputs.
- **Phase 2 (T005–T008)** blocks everything. T006 and T007 are [P] with each other. T005 and T008 touch distinct files but T008 needs T006/T007 to exist, so do T005 → (T006 ∥ T007) → T008.
- **US2 (Phase 3)**:
  - D-1 (T009 → T010 → T011 → T012) is independent of D-2 and can land first.
  - D-2 (T013 → T014 → T015 → T016 → T017 → T018 → T019) and D-3 (T020 → T021 → T022) land **in one commit**, so no duplicate of the loop ever exists.
  - D-4 (T023 → T024 → T025) comes after D-2.
- **US1 (Phase 4)** depends on all of US2.
- **US3 (Phase 5)**:
  - The fixtures T028/T029 and the script T032 are [P] and can be written any time after Phase 2.
  - T030/T031 need D-2 (the API) and D-1 (`selectStdlibFiles`).
  - T033 needs T032.
  - T034 is the gate.
- **Polish (Phase 6)** comes after all stories.

## Parallel examples

```text
# Phase 2
T006 ProjectCompile.h   ∥   T007 StdlibSelect.h

# US3 prep (any time after Phase 2)
T028 diag_parity/prelude_use/prelude_shadow fixtures   ∥   T029 C7 fixtures   ∥   T032 tools/check_no_subprocess.sh

# Polish
T035 CLAUDE.md   ∥   T036 checklists/neutrality.md
```

## Implementation strategy

1. **MVP = US2 + US1.** The extraction plus proof of neutrality is the unit's reason to exist (REQ-001, done-condition 1a/1b/1d). Land D-1 first: it is the smallest independent slice, and it proves the stub/CMake wiring.
2. Then D-2 + D-3 as one atomic move, the largest-risk step. Byte-identity (T022) is checked before moving on.
3. D-4 (blangd), then US3's ctests lock the seam for U2–U4.
4. **The converge gate** (workplan U1 (f)): `/speckit-converge` reports `converged`, or two converge→implement cycles leave only LOW/MEDIUM `unrequested` findings.

## Audit traceability

| Amendment | Tasks |
|---|---|
| F1 recorded-only view + C7 | T006 (`recorded`, `moduleIndices`), T016(g), T017, T018, T021, T029, T030 C7, T036 (13a) |
| F2 kept `gScope` install, reworded I3 | T006 (I3 comment), T014 (assert, never write), T024 |
| F3 nm-based `blangd_no_subprocess` (+ teeth) replacing the grep | T019 (inspection only), T032, T033, T034, T037 |
| F4 bcc call-site labels | T011 (a)–(d) |
| F5 verbatim `isspace` / literal-space scan | T007, T009, T030 C5 (tab case) |
| F6 name-keyed shadow test; `hadError() \|\| diag.hasErrors()` | T018, T021, T030 C1/C7 |
| F7 precise zero-golden-edit `git diff --diff-filter` gate | standing rule 2, T012, T022, T025, T034, T037 |
