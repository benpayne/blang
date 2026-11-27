---
description: "Task list for U4 — cross-module go-to-definition and hover in blangd"
---

# Tasks: Cross-module go-to-definition and hover in blangd

**Input**: Design documents from `specs/044-lsp-xmodule-navigation/`. These are `plan.md`, `spec.md`, `research.md` (R1–R8), `data-model.md`, `contracts/navigation.md` and `quickstart.md`.

**Tests**: Tests are required. Workplan U4 (a)–(c) and spec FR-011 mandate transcript goldens, and constitution II mandates regression tests. Every golden is a `test_files/lsp/xmod_*.lsp.jsonl` script with an `.expected.out` file. `./test_lsp.sh --update-goldens` generates the `.expected.out` files, but **only after** the produced output has been checked by hand against the expected strings in `contracts/navigation.md`. If a mismatch appears, fix the code or record a spec amendment. Never accept a mismatch silently.

**Hard constraints (every task)**:

- Never modify a pre-existing `test_files/lsp/*.expected.out` file. That includes every `defn_*`, `hover_*` and `symbols_*` golden.
- Leave same-file definition, hover and documentSymbol output byte-identical. `lsp/AstLocator.*` is not edited.
- blangd must not spawn processes or touch the network.
- Every rule blangd uses comes from the shared frontend, which `qcc` also calls.
- New mapping state is owned by `CompileResult` or `CompileContext`, so `parser_no_leak` stays LSan-clean.
- A definition never resolves to a `.bmod` location. When no unique source match exists, the answer is `null`.

**Settled decisions (manager, 2026-11-26)**:

- A type name in a declaration (type position) navigates **only** when its target is in another file. Same-file type positions stay `null`.
- Foreign struct hover includes the generic parameters (`struct Map<K, V>`). Same-file struct hover is unchanged (`struct Point`).
- The FR-008 display rule is design.md D4 plus its addendum.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: The task can run in parallel. It touches different files and depends on no incomplete task.
- **[Story]**: The spec user story the task belongs to (US1–US5).

---

## Phase 1: Setup (baseline capture)

**Purpose**: Freeze a byte-for-byte reference of the `043` compiler before any shared-frontend edit, and record the pre-U4 navigation baseline.

- [ ] T001 Create a detached worktree of the U3 head with `git worktree add /tmp/blang-043 7e6183c` (branch `043-lsp-project-context`, which is not modified). Build it in both modes: `cmake -S /tmp/blang-043 -B /tmp/blang-043/build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build /tmp/blang-043/build -j`, and `cmake -S /tmp/blang-043 -B /tmp/blang-043/build-nollvm -DBLANG_ENABLE_LLVM=OFF && cmake --build /tmp/blang-043/build-nollvm -j`. These are the reference `qcc` binaries for T009/T010.
- [ ] T002 Write the equivalence harness `specs/044-lsp-xmodule-navigation/qcc_equivalence.sh`. It takes `--ref <043 build dir> --new <build dir> [--parse-only]` and compares, for every input, the **exit code, stdout+stderr and output files byte for byte** (`cmp`) between the reference `qcc` and the new `qcc`. The inputs are:
  - (a) every `test_files/codegen_*.b`, compiled through the `qcc` line that `bcc -v -S <file>` prints. This compares the `.ll` in LLVM mode, or the diagnostics and `--dump-locations` output in `--parse-only` mode;
  - (b) every `test_files/pass/*.b`, `fail/*.b`, `fail/sema/*.b` and `cgfail/*.b` under plain `qcc`. This compares the diagnostics and exit code;
  - (c) every `test_build/*` library, via the `qcc --emit-bmod` line that `bcc build -v` prints. This compares the `.bmod` text and, in LLVM mode, the `.ll`;
  - (d) every `test_build/*` binary's final `qcc` line from `bcc build -v`. This compares the `.ll`.

  The `qcc` command lines are taken **once**, from the LLVM build's `bcc` (`build/bcc -v -S` / `bcc build -v`). Each line is replayed against both `qcc` binaries of a pair, with `--parse-only` added for the non-LLVM pair (F4). The script prints a per-category input count for (a), (c) and (d) (`EQUIV(a): n inputs`, …) and **fails if any count is 0**. The script runs each pair in a scratch directory, prints `EQUIV: N/N identical` and exits non-zero on the first difference, naming the file. It has a `--selfcheck` that flips one byte of one reference output and asserts the harness goes red.
