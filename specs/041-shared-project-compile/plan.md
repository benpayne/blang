# Implementation Plan: Shared Project-Compile Entry Point (behavior-neutral extraction)

**Branch**: `041-shared-project-compile` | **Date**: 2026-11-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/041-shared-project-compile/spec.md`

**Epic / unit**: `docs/epics/modules-v2-lsp/` U1 · **Covers**: REQ-001 · **Closes**: epic done-condition 1 · **Binding record**: `docs/epics/modules-v2/overview.md` D1–D17 (not reopened)

---

## ⚠ The three constraints that govern every task in this plan

1. **Behavior-neutral.** `qcc`, `bcc` and `blangd` must not change in any observable way. That covers:
   - exit codes;
   - diagnostic text, location, severity, order and count, in human and `--json` form, with and without `-Werror` and `--debug-compiler`;
   - stdout/stderr, including `-v` traces and `--dump-locations`;
   - emitted IR, `.bmod` and schema output;
   - LSP transcripts.

   (spec FR-001)
2. **Zero golden edits.** At every commit of this branch, both of these must print nothing:

   ```bash
   git diff --no-renames --name-only --diff-filter=MDTRC epic/modules-v2-lsp...HEAD -- test_files/
   git diff --no-renames --name-only --diff-filter=A     epic/modules-v2-lsp...HEAD -- test_files/ | grep -v '^test_files/project_compile/'
   ```

   The first command proves no existing file under `test_files/` was modified, deleted, type-changed, renamed or copied. The second proves the only additions are the new ctest fixtures. Nothing under `test_files/` or `test_files/lsp/` is regenerated or hand-edited. The `.bmod` format version, `BuildCache` keys, the quarantine list and D1–D17 are untouched. (FR-002)
3. **Stop rule.** If any step appears to *require* a golden, diagnostic-text, exit-code or output change, the implementer stops. They do not "fix" or regenerate the golden. They end the turn with a `BLOCKER:` line naming the failing gate, the file, the diff and why it seems forced. Running `--update-goldens` is forbidden in this unit. (FR-003)

Every phase below ends with the regression checkpoint in [§Verification protocol](#verification-protocol). A red checkpoint is a bug in the extraction, never a golden to update.

---

## Design-audit amendments (2026-11-26)

The Principle VI design audit **passed with no STOP**, on condition that the amendments below are made before `/speckit-tasks`. They are folded into the sections they touch.

| Finding | Severity | Amendment | Where |
|---|---|---|---|
| F1 | HIGH | "Recorded" is a distinct step. A module that parsed fine can still be **dropped**: a `realpath` failure (qcc.cpp ~L640–656) or a wrong-format `.bmod` (~L670–700). `ModuleRecord::module` and the `recorded` flag are set **only** at the recorded step, after injection. Every downstream reader uses the recorded-only view: `moduleIndices()`, Sema, eager PROMOTE, the import-diagnostics pass, the prelude-shadow set and `bmodEmitScope`. Error paths therefore add no diagnostics (FR-001). New ctest case **C7**. | D-2, D-5, data-model.md, contract I9 |
| F2 | MEDIUM | `compileDocument` **keeps** `gScope = sResolver.globalScope();`. A local `Resolver` in `resolver_reuse`/`parser_no_leak` restores `gScope` to its saved (possibly null) value on destruction, so the alias must be re-installed per compile. I3 is reworded: **the caller installs the alias; `run` asserts it.** | D-4, contract I3 |
| F3 | promoted to REQUIRED | The no-subprocess/no-network guarantee is checked on the **binary**, not by grepping source. A new ctest `blangd_no_subprocess` asserts that `nm -D --undefined-only blangd` imports none of the forbidden symbols, with teeth (it is run against `bcc`, which must fail). | D-6 (new), Verification gate 5, contract I8 |
| F4 | correction | The `bcc` call-site labels were wrong: L798 is `runMigrate`, and L1698 is `main`'s single-file compile. | D-1 |
| F5 | correction | `scanImports` keeps bcc's exact rule: `isspace()` skipping, then a literal `"import "` (one ASCII space). | D-1, stdlib-select contract |
| F6 | correction | The shadow-set prose now states the real, name-keyed test `!preludeModuleNames.count(mn) && !moduleNamespaces.count(mn)`. That test includes `.bmod` modules and the user file. C1 asserts `hadError() \|\| diag.hasErrors()` against qcc's exit status. | D-2, D-3, D-5, data-model.md, research R10 |
| F7 | correction | The zero-golden-edit gate is an exact `git diff --diff-filter` command pair. New files are allowed only under `test_files/project_compile/`. | Constraint 2, Verification gate 1, quickstart |

## Summary

Move the rules that turn "an ordered list of inputs" into "resolved, analyzed modules plus diagnostics" out of `qcc.cpp main()` and into one shared frontend component, **`ProjectCompile`**. It is called by both `qcc` and `blangd`:

- **Moved out of `qcc.cpp`**: the per-input scope routing, prelude LOAD/PROMOTE, the `.bmod` pre-scan, format check and injection, origin/digest stamping, Sema, and the import-diagnostics pass.
- **Moved out of `bcc.cpp`**: `resolveStdlibFiles` and its `parseImports` text scan, into a dependency-free shared unit, **`StdlibSelect`**. It is linked by both `bcc` (which does not link the frontend) and the frontend.

`qcc` becomes a caller:

1. parse arguments;
2. build a `CompilePlan`;
3. call `ProjectCompile::run`;
4. run its own output stages (dump-locations, `.bmod` and schema emission, LLVM codegen, rendering, exit codes) on the returned `CompileContext`.

`lsp/Compile.cpp` calls the same `run`. It stays in single-file mode with an in-memory, origin-less input, so blangd's behavior is unchanged.

A new ctest, `project_compile`, drives `ProjectCompile::run` and `StdlibSelect` directly. It uses the `qcc` binary only as an **oracle** for the expected diagnostic text.

The technique is a **mechanical, order-preserving move**: the loop body is lifted verbatim into `ProjectCompile.cpp`, local variables become `CompileContext` fields, and command-line state becomes `CompilePlan` fields. No rule is rewritten. Every behavior-relevant ordering is pinned in [research.md](research.md) R3: `.bmod`-first reorder, per-file `continue`s, eager PROMOTE after Sema, and the import pass after the loop.

## Technical Context

**Language/Version**: C++17 (house style: tabs, spaces inside parentheses, Allman braces, `m`-prefixed members)

**Primary Dependencies**: none new. `ProjectCompile` uses only the existing frontend (`Module::Parse`, `Sema`, `DiagnosticEngine`, `Resolver`, `Frontend.h` helpers, `BmodFormat.h`). `StdlibSelect` uses only the C++ standard library plus POSIX `access(2)`. No LLVM, in either.

**Storage**: N/A. The inputs are files on disk or in-memory text. `StdlibSelect` only tests whether files exist.

**Testing**:
- existing: `run_tests.sh` (both build modes), `test_codegen.sh` (+ `--leak-check`, `--selfcheck`), `test_build/run_build_tests.sh`, `ctest` (`build`, `build-asan`), `test_lsp.sh`, `tools/check_no_field_reachins.sh`, `examples/*/test_*.sh`, `make -C demos run`;
- new: the `project_compile` ctest (C++ assert harness, same style as `ResolverTest.cpp` / `ParserLeakTest.cpp`).

**Target Platform**: Linux (CI) and macOS (POSIX `realpath`/`access`, as today).

**Project Type**: Compiler (frontend library + CLI drivers + language server).

**Performance Goals**: No regression. The extraction adds one plan-to-context indirection per input. Compile wall time on `examples/kv/main.b --combine` stays within noise of the ~0.09 s baseline (design.md).

**Constraints**:
- behavior-neutral (constraint 1);
- no LLVM in `ProjectCompile`/`StdlibSelect`;
- `StdlibSelect` must not pull the parser into `bcc`;
- KG-7 ownership: everything the context builds is owned by the context and released before the `Resolver`;
- no new process or network use anywhere (blangd invariant C4).

**Scale/Scope**:
- `qcc.cpp`: about 560 lines move out (the parse loop at L267–L820, the import pass at L797–L824, and `injectBmodSymbols`);
- `bcc.cpp`: about 90 lines move out (two forward declarations plus the `parseImports` and `resolveStdlibFiles` bodies);
- `lsp/Compile.cpp`: about 20 lines change.

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design (below).*

| Principle / Gate | Status | How this plan satisfies it |
|---|---|---|
| I. One Right Way (spec fidelity) | PASS | One implementation of each compile rule (SC-002). The plan *removes* duplication; it adds no new language surface. `CLAUDE.md` gets a repository-structure entry for the two new files (Gate 5). |
| II. Test-Gated Changes | PASS | New `project_compile` ctest (FR-019). Every existing gate stays green with zero golden edits (SC-001). Neutrality is spot-checked byte-for-byte on six real programs (SC-005, quickstart §3). |
| III. Reject, Don't Coerce | PASS (n/a) | No diagnostic is added, removed or reworded. Error-continue semantics are preserved exactly. |
| IV. Memory/Thread Safety Verified | PASS | KG-7 ownership kept explicit in `CompileContext` (data-model.md). `project_compile` and `parser_no_leak` run LSan-clean under `build-asan`. `--leak-check` stays green. |
| V. House Style | PASS | New files follow the existing conventions (PascalCase files, `m` members, tabs). |
| VI. Principled Design | PASS (audit pending) | This plan is the design artifact. The architect's Principle VI design audit of this plan happens **before** implementation (workplan U1 Audit). The design is the general seam U2–U4 consume (plan-in → context-out), not a point fix. |
| Gate 1 both build configs | PASS | `ProjectCompile` is in `BLANG_FRONTEND_SOURCES`; `StdlibSelect` is in the frontend sources and the `bcc` target. The parse-only build compiles both. LLVM-only code stays in `qcc.cpp` under `#ifdef BLANG_HAS_LLVM`. |
| Gate 7 security/maintainability | PASS | The untrusted-input handling (malformed foreign-type header, format mismatch, realpath failure) moves verbatim with its located-error behavior. No new input surface. The maintainability review focus is the API seam (contracts/). |

No violations, so Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/041-shared-project-compile/
├── spec.md                       # /speckit-specify output (committed ce94cf5)
├── plan.md                       # this file
├── research.md                   # Phase 0: decisions R1–R10
├── data-model.md                 # Phase 1: CompilePlan / CompileInput / CompileContext / ModuleRecord
├── contracts/
│   ├── project-compile-api.md    # ProjectCompile.h contract (signatures + invariants)
│   └── stdlib-select-api.md      # StdlibSelect.h contract
├── quickstart.md                 # Phase 1: validation run guide (neutrality proof)
├── checklists/requirements.md    # spec quality checklist (all pass)
└── tasks.md                      # Phase 2 (/speckit-tasks; not created here)
```

### Source Code (repository root)

```text
ProjectCompile.h / ProjectCompile.cpp   NEW  shared entry point (frontend, no LLVM)
StdlibSelect.h   / StdlibSelect.cpp     NEW  stdlib-set selection + import-name scan (no parser, no LLVM)
StringLexerReader.h                     NEW  (moved from lsp/) in-memory LexerReader, QLang namespace
lsp/StringLexerReader.h                 EDIT  becomes a one-line alias to the root header (keeps existing includes compiling)
qcc.cpp                                 EDIT  thin caller: args → CompilePlan → run → output stages
bcc.cpp                                 EDIT  remove resolveStdlibFiles/parseImports bodies + fwd decls; call StdlibSelect
lsp/Compile.cpp / lsp/Compile.h         EDIT  call ProjectCompile::run (single-file, in-memory, IdentityPolicy::kNone)
CMakeLists.txt                          EDIT  add ProjectCompile.cpp + StdlibSelect.cpp to BLANG_FRONTEND_SOURCES;
                                              StdlibSelect.cpp to bcc; new projectCompileTest + add_test(project_compile)
ProjectCompileTest.cpp                  NEW  the project_compile ctest driver
test_files/project_compile/             NEW  fixture inputs for the ctest ONLY (new files — not goldens of any
                                              existing suite; see R8 on why this is not a golden edit)
CLAUDE.md                               EDIT  repository-structure entries for the new files (docs gate)
```

**Structure Decision**: root-level frontend files, matching the existing layout. `Resolver.*`, `Sema.*` and `BmodEmitter.*` all live at the root and are listed in `BLANG_FRONTEND_SOURCES`, so no new directory or library target is introduced. The `project_compile` fixtures go in a **new** directory that no existing runner globs. `run_tests.sh` walks only `pass/`, `fail/`, `cgfail/` and `xfail/` (verified by T-gate), so adding them cannot change any existing suite's counts. See research R8.

## Design

The full API is in [contracts/project-compile-api.md](contracts/project-compile-api.md) and [contracts/stdlib-select-api.md](contracts/stdlib-select-api.md). Entities and ownership are in [data-model.md](data-model.md). The essentials follow.

### D-1. `StdlibSelect` — stdlib set + import scan (FR-016/017/018)

```cpp
namespace BlangStdlib {
	// Leading identifier of each line that begins (after whitespace) with "import ",
	// after stripping a trailing // comment. Byte-for-byte bcc's parseImports rule,
	// on TEXT (bcc reads the file; blangd will pass its buffer in U2).
	std::set<std::string> scanImports( const std::string &sourceText );

	// bcc's resolveStdlibFiles rule: base {sys,buffer,collections,fs,net} always,
	// then gated {timer,math,time,random,env,cli} in that order, then any other
	// imported name — each only if <stdlibDir>/<name>.b exists (access F_OK).
	std::vector<std::string> selectStdlibFiles( const std::string &stdlibDir,
		const std::set<std::string> &imports );
}
```

- **Path-string identity.** Today the candidate is `exeDir + "/stdlib/" + name + ".b"`. `bcc` will call `selectStdlibFiles( exeDir + "/stdlib", … )` and the function builds `stdlibDir + "/" + name + ".b"`, which yields the **identical string**. That matters because these paths appear in `qcc` diagnostics and `bcc -v` output (R5).
- **`bcc` call sites.** The four sites switch to `BlangStdlib::selectStdlibFiles( exeDir + "/stdlib", BlangStdlib::scanImports( readFileToString( f ) ) )`:
  - `compileAndRunTestFile` (L571; `bcc test`);
  - `runMigrate` (L798–L800; `bcc migrate`, unions the scans of every `sourceFiles` entry);
  - `buildProject` (L1336–L1339; `bcc build`, unions the scans of every project source);
  - `main`'s single-file compile (L1698; `bcc file.b`).

  The union loops stay in `bcc` exactly as written. Only `parseImports( f )` becomes `BlangStdlib::scanImports( readFileToString( f ) )`, and `resolveStdlibFiles( exeDir, I )` becomes `BlangStdlib::selectStdlibFiles( exeDir + "/stdlib", I )`. Every forward declaration (`parseImports` at L555/L739, `resolveStdlibFiles` at L556/L740) and both bodies (L965, L1002) are deleted.
- **`scanImports` rule, verbatim from `parseImports` (F5).** For each `getline` line:
  1. truncate at the first `"//"`, even one inside a string literal;
  2. skip characters while `isspace( (unsigned char)c )`;
  3. require `line.compare( i, 7, "import " ) == 0`: a literal `import` followed by exactly one ASCII space, so a tab after `import` does not match;
  4. skip `isspace` characters again;
  5. take the maximal `isalnum || '_'` run and insert it if it is non-empty.

  Block comments are not recognized. The function is a copy of the existing body with `readFileToString( path )` replaced by the `sourceText` parameter, and nothing else changes.
- **Missing-file behavior.** `readFileToString` on a missing file behaves exactly as today, because it is unchanged and stays in `bcc`.
- **Linkage.** `StdlibSelect.cpp` is added to `add_executable(bcc …)` and to `BLANG_FRONTEND_SOURCES`. It includes only `<set> <string> <vector> <unistd.h>`.

### D-2. `CompilePlan` → `ProjectCompile::run` → `CompileContext` (FR-004–013)

```cpp
namespace QLang {
struct CompileInput {
	std::string path;          // as given; diagnostics, module name, IR name all derive from it
	enum class Kind { kSource, kInterface } kind;   // kInterface == ".bmod" (classifyInput())
	bool inMemory = false;     // true: lex `text`; false: read `path` from disk (today's readers)
	std::string text;          // only when inMemory
	std::string origin;        // explicit canonical origin (--bmod-origin for an interface); may be empty
};
enum class IdentityPolicy { kCanonical, kNone };
struct CompilePlan {
	std::vector<CompileInput> inputs;    // command-line order (run() applies the .bmod-first reorder)
	bool combine = false;                // --combine
	std::string moduleOrigin;            // --module-origin (applies to kSource inputs)
	IdentityPolicy identity = IdentityPolicy::kCanonical;
	bool lexerTrace = false;             // qcc: verbose && !dumpLocations
};
class CompileContext { … see data-model.md … };
class ProjectCompile {
public:
	static std::unique_ptr<CompileContext> run( const CompilePlan &plan,
		Resolver &resolver, DiagnosticEngine &diag );
};
CompileInput::Kind classifyInput( const std::string &path );  // ".bmod" suffix rule, shared
std::string moduleNameOf( const std::string &path );          // basename sans extension, shared
}
```

**What `run` does** is today's `qcc.cpp` L248–L824, in today's order:

1. Install `gDiag = &diag` and save the previous alias. The `Resolver` has already installed `gScope`.
2. Apply the stable `.bmod`-first reorder of `plan.inputs`.
3. In combine mode, create `preludeScope` (parent = global) and `combineScope` (child of prelude). Both come from `resolver.newModuleScope` and are adopted into `ctx->mOwnedModuleScopes`.
4. Per input, in order:
   - **route**: user scope, prelude holding namespace, stdlib namespace, or a non-combine module scope;
   - for an interface input, the foreign-type pre-scan;
   - lex (`LexerReader` from disk, or `StringLexerReader` over the in-memory text);
   - `Module::Parse`;
   - `stampDefiningOrigin`;
   - origin selection and `stampModuleDigest` (skipped under `kNone`);
   - for an interface input: the format check, `setExtern`, `setFromInterface`, and `injectBmodSymbols`;
   - **record** the module: set `record.module` and `record.recorded = true`, append to `moduleIndices`, and for a source input set `bmodEmitScope`. This is the point where today's code pushes onto `modules` / `moduleScopes` / `moduleNames` (qcc.cpp ~L737–L752);
   - `PARSE_TRACE("Completed parse")`;
   - Sema;
   - eager PROMOTE.

   The `continue`-on-failure points are exactly today's.

   **Drop points (F1).** A record whose input fails *before* the recorded step stays unrecorded: `recorded == false` and `module == null`. Its parsed `SmartPtr<Module>` is a local that is released at the end of the iteration, exactly as today's `mod` local. There are five such points:
   - a malformed foreign-type header;
   - the global-`hadError` skip after the pre-scan;
   - a catastrophic parse failure;
   - a `realpath` failure;
   - a wrong-format `.bmod`.

   Each sets `hadError` and emits only the diagnostic it emits today. A dropped module is **never** analyzed, promoted, scanned by the import pass, counted in the shadow set, or chosen as `bmodEmitScope`. Its `import` lines therefore produce no unknown-module or unused-import diagnostic, matching today's behavior, where it never reached `modules`. Only `record.scope` is set at routing time, because blangd needs a non-null `fileScope` even on failure.
5. Run the import-diagnostics pass over **recorded** modules only (today: `modules`), gated on `combine || anyInterfaceInput`. The gate counts interface *inputs*, recorded or not, exactly as today's `!bmodFiles.empty()`.
6. Compute the prelude-shadow set over **recorded** modules only. This is the non-LLVM half of today's L918–L935, moved verbatim:
   - for each recorded module `mn` with `!preludeModuleNames.count(mn) && !moduleNamespaces.count(mn)`, collect its struct names. That test is name-keyed, so it also admits `.bmod` interface modules and the combine user file;
   - `suppressedPrelude` = those names ∩ the prelude manifest.

   It moved so that codegen consumes data instead of re-deriving it.
7. Restore `gDiag` and return.

**`run` never calls `diag.finish()`** (FR-011). Rendering stays with the caller.

**`IdentityPolicy::kNone`** is used only by blangd in U1. It skips the whole origin/digest block, so there is no `realpath` and no `stampModuleDigest`. That reproduces today's `lsp/Compile.cpp` exactly: it calls `stampDefiningOrigin` but never stamps a digest. See research R4.

**Disk vs. in-memory reading.** A disk input keeps **today's three readers byte-for-byte**:
- an `ifstream` line scan for foreign-type headers;
- `LexerReader( path )` for lexing;
- an `ifstream` 8-line probe for the format marker.

The in-memory path runs the same two scans over the text through one shared line-iterator helper, and lexes with `StringLexerReader`. Using one helper for both paths is the plan. If that ever makes a disk-input diagnostic differ, the stop rule applies (R6).

### D-3. `qcc.cpp` after the move (FR-014)

`main()` keeps:
- argument parsing and usage errors (L54–L246, unchanged);
- `DiagnosticEngine` setup;
- `Resolver resolver;`;
- `setParseTraceEnabled( verbose && !dumpLocations )`.

It then builds the plan (`CompileInput` per argument, `classifyInput`, `bmodOrigins[path]` copied onto interface inputs, `combine`, `moduleOrigin`, `lexerTrace`) and calls `auto ctx = ProjectCompile::run( plan, resolver, diagnostics );`.

The output stages read from `ctx`. Their logic is unchanged; only the data source changes:

| Output stage | Reads from `ctx` |
|---|---|
| error exit (`hadError \|\| gDiag->hasErrors()`) | `ctx->hadError() \|\| diagnostics.hasErrors()`: both halves, as today |
| `--dump-locations` | `ctx->modules()` |
| `--emit-bmod` | non-extern modules + `ctx->bmodEmitScope()` |
| `--emit-schema` | `ctx->modules()` |
| LLVM: `allStructs` filter | `ctx->isPreludeProvider(i)` + `ctx->suppressedPrelude()` |
| LLVM: module prefix | `ctx->codegenPrefix(i)`, which encodes today's **name-keyed** `moduleNamespaces.count(fname) && !preludePrefixExempt.count(fname)` rule verbatim (not a role test; see research R10) |
| LLVM: per-module IR name, codegen failure message | `ctx->record(i).path` (replaces today's `inputFiles[idx]`; equivalent because codegen runs only on an error-free compile, where `modules` and the reordered inputs align — R3) |
| combined IR name | its own `sourceFiles` list, built in the same reorder |

**Ownership order in `main`** is the KG-7 order: `Resolver resolver;` is declared before `ctx`, so `ctx`, and with it every module scope and AST, is destroyed first.

What is removed from `qcc.cpp`:
- the `isUserFile` / `isPreludeModule` branch;
- `injectBmodSymbols`;
- the pre-scan, the format check and the origin block;
- the `Sema::analyze` call;
- the import-diagnostics loop;
- `ownedModuleScopes` / `adoptModuleScope`.

These are the SC-003 grep targets.

### D-4. `lsp/Compile.cpp` after the move (FR-015)

```cpp
static Resolver sResolver;                      // unchanged: process-lifetime global scope
gScope = sResolver.globalScope();               // KEPT (F2): the CALLER installs the alias; run() asserts it.
                                                // A test-local Resolver (resolver_reuse, parser_no_leak) restores
                                                // gScope to its saved value on destruction, so re-install per call.
DiagnosticEngine engine;                        // unchanged: fresh per reparse, never finish()ed
CompilePlan plan;
plan.inputs.push_back( { path, kSource, /*inMemory*/ true, text, /*origin*/ "" } );
plan.combine = false;
plan.identity = IdentityPolicy::kNone;
result.context = ProjectCompile::run( plan, sResolver, engine );
result.module    = result.context->record( 0 ).module;   // null unless recorded; under kNone with no interface
                                                         // inputs, recorded == parsed, so null only on a catastrophic parse, as today