- [ ] T003 [P] Record the pre-U4 navigation baseline in `specs/044-lsp-xmodule-navigation/research.md` (R1 table, already present). Confirm it by running the probe script from research.md against `build/blangd`, and save the transcript as `specs/044-lsp-xmodule-navigation/probe-baseline.txt` for the review record.

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: Add the shared-frontend pieces every story consumes, prove them byte-for-byte neutral for `qcc`, and create the blangd navigation skeleton. **No user story work starts until T010 is green.**

### Shared-compiler change A: use-capability type set and member lookup (research R3)

- [ ] T004 In `ProjectCompile.h`/`ProjectCompile.cpp`, add `void CompileContext::useCapabilityTypes( std::vector<SmartPtr<StructDefinition>> &structs, std::vector<SmartPtr<EnumDefinition>> &enums ) const`. Move the `allStructs`/`allEnums` collection loop out of `qcc.cpp` (currently lines ~397–417, inside `#ifdef BLANG_HAS_LLVM`) **verbatim**: iterate the recorded modules in record order, and skip a prelude provider's struct whose name is in `suppressedPrelude()`. The new code must have no LLVM dependency.
- [ ] T005 In `qcc.cpp`, replace the inline loop with `ctx->useCapabilityTypes( allStructs, allEnums );`. Keep the comment explaining the shadow filter. `registerExternalTypes` receives identical vectors in identical order.
- [ ] T006 In `Type.h` (StructDefinition), add `FunctionDefinition *findMethod( const std::string &name ) const`. It is the first method in `mMethods` whose `getName() == name`. In `Sema.cpp` (method-call resolution, ~line 1716), replace the inline loop with `findMethod`. Factor the receiver-base-type derivation Sema uses for a `MethodCallExpression` into a public static helper, declared in `Sema.h`, that blangd can call. Sema's behavior must not change at all: the stamping, `setResolvedType` and the "has no method" diagnostic paths stay exactly as they are.

### Shared-compiler change B: parser type-reference recording (research R4)

- [ ] T007 In `ProjectCompile.h`, add:
  - `struct TypeReference { SourceLocation location; uint32_t length; const Symbol *target; };`, where `target` is non-owning;
  - `std::vector<TypeReference> typeReferences;` on `ModuleRecord`;
  - `bool recordTypeReferences = false;` on `CompilePlan`.

  In `Frontend.h`, add a parse-time sink hook: a `TypeReferenceSink` pointer, null by default, plus an RAII installer. In `ProjectCompile.cpp`, install the sink around each module's `Module::Parse` **only** when `plan.recordTypeReferences` is set, targeting that input's `ModuleRecord`. Uninstall it on every exit path, including a `CompileError`.
- [ ] T008 In `QType.cpp`, after a successful resolution, push a record when a sink is installed:
  - qualified (`nsScope->findType( memberName )` succeeded): look up `nsScope->findSymbol( memberName )`, with the member-name token's location;
  - bare (`s->findType( symName )` succeeded): look up `s->findSymbol( symName )`, with the name token's location.

  Push `{ location, name length, symbol }` only if the symbol downcasts to `StructDefinition`, `EnumDefinition` or `ProtocolDefinition`. The recording lookup MUST have **no side effects** (F5): call only `findSymbol`. It must not call `markTypeUsed`/`markModuleUsed`, mutate any scope or touch import or usage state, so the unused-import and collision diagnostics cannot move. With no sink installed, the code path must do nothing beyond the null check: no lookups and no allocation. Generic type arguments recurse through `Type::Parse`, so each argument is recorded separately.

### Byte-for-byte equivalence vs the 043 qcc (both modes)

- [ ] T009 Build `build/` and `build-nollvm/` from this branch. Run `specs/044-lsp-xmodule-navigation/qcc_equivalence.sh --ref /tmp/blang-043/build --new build`, and `... --ref /tmp/blang-043/build-nollvm --new build-nollvm --parse-only`. Both must print `EQUIV: N/N identical` with a non-zero per-category count for (a), (c) and (d); a zero count fails (F4). Record the counts. Run `--selfcheck` once and confirm it goes red. Paste both summaries into `specs/044-lsp-xmodule-navigation/equivalence.md`.
- [ ] T010 Run the regression gates for changes A and B: `./run_tests.sh`, `BUILD_DIR=build-nollvm ./run_tests.sh`, `./test_codegen.sh`, `./test_codegen.sh --leak-check` (`Leaks: 0`), `test_build/run_build_tests.sh`, `ctest --test-dir build` and `ctest --test-dir build-asan`. Every count must equal the `043` baseline, with zero golden edits. Append the results to `specs/044-lsp-xmodule-navigation/equivalence.md`. **Stop and raise a BLOCKER if any byte or count differs.**

### blangd navigation skeleton

- [ ] T011 In `lsp/ProjectContext.h`/`lsp/ProjectContext.cpp`, expose the interface-path → dependency-run pairs. `ProjectContextResult` gains `std::vector<std::pair<std::string, std::shared_ptr<Run>>> interfaceRuns`, holding each closure node's `bmodPath` (exactly the `CompileInput::path` given to its interface input) and its kept source `Run`. A dependency whose source compile failed contributes no pair.
- [ ] T012 In `lsp/Compile.h`/`lsp/Compile.cpp`:
  - add `InterfaceSourceMap interfaces;` to `CompileResult`, declared **after** `deps`, and fill it from `interfaceRuns`;
  - set `plan.recordTypeReferences = true` on the **document's** compile plan, for both the single-file/stdlib path and the project path. Dependency-derivation runs leave it `false`.
  - keep the result's ownership order (resolver first/destroyed last, then context, then deps, then interfaces) and document it in the struct comment.
- [ ] T013 Create `lsp/Navigation.h`/`lsp/Navigation.cpp`, in namespace `lsp`, with:
  - `struct NavigationTarget { const QLang::Symbol *decl; const QLang::ModuleRecord *definingRecord; bool isForeign; }`;
  - `const QLang::ModuleRecord *viewingRecord( const CompileResult &, const std::string &docPath )`, the record whose `path == docPath`;
  - `const QLang::ModuleRecord *recordForFile( const CompileResult &, const std::string &file )`.

  F1: `definingRecord` is always the record **in the document's own compile**. For a dependency declaration this is the **interface** record, whose `moduleName` comes from the `<name>.bmod` path. The dependency's source record (reached through `InterfaceSourceMap`) supplies **only** the location and the signature text, and never the qualifier.

  Add `lsp/Navigation.cpp` to the `blangd`, `parserLeakTest` and any other target in `CMakeLists.txt` that links `lsp/Compile.cpp`.
- [ ] T014 In `lsp/Navigation.cpp`, implement `InterfaceSourceMap::toSource( const QLang::Symbol *interfaceDecl )` per research R2:
  1. find the `Run` by `interfaceDecl`'s `getLocation().file`;
  2. iterate the `Run`'s recorded modules whose role is not `kInterface`;
  3. match by kind (`FunctionDefinition` free function, `StructDefinition`, `EnumDefinition`, `ProtocolDefinition`) and name. A method or `init` matches by owning struct name, then by member name; `init` maps to the struct's init method.
  4. return the declaration only if **exactly one** match exists across those modules, else `nullptr`.

  Fallback (R2 risk): when the owning run yields no match, search every interface run in map order, still requiring a unique match. A declaration that is not from an interface is returned unchanged.
- [ ] T015 In `lsp/Server.cpp`, route `definition` and `hover` through `Navigation`, following the five-step resolution order in `contracts/navigation.md`. When a target's defining record **is** the viewing record (same module), the response is produced by the **unchanged** pre-U4 code: `AstLocator::definitionLocation` + `pathToUri`, and `AstLocator::hoverText`. Add a guard that converts any location whose file ends in `.bmod`, or is empty, to `null` (G1).

**Checkpoint**: T010 is green, T015 builds, and `./test_lsp.sh` passes with **zero** golden diffs. Same-file behavior is unchanged at this point.

---