result.fileScope = result.context->record( 0 ).scope;    // always non-null, as today
result.diagnostics = engine.diagnostics();
```

- **`CompileResult`** gains `std::shared_ptr<QLang::CompileContext> context`. It is a `shared_ptr` because `Server` stores results in a `std::map` by value. The existing `module`/`fileScope` `SmartPtr`s stay, so `Server.cpp` and `AstLocator` are untouched.
- **The `gDiag` save/restore** that `compileDocument` does today now happens inside `run`. Its observable effect is identical. The **`gScope` install stays in `compileDocument`** (F2): `run` never writes `gScope`, it only asserts `gScope == resolver.globalScope()` (contract I3).
- **Why it matches today's blangd exactly:**
  - non-combine, so no prelude scope and no namespaces;
  - no interface inputs, so no import pass ("not authoritative"), no `.bmod` reads and no global-scope namespaces;
  - `kNone`, so no `realpath` and no digest;
  - `stampDefiningOrigin( module, path )` and `Sema::analyze( module, scope, engine )` run in the same order;
  - the lexer is `StringLexerReader( text, path )`, as today.

### D-5. `project_compile` ctest (FR-019)

`ProjectCompileTest.cpp` links `${BLANG_FRONTEND_SOURCES}` (not `lsp/`, not `qcc.cpp`). It is registered as:

```cmake
add_test(NAME project_compile
         COMMAND projectCompileTest $<TARGET_FILE:qcc> ${CMAKE_CURRENT_SOURCE_DIR})