## Phase 3: User Story 1 — Jump to a dependency's definition (P1) 🎯 MVP

**Goal**: A qualified call, an aliased call and a name-capability type each resolve to the dependency's `.b` source, never to a `.bmod`.

**Independent Test**: Run `./test_lsp.sh` over the `xmod_defn_qualified`, `xmod_defn_alias`, `xmod_defn_namecap` and `xmod_defn_not_open` goldens. Each URI ends in `.b` and points at the declaration. For example, `test_build/mathlib/math.b` 0-based 0:4 is `pub fn add`.

- [ ] T016 [US1] In `lsp/Navigation.cpp`, apply `InterfaceSourceMap::toSource` to every target from the existing arms (call, construct, method, field, function ref) whose file is an interface path. An unmapped target answers `null`.
- [ ] T017 [US1] In `lsp/Navigation.cpp`, add the type-reference arm (contract step 1). It finds the viewing module's `ModuleRecord::typeReferences` entry whose line equals the cursor line and whose `[col, col+length)` span contains the cursor column. It applies only when the target, after source mapping, lives in a file other than the document's path. A same-file target falls through to `locate`, which preserves `null`.
- [ ] T018 [P] [US1] Write `test_files/lsp/xmod_defn_qualified.lsp.jsonl`: open `test_build/myapp/main.b` with `stdlibPath=${ROOT}/stdlib`, then request a definition on `add` in `mathlib.add(3, 4)` (0-based 5:23). Expect `file://${ROOT}/test_build/mathlib/math.b` at `pub fn add`.
- [ ] T019 [P] [US1] Write `test_files/lsp/xmod_defn_alias.lsp.jsonl`: open `test_build/aliasapp/main.b`, then request a definition on `add` in `m.add(3, 4)` (9:26). Expect the same `math.b` location as T018.
- [ ] T020 [P] [US1] Write `test_files/lsp/xmod_defn_namecap.lsp.jsonl`: open `test_build/counterapp/main.b` and send two requests. The first is on the **type position** `Counter` in `Counter c` (6:1). The second is on the **construction** `Counter(` (6:14). Both expect `file://${ROOT}/test_build/counterlib/counter.b` at `pub struct Counter`.
- [ ] T021 [P] [US1] Write `test_files/lsp/xmod_defn_not_open.lsp.jsonl` (FR-004). Open **only** `test_build/myapp/main.b`, never `mathlib/math.b`, and request a definition on `mathlib.multiply` (8:22). Expect the on-disk `math.b` URI. The script's header comment states that the target file was never opened.
- [ ] T022 [P] [US1] Write `test_files/lsp/xmod_defn_unsaved.lsp.jsonl` (FR-005). Open `test_files/lsp/projects/unsaved_dep/` app and lib (the U3 fixture). Send a `didChange` that inserts two blank lines above the lib's exported function, then re-send the app's text so it recompiles. Request a definition on the call. Expect the lib URI with the line shifted by +2.
- [ ] T023 [P] [US1] Write `test_files/lsp/xmod_defn_failed_dep.lsp.jsonl` (FR-010). Open `test_files/lsp/projects/dep_failed/` (the U3 fixture whose dependency fails to compile) and request a definition and a hover on the call into the failed dependency. Both expect `null`, and no stray output appears (strict framing).
- [ ] T024 [US1] Run `./test_lsp.sh`, check each T018–T023 transcript by hand against `contracts/navigation.md`, then run `./test_lsp.sh --update-goldens` to write **only** the new `xmod_*.expected.out` files. `git status test_files/lsp` must list no modified pre-existing golden.

**Checkpoint**: US1 works on its own. Run `grep -l '\.bmod"' test_files/lsp/xmod_*.expected.out` and confirm it prints nothing.

---

## Phase 4: User Story 2 — Jump through a value held by use-capability only (P1)

**Goal**: `b.get()` on a `Box<int>` that `usebox` never names resolves to `boxq`'s source. `midx.get_box` resolves to `midx`'s source.

**Independent Test**: The `xmod_defn_usecap` golden pins `file://${ROOT}/test_build/boxq/boxq.b` 0-based 4:5 (`pub fn get`) and `test_build/midx/midx.b` (`pub fn get_box`).

- [ ] T025 [US2] In `lsp/Navigation.cpp`, add the use-capability member fallback (contract step 3), applied when the located `MethodCallExpression` has `mResolvedMethod == nullptr`:
  1. derive the receiver type with the **shared** Sema receiver helper from T006;
  2. collect `CompileContext::useCapabilityTypes` from the document's context, and take the **unique** struct whose name equals the receiver type's base name. Zero or more than one candidate answers `null`;
  3. call `StructDefinition::findMethod( mc->mMethodName )`;
  4. source-map the result via T016.

  Add no other lookup.
- [ ] T026 [P] [US2] Write `test_files/lsp/xmod_defn_usecap.lsp.jsonl`: open `test_build/usebox/main.b` and send two requests, on `get` in `b.get()` (14:18) and on `get_box` in `midx.get_box(7)` (13:15). Expect `boxq/boxq.b` at `pub fn get` and `midx/midx.b` at `pub fn get_box`. Verify the R2 attribution of the transitive generic body: if the definition first comes back attributed to `midx.bmod`, the T014 fallback must still yield `boxq.b`. Record the observed attribution in `research.md` R2.
- [ ] T027 [US2] Add a negative check to `xmod_defn_usecap.lsp.jsonl`, or to a sibling fixture `test_files/lsp/xmod_defn_usecap_ambiguous.lsp.jsonl`. Open `test_build/boxapp/main.b`, which imports both `boxa` and `boxb`, each exporting a `Box`. Request a definition on a receiver whose type is ambiguous by name. Expect `null`, never a guess. If boxapp has no such call site, document that and rely on the T014/T025 uniqueness checks in `parserLeakTest` (T046) instead.
- [ ] T028 [US2] Generate the goldens for T026/T027 after checking them by hand (as in T024). No pre-existing golden may be modified.

---

## Phase 5: User Story 3 — Jump into the stdlib, the prelude and sibling files (P1)

**Goal**: Stdlib functions and types, prelude types and sibling-file functions resolve to their real `.b` files.

**Independent Test**: The `xmod_defn_stdlib_fn`, `xmod_defn_stdlib_type`, `xmod_defn_prelude` and `xmod_defn_sibling` goldens. Their URIs are under `${ROOT}/stdlib/` or `${ROOT}/test_files/lsp/projects/multifile/`, and never contain a build directory.

- [ ] T029 [P] [US3] Create `test_files/lsp/src/xmod_stdlib.b`, a small clean program that has `import net;`, calls `net.http_ok("hi")`, and declares `net.HttpServer server = net.http_server("0.0.0.0", 8080);` without running it. Confirm that its reference compile (`bcc -v -S` `qcc` line) exits 0.
- [ ] T030 [P] [US3] Write `test_files/lsp/xmod_defn_stdlib_fn.lsp.jsonl`: initialize with `stdlibPath=${ROOT}/stdlib`, open `test_files/lsp/src/xmod_stdlib.b`, and request a definition on `http_ok`. Expect `file://${ROOT}/stdlib/net.b` at `pub fn http_ok`.
- [ ] T031 [P] [US3] Write `test_files/lsp/xmod_defn_stdlib_type.lsp.jsonl`: same document, with a definition request on `HttpServer` in the type position `net.HttpServer server`. This uses the T017 type-reference arm. Expect `stdlib/net.b` at `pub struct HttpServer`. Add a second request on the qualifier token `net`, and assert its result equals the pre-U4 answer (`null`), so no new behavior appears on qualifiers.
- [ ] T032 [P] [US3] Write `test_files/lsp/xmod_defn_prelude.lsp.jsonl`: open `test_files/codegen_prelude_no_import.b` and send two requests, on the type position `Map` in `Map<string, int> m` (6:4) and on the construction `Map<string, int>()` (6:27). Both expect `stdlib/collections.b` at `pub struct Map`.
- [ ] T033 [P] [US3] Write `test_files/lsp/xmod_defn_sibling.lsp.jsonl`: open `test_files/lsp/projects/multifile/main.b` and request a definition on `triple` (5:20). Expect `projects/multifile/helpers.b` at `pub fn triple`.
- [ ] T034 [US3] Generate the goldens for T030–T033 after checking them by hand. No pre-existing golden may be modified.