```

Each case constructs its **own** `Resolver` in a nested block, so the global scope and any global-scope namespace state never cross cases. The context is destroyed before its resolver: the KG-7 order, LSan-checked under `build-asan`.

| Case | Inputs (`stdlibDir = <src>/stdlib`) | Asserts |
|---|---|---|
| C1 diag parity | `selectStdlibFiles(stdlib, scanImports(fixture))` + `test_files/project_compile/diag_parity.b` (an `import net;` that is unused → warning; an `import nonesuch;` → error; a Sema type error) | `run` rendered through `DiagnosticEngine(ostringstream)` + `finish()` == stderr of `qcc --parse-only --combine <same paths, same order>` (popen; **qcc is the oracle, not the system under test**; the popen command string is prefixed `ASAN_OPTIONS=detect_leaks=0`, child-only, because under `build-asan` the oracle's pre-existing error-exit leak would otherwise append a LeakSanitizer report to the compared stderr — the test process keeps leak detection on). Also: `( ctx->hadError() \|\| diag.hasErrors() )` == (qcc exit ≠ 0), the same two-part test qcc's error exit uses (F6). Both human and `--json` renderings are compared (json via `setJson(true)` vs `qcc --json`). |
| C2 prelude resolution | base stdlib + `prelude_use.b` (zero imports, `Map<string,int>` and `Set<int>` declared) | `ctx->preludeScope()->findType("Map")` and `("Set")` non-null; the user scope has **no own** entry for them (lookup from the user scope resolves to the prelude scope's symbol, pointer-equal); no diagnostics |
| C3 namespace registration | the same base set | `ctx->userScope()->findNamespace("net")` non-null; `ctx->record(i)` for `stdlib/net.b` has role `kNamespaced`; `codegenPrefix` == `"net"`; buffer/collections have role `kPreludeProvider` and an empty prefix |
| C4 user shadowing | base stdlib + `prelude_shadow.b` (user `struct Map` with a distinct API) | user-scope lookup of `Map` == the user's `StructDefinition` (pointer-equal to the struct in the user module's struct list); `ctx->suppressedPrelude()` contains `"Map"` and not `"Set"`; no diagnostics |
| C5 stdlib selection | `scanImports` over a text with `import cli;`, `import timer;`, `// import math;`, `   import   env.x;`, `import zzz_unknown;` | exact ordered `selectStdlibFiles` output: `sys,buffer,collections,fs,net,timer,env,cli` (the commented-out math excluded; the unknown name dropped because no file exists); scan set == `{cli,timer,env,zzz_unknown}` |
| C7 dropped-module parity (F1) | combine plan: base stdlib + `test_files/project_compile/dropped_user.b` + `test_files/project_compile/wrong_format.bmod`. The `.bmod` has a `// blang-bmod-format: 4` marker (≠ `kFormatVersion` 5), parses cleanly, and contains an `import net;` and an `import nonesuch;` line, so if it were wrongly recorded the import pass would add an unknown-module error and an unused-import warning. The user file uses nothing from it. | rendered text (human and `--json`) == `qcc --parse-only --combine <same paths, same order>` stderr; exactly one error, the format-mismatch diagnostic at `wrong_format.bmod:1:1`; the `.bmod`'s record has `recorded == false` and `module == null`; `moduleIndices()` excludes it; `( hadError() \|\| diag.hasErrors() )` == (qcc exit ≠ 0). The case asserts its own premise first (the fixture's marker differs from `BlangBmod::kFormatVersion`), so a future format bump fails loudly instead of silently voiding the case. |
| C6 single-file / kNone | one in-memory input at a **nonexistent** path, `kNone` | no `realpath` diagnostic; `record(0).scope` non-null; diagnostics equal those from the pre-unit `lsp::compileDocument` shape (asserted structurally: same count and same messages as a direct `Module::Parse` + `stampDefiningOrigin` + `Sema` on a fresh resolver scope) |