---

## Phase 6: User Story 4 — Hover shows the foreign signature with a stable display name (P1)

**Goal**: Every case from US1–US3 hovers with the FR-008 rendering. Same-module hovers are untouched.

**Independent Test**: In the `xmod_hover_*` goldens, the strings equal the hover table in `contracts/navigation.md` exactly, which satisfies workplan U4 (d).

- [ ] T035 [US4] In `lsp/Navigation.cpp`, implement `displayName( const QLang::Symbol *def, const QLang::ModuleRecord &viewing, const CompileResult & )` per FR-008 and research R5:
  - a prelude-provider record (`CompileContext::isPreludeProvider`), a core type with no record, or the declaration's own generic parameter renders bare;
  - otherwise collect the `ImportStatement::getLocalQualifier()` values over `viewing.module->getImports()` whose `getModuleName() == definingRecord.moduleName`;
  - if that set is non-empty, use the lexicographically smallest qualifier;
  - otherwise (**unbound**, use-capability only; F2, normative per design.md "D4 addendum — settled for U4" and spec FR-008 rule 3), use the defining module's **human name**: the `getDefiningFile()` value of the definition the document's compile resolved (`StructDefinition`/`EnumDefinition::getDefiningFile()`). This is exactly the name a `.bmod` `// foreign-type:` header prints (BmodEmitter.cpp:665). It is **not** the interface record's `moduleName`. If it is empty, the module name is rendered as `?`, as the header does;
  - F1: for a **bound** module, the match key is the defining record in the document's own compile (the interface record's `moduleName`, from the `<name>.bmod` path). The dependency's source record supplies only the location and the signature text;
  - a member renders as `<owner display>.<member>`.
- [ ] T036 [US4] In `lsp/Navigation.cpp`, implement nested-type rendering (FR-008 rule 6, R5). For each `Type` name in a foreign signature:
  1. resolve it in the **declaring record's** `scope->findSymbol()`, keeping a struct, enum or protocol;
  2. otherwise, take the unique same-named entry of `useCapabilityTypes`;
  3. otherwise, use the name as written.

  Render the result via `displayName`. Type arguments recurse (`boxq.Box<int>`), and `self` stays bare.
- [ ] T037 [US4] In `lsp/Navigation.cpp`, implement the foreign hover renderer (research R6):

  | Target | Rendering |
  |--------|-----------|
  | free function | `fn <disp>.<name>(<params>) [-> <ret>]` |
  | method / `init` | `fn <owner disp>.<name>(...)` |
  | struct | `struct <disp>[<G…>]` (generic parameters included) |
  | enum | `enum <disp>[<G…>]` |
  | protocol | `protocol <disp>` |

  The renderer reads the source-mapped declaration and falls back to the interface declaration; with neither, the hover is `null`. It is used only when `isForeign` is true. Same-module targets keep `AstLocator::hoverText`. The markdown wrapper is the existing ` ```blang\n…\n``` `.
- [ ] T038 [P] [US4] Write `test_files/lsp/xmod_hover_qualified.lsp.jsonl` (myapp, `mathlib.add`). Expect `fn mathlib.add(int a, int b) -> int`.
- [ ] T039 [P] [US4] Write `test_files/lsp/xmod_hover_alias.lsp.jsonl` (aliasapp, `m.add`). Expect `fn m.add(int a, int b) -> int`.
- [ ] T040 [P] [US4] Write `test_files/lsp/xmod_hover_namecap.lsp.jsonl` (counterapp, on the type position and on the construction). Both expect `struct counterlib.Counter`.
- [ ] T041 [P] [US4] Write `test_files/lsp/xmod_hover_usecap.lsp.jsonl` (usebox) with two requests. `b.get()` expects `fn boxq.Box.get(self) -> T`. `midx.get_box` expects `fn midx.get_box(int n) -> boxq.Box<int>`.
- [ ] T041a [P] [US4] (F2) Add the fixture project `test_files/lsp/projects/usecap_names/` (each part read-only on disk, no build needed):
  - `app/` (bin): `import mid;`, `var s = mid.make(3);`, and a method call `s.area()`;
  - `mid/` (lib): `import shapes;` and `pub fn make(int n) -> Sq { ... }`;
  - a library whose **directory and `blang.toml` name are `shapes`** but whose only **source file is `geometry.b`**, defining `pub struct Sq` with `pub init` and `pub fn area(self) -> int`.

  The app never imports `shapes`, so `Sq` is use-capability only. Write `test_files/lsp/xmod_hover_usecap_names.lsp.jsonl` to hover `s.area()` and `mid.make`. The expected owner qualifier is the struct's `getDefiningFile()` human name: the `<module>` token that `mid`'s `// foreign-type: Sq <module> <digest>` header prints under `bcc build`. Capture it once with `bcc build -v` into a scratch copy and record it in the fixture's header comment. Expect `fn <module>.Sq.area(self) -> int` and `fn mid.make(int n) -> <module>.Sq`. If the header's `<module>` equals the directory name and not the file name, the golden still pins the **header-derived** value, and `research.md` R5 records which of the two names the header carries. If `bcc build` of the fixture is needed for that capture, run it in a scratch copy, never in the committed tree.