### D-6. `blangd_no_subprocess` ctest (F3, required)

This is a binary-level proof of invariant C4 / I8. It replaces the source grep.
- **The script.** `tools/check_no_subprocess.sh <binary>` runs `nm -D --undefined-only <binary>` and fails if any imported symbol, ignoring its `@GLIBC_*` version suffix, is exactly one of:
  - `popen`, `system`, `fork`, `vfork`;
  - `execl`, `execlp`, `execle`, `execv`, `execvp`, `execvpe`, `execve`, `fexecve`;
  - `posix_spawn`, `posix_spawnp`;
  - `socket`, `connect`, `getaddrinfo`.
- **Static-link guard.** It also fails if the binary has **zero** undefined dynamic symbols, so a static link cannot pass vacuously.
- **Registration:**

  ```cmake
  add_test(NAME blangd_no_subprocess COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_subprocess.sh $<TARGET_FILE:blangd>)
  add_test(NAME blangd_no_subprocess_teeth COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_no_subprocess.sh $<TARGET_FILE:bcc>)
  set_tests_properties(blangd_no_subprocess_teeth PROPERTIES PASS_REGULAR_EXPRESSION "fork")
  ```

  The teeth leg is real: `bcc` imports `fork`, `execvp`, `popen` and `system` (verified 2026-11-26).