- [ ] T042 [P] [US4] Write `test_files/lsp/xmod_hover_stdlib_fn.lsp.jsonl`, `xmod_hover_stdlib_type.lsp.jsonl`, `xmod_hover_prelude.lsp.jsonl` and `xmod_hover_sibling.lsp.jsonl`, on the same documents and positions as T030–T033. The expected hovers are:

  | Fixture | Expected hover |
  |---------|----------------|
  | stdlib function | `fn net.http_ok(string body) -> net.HttpResponse` |
  | stdlib type | `struct net.HttpServer` |
  | prelude type | `struct Map<K, V>` |
  | sibling function | `fn helpers.triple(int x) -> int` |

  The `stdlibPath` is `${ROOT}/stdlib` throughout.
- [ ] T043 [US4] Generate the goldens for T038–T042 after a character-by-character check against `contracts/navigation.md`. Record in `spec.md` FR-008 a one-line note that the rendered strings match, which closes workplan U4 (d). No pre-existing golden may be modified.

---

## Phase 7: User Story 5 — Same-file navigation is unchanged (P1)

**Goal**: Pre-U4 same-file results are byte-identical.

**Independent Test**: Every pre-existing `defn_*`, `hover_*`, `symbols_*`, `lifecycle` and other golden passes with no golden file modified.

- [ ] T044 [US5] Run `./test_lsp.sh` and `./test_lsp.sh --selfcheck` (`SELFCHECK: OK`). Run `git diff --stat 043-lsp-project-context -- test_files/lsp/ | grep -v 'xmod_\|src/xmod_stdlib.b'` and confirm it prints nothing. Also run `git diff 043-lsp-project-context -- lsp/AstLocator.h lsp/AstLocator.cpp` and confirm it is empty.
- [ ] T045 [P] [US5] Add a same-file type-position regression request to `test_files/lsp/xmod_defn_namecap.lsp.jsonl`, or write a new `test_files/lsp/xmod_samefile_typepos.lsp.jsonl`. Open the existing `test_files/lsp/src/defs.b` and request a definition and a hover on the type position `Point` in `Point p = Point(1, 2);` (0-based 25:1). Both must equal the pre-U4 answer recorded in T003's `probe-baseline.txt`, which is expected to be `null`. Add these two requests to the T003 probe so the baseline actually contains them. This pins the accepted decision that same-file type positions keep their pre-U4 result.

---

## Phase 8: Polish & cross-cutting concerns

- [ ] T046 Extend `ParserLeakTest.cpp` (the `parser_no_leak` ctest). Add a loop that runs 50 times: `lsp::compileDocument` on `test_build/usebox/main.b` through the project path, then the navigation calls for `b.get()` (definition and hover) and for a type reference, asserting the boxq source location each time. Also add one assertion that an ambiguous or unmapped target returns null. Run `ctest --test-dir build-asan -R parser_no_leak`; it must be LSan-clean.
- [ ] T047 [P] Run `ctest --test-dir build -R blangd_no_subprocess` (and its `_teeth` leg) to confirm `blangd` still imports no `popen`/`fork`/`exec*`/`socket`/`connect`. Also require `./test_lsp.sh` output to contain the `PASS git-shim: ... git never invoked` line (L1).
- [ ] T048 [P] Run `./test_lsp.sh` on a sanitizer build (`BUILD_DIR=build-asan ./test_lsp.sh`) and confirm the xmod goldens pass under ASan with no report.
- [ ] T049 Re-run `specs/044-lsp-xmodule-navigation/qcc_equivalence.sh` in both modes after all edits (`EQUIV: N/N identical`), and append the final result to `equivalence.md`.
- [ ] T050 Run the full workplan gate list:
  - `./run_tests.sh` and `BUILD_DIR=build-nollvm ./run_tests.sh`;
  - `./test_codegen.sh` and `--leak-check`;
  - `./test_lsp.sh`, including the corpus and latency legs (median ≤ 500 ms);
  - `ctest --test-dir build` and `ctest --test-dir build-asan`;
  - `test_build/run_build_tests.sh`;
  - `tools/check_no_field_reachins.sh examples test_build`;
  - the `.bmod`-URI scan over **every** `xmod_*` golden (F3): `grep -l '\.bmod"' test_files/lsp/xmod_*.expected.out` must print nothing;
  - both diagnostic-corpus legs, as the proof that type-reference recording changes no diagnostic (F5): `tools/lsp_diag_corpus.py --corpus stdlib` must report 179/179, and `--corpus project` must equal `bcc build`. The project leg needs `llc`, so run it on the LLVM `build/`;
  - the `PASS git-shim: ... git never invoked` line must be present in the `test_lsp.sh` output (L1).

  Record the counts. The test totals may only grow.
- [ ] T051 [P] Update `CLAUDE.md`:
  - the `lsp/` repository-structure entry gains `Navigation.{h,cpp}`, with cross-module definition and hover and the D4 display rule;
  - the `ProjectCompile.h` entry gains `useCapabilityTypes` and type-reference recording;
  - add a Known-Issues/status bullet for modules-v2-lsp U4 that cites `specs/044-lsp-xmodule-navigation/`.
- [ ] T052 Remove the `/tmp/blang-043` worktree (`git worktree remove /tmp/blang-043`). Commit the code, the goldens, `equivalence.md` and `probe-baseline.txt` on `044-lsp-xmodule-navigation`, staging by explicit path.

---

## Dependencies & execution order

- **Phase 1 (T001–T003)**: T001 comes before T002/T009. T003 is independent.
- **Phase 2**:
  - T004 → T005;
  - T006 and T007 → T008 are independent of T004/T005;
  - T009/T010 require T004–T008. **This is the gate for all later phases.**
  - T011 → T012 → T013 → T014 → T015.
- **US1 (Phase 3)**: requires Phase 2. T016/T017 come before the golden generation in T024. T018–T023 can be written in parallel.
- **US2 (Phase 4)**: requires T016 (source mapping) and T006 (shared helpers).
- **US3 (Phase 5)**: requires T016/T017. T029 comes before T030/T031.
- **US4 (Phase 6)**: requires US1–US3 targets. T035 → T036 → T037 → goldens.
- **US5 (Phase 7)**: can run after any phase as a regression check. It is final after Phase 6.
- **Polish (Phase 8)**: runs after all stories. T049/T050 come last before T052.

## Parallel examples

- **Phase 2**: T006 (Type.h/Sema) can run alongside T007/T008 (ProjectCompile.h/QType.cpp) once T004/T005 land.
- **US1**: the scripts T018, T019, T020, T021, T022 and T023 are separate files.
- **US3**: T030, T031, T032 and T033 are separate files.
- **US4**: the scripts T038 through T042 are separate files.

## Implementation strategy

1. **MVP = Phase 1 + Phase 2 + US1.** That covers the dependency definition, never a `.bmod`, plus the not-open, unsaved-buffer and failed-dependency cases.
2. Add US2 (use-capability), then US3 (stdlib, prelude, sibling), then US4 (hover), each landing its own goldens.
3. Run US5 and Phase 8 as the closing gates. The converge gate for `specs/044-lsp-xmodule-navigation` follows `/speckit-implement`.