- **Baseline.** `blangd` imports none of the forbidden symbols today, in either `build/` or `build-asan/` (verified 2026-11-26).
- **Goldens.** It edits no golden and runs in every build directory.
- **Why a ctest** rather than a `test_lsp.sh` leg: CI's `runtime-units` job already gates on `ctest` in `build` and `build-asan`.
- **Scope.** This checks the shipped `blangd` binary, which is what C4 promises. `ProjectCompileTest` legitimately uses `popen` for its oracle and is not checked.

**Oracle availability.** `qcc` is built in every configuration (parse-only included) and `--parse-only --combine` needs no LLVM, so C1 runs in all three build directories. The test fails loudly if the oracle binary is absent; it never skips silently.

## Verification protocol

The gates below run at **every phase checkpoint** (after D-1, after D-2+D-3, after D-4, after D-5), on `build` (LLVM), `build-nollvm` (parse-only; `BLANG_ENABLE_LLVM=OFF`) and `build-asan`:

1. The two zero-golden-edit commands in constraint 2 both print nothing (F7).
2. `./run_tests.sh` in both modes, at the baseline counts (evaluation.md: 251/0 LLVM, 244/0 parse-only; re-measured on the base **before** Phase D-1, with any pre-existing red reported and not absorbed).
3. `./test_codegen.sh` (172/0), `--leak-check` (Leaks: 0), `--selfcheck` (prints `SELFCHECK: OK`, exits non-zero).
4. `test_build/run_build_tests.sh` passes (this exercises `bcc build`'s stdlib selection, `.bmod` injection, origins, aliasing, diamond, git dep and ASan counter legs).
5. `ctest --test-dir build` and `ctest --test-dir build-asan` pass. The baseline is 82/82. After D-5 and D-6 it is 85/85, adding `project_compile`, `blangd_no_subprocess` and `blangd_no_subprocess_teeth` (PASS_REGULAR_EXPRESSION "fork" on the offender listing). `parser_no_leak`, `resolver_component` and `resolver_reuse` stay unmodified and green.
6. `./test_lsp.sh` (70/0) and `./test_lsp.sh --selfcheck`.
7. `tools/check_no_field_reachins.sh` (+ `--selfcheck`), `examples/*/test_*.sh`, `make -C demos run`.
8. **Byte-identity spot check** (SC-005, quickstart §3): for the six overview files, capture exit code + stderr of `qcc --parse-only <file>` and of the `qcc` line `bcc -v -S <file>` prints, from a **pre-unit** `qcc`/`bcc` built at `epic/modules-v2-lsp`, then `cmp` against the post-unit binaries. Add one `--json` and one `-Werror` run, one `--dump-locations` (`test_files/pass/func_simple.b`) and one `--emit-bmod` (`test_build/mathlib` sources), all `cmp`-identical.

Any red is handled under the stop rule (constraint 3).

## Phasing (for /speckit-tasks)

1. **Baseline**: build all three configurations at the base, run the gates, record the counts and the pre-unit binaries for gate 8.
2. **D-1 `StdlibSelect`**: add the files, switch `bcc` over, delete the bcc bodies, then checkpoint. This is independent of the rest and the smallest step.
3. **`StringLexerReader` relocation**: root header plus the `lsp/` alias, then checkpoint (pure move).
4. **D-2 + D-3**: create `ProjectCompile` by lifting the loop out of `qcc.cpp` verbatim, then make `qcc` a caller in the same commit so there is never a duplicate. Checkpoint.
5. **D-4**: blangd through `run`, then checkpoint (gate 6 especially, and gate 5's `parser_no_leak`).
6. **D-5 + D-6**: the `project_compile` ctest (C1–C7) and fixtures, plus the `blangd_no_subprocess` ctest and its teeth, then checkpoint.
7. **Docs**: `CLAUDE.md` entries for `ProjectCompile.*`, `StdlibSelect.*`, root `StringLexerReader.h` and the new ctest. Update the `lsp/Compile.cpp` comment that says the module-graph methods are dormant. The CLAUDE.md single-file wording stays until U5.
8. **SC-003 greps** and the final full checkpoint. (The ProjectCompile.cpp/StdlibSelect.cpp source grep for popen/fork/exec is superseded by D-6's binary check, F3.)

## Assumptions (carried forward for the design audit to weigh)

These are the spec's judgment calls, restated with their plan consequences. Each was recorded as a `DECISION:` in the specify turn.

- **A1. FR-018: `parseImports` moves too (UNCONFIRMED; derived from the overview constraint "every rule blangd applies must come from the shared entry point").** The workplan names only `resolveStdlibFiles`. The plan moves the text scan as `BlangStdlib::scanImports( text )`. `bcc` keeps file reading (`readFileToString`), which keeps it neutral.

  *Audit question:* is a crude line scan (it ignores `/* */` blocks; it takes only the leading identifier of a dotted path) the rule we want blangd to inherit? The plan's answer is **yes for U1**: it is bcc's rule, and agreeing with bcc is the contract. Improving it would change bcc's stdlib set, which is a `bcc` behavior change and out of scope.

  *If the PM declines:* delete `scanImports`, keep `parseImports` local to `bcc`, and U2 must take the scan by some other shared route. This touches only D-1, C5 and one `bcc` helper.
- **A2. No digest on unsaved/in-memory blangd input (`IdentityPolicy::kNone`) (UNCONFIRMED).** blangd today stamps `stampDefiningOrigin` but **never** `stampModuleDigest` and never calls `realpath`. The plan adds an explicit `kNone` policy rather than having blangd pass a fake origin.

  *Audit questions:*
  - Is a policy enum the right seam? The alternative is "empty origin means skip", which would silently change qcc, where an empty origin means "use `realpath`".
  - Should U2 retire `kNone`? The plan's answer: U2/U3 plans carry real origins for on-disk documents. Whether an unsaved, never-saved buffer keeps `kNone` or gets a synthetic origin is U2's decision.

  Digests affect only mangling, not diagnostics, so `kNone` is diagnostic-neutral by construction.
- **A3. Dependency namespaces stay on the global scope (deferred to the U3 design audit).** `injectBmodSymbols` registers each dependency namespace on the resolver's global scope (`gScope->addNamespace`; the namespace is adopted by global via `setParent`). It does this because the global scope is the common ancestor of the combine user scope and non-combine module scopes. The move preserves that byte-for-byte.
  - **Harmless in U1:** `qcc` is one compile per process, blangd sends no interfaces, and the ctest uses a fresh `Resolver` per case.
  - **Not harmless for U3:** a long-lived blangd feeding dependency interfaces would accumulate namespaces on the process global scope, violating C3 (and, worse, a stale namespace from a previous reparse could satisfy `findNamespace`).
  - **The plan's seam for U3:** `CompileContext` already owns every scope it creates, so U3 can introduce a context-owned "dependency scope" between the global and prelude scopes. That changes the non-combine scope chain, so it is **not** behavior-neutral and does not belong here.

  *Audit question:* does the contract need to reserve that hook now? The plan's answer: no API reservation is needed. The change is internal to `run`, and the contract (contracts/project-compile-api.md, invariant I6) documents the current global-scope adoption as a known U3 item.
- **A4. Branch name** `041-shared-project-compile`, without the `specs/` prefix, per the ceremony's "named exactly after its spec directory" and its example.
- **A5. Fixture directory `test_files/project_compile/` is new, not a golden edit.** The zero-golden-edit rule protects existing expected outputs. New input fixtures for a new test, globbed by no existing runner, change no existing result. The alternative (fixtures embedded as C++ string literals) works for C2–C5. But C1 needs *on-disk paths* so that `qcc`'s rendered paths match, so real files are required. If the PM reads "zero edits under `test_files/`" literally, the fixtures move to `tests/project_compile/`. That is a path change only.

## Constitution re-check (post-design)

Unchanged: all PASS.
- The design adds two frontend translation units and one ctest.
- It removes three duplicated rule sites (the qcc routing, the bcc selection, and the future blangd copy that is now prevented).
- It introduces no new input surface, process or network use.
- Ownership stays explicit (data-model.md "Ownership").

## Complexity Tracking

No constitution violations to justify.
