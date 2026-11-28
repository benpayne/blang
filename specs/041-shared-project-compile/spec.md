# Feature Specification: Shared Project-Compile Entry Point (behavior-neutral extraction)

**Feature Branch**: `041-shared-project-compile`

**Created**: 2026-11-26

**Status**: Draft

**Input**: User description: "U1 specs/041-shared-project-compile. Behavior-neutral extraction of the shared project-compile entry point. Move `qcc.cpp` `main()`'s combine-mode routing (user/prelude/namespaced-stdlib scope selection, `.bmod` injection, origin and digest stamping, the whole-program import-diagnostics pass, Sema) and `bcc`'s `resolveStdlibFiles` stdlib-set selection into shared frontend code (`BLANG_FRONTEND_SOURCES`, no LLVM). Call it from BOTH `qcc.cpp` and `lsp/Compile.cpp`, and add a new ctest `project_compile`. Zero golden edits, no diagnostic-text or exit-code changes. Any forced golden or diagnostic change means stop and raise a question."

**Epic**: `docs/epics/modules-v2-lsp/` (Epic C of modules-v2). This is unit U1. It covers REQ-001 and epic done-condition 1.

**Binding design record**: `docs/epics/modules-v2/overview.md` (D1–D17). This unit does not reopen it.

**Launch precondition (verified 2026-11-26)**: the epic branch `epic/modules-v2-lsp` (HEAD `2f68efe`) contains `830557c` (the merge of PR #157) and `efb099a` (the KG-7 top-down scope-ownership fix). `git merge-base --is-ancestor efb099a HEAD` succeeds.

## Context

Today, the rules that decide how a set of BLang inputs is compiled together live in two drivers:

- **`qcc`'s command-line `main()`.** It decides, inline, which scope each input parses into:
  - the last source file is the user file and parses into the user scope;
  - `buffer`/`collections` LOAD into a prefix-exempt holding namespace, and their prelude types are then PROMOTEd into the prelude scope;
  - every other stdlib file becomes a namespaced module.

  `main()` also:
  - pre-scans `.bmod` inputs for foreign-type headers;
  - checks the `.bmod` format version;
  - injects each `.bmod`'s public interface into a per-module namespace;
  - stamps each module's defining origin and identity digest;
  - runs Sema on each module;
  - runs the whole-program import-diagnostics pass (unknown-module, unused-import).
- **`bcc`.** Its `resolveStdlibFiles` picks which stdlib files make up a compile. It also has a text scan of a program's `import` lines (`parseImports`).

The language server (`blangd`) shares none of this. It compiles each document alone, so it cannot agree with the compiler about any program that uses the stdlib, the prelude or a dependency. The epic's goal is that the editor and the compiler run *the same code*. That code has to exist as a shared component before anything can consume it.

This unit creates that component and makes the existing callers use it. **Nothing observable changes.** `qcc`, `bcc` and `blangd` behave byte-for-byte as before. The value of the unit is structural: every later unit (U2 stdlib context, U3 project context, U4 navigation) builds on this seam, and nothing can drift from the compiler because nothing is copied.

## User Scenarios & Testing *(mandatory)*

The "users" of this unit are BLang developers (whose `qcc`/`bcc`/`blangd` must not change), and the maintainers and later epic units that build on the shared entry point.

### User Story 1 - The compiler behaves exactly as before (Priority: P1)

A BLang developer compiles programs with `bcc`, `bcc build`, `bcc test`, `bcc migrate` and direct `qcc` invocations. This covers single-file, `--combine`, multi-file, `.bmod` dependency, `--parse-only`, `--dump-locations`, `--emit-bmod`, `--emit-schema`, `--json`, `-Werror` and `-v` runs. They see exactly the same results as before this unit: the same exit codes, the same diagnostics (text, order, location, severity), the same emitted IR / `.bmod` / schema, and the same stdout/stderr.

**Why this priority**: This is the unit's hard constraint (overview "U1 is a behavior-neutral extraction"; design.md C2). A refactor of the driver's core that changes anything observable fails the unit, however clean it is.

**Independent Test**: Run every pre-existing gate with zero edits to any golden or expected file:
- `./run_tests.sh` in both build modes;
- `./test_codegen.sh`, plus `--leak-check` and `--selfcheck`;
- `test_build/run_build_tests.sh`;
- `ctest` in `build` and `build-asan`;
- `./test_lsp.sh`;
- the reach-in gate.

**Acceptance Scenarios**:

1. **Given** the pre-unit test suites and their committed goldens, **When** they run against the post-unit build, **Then** every suite passes and `git diff` of the branch shows no change under `test_files/` (goldens, `.expected` files, LSP transcripts) or `test_files/lsp/`.
2. **Given** any program that previously failed to compile, **When** it is compiled after this unit, **Then** it fails with the same exit code and the same rendered diagnostic text, in the same order, in both human and `--json` form.
3. **Given** a project built with `bcc build` (path deps, git deps, diamond graphs, aliasing, prelude shadowing), **When** it is built after this unit, **Then** `bcc` passes `qcc` the same stdlib files in the same order as before, and the build produces the same artifacts and run output.

---

### User Story 2 - One shared entry point, called by both the compiler and the editor (Priority: P1)

A maintainer (and the U2–U4 implementers) needs a single shared place that decides how an ordered set of compile inputs becomes resolved, analyzed modules with diagnostics. Both the compiler driver and the language server must call it, so no rule exists in two copies.

**Why this priority**: This is REQ-001 and the reason for the unit. Every later unit consumes it. If the language server reaches the rules through anything other than this entry point, the epic's central contract (C1, "one compile, two front ends") can never hold.

**Independent Test**:
- A search of the sources shows the shared entry point called from both the `qcc` driver and the language server's compile path.
- The per-file routing branch and the import-diagnostics pass no longer exist in the `qcc` driver.
- `bcc` no longer contains its own stdlib-selection body.

**Acceptance Scenarios**:

1. **Given** the post-unit sources, **When** a reviewer searches for the shared entry point, **Then** it is invoked from `qcc.cpp` and from `lsp/Compile.cpp`.
2. **Given** the post-unit `qcc.cpp`, **When** a reviewer searches it, **Then**:
   - it no longer contains the per-file combine-routing selection (the `isUserFile` / `isPreludeModule` branch);
   - it contains no copy of the whole-program import-diagnostics pass;
   - it contains no `.bmod` injection, foreign-type pre-scan, format-version check, origin/digest stamping or Sema invocation of its own.
3. **Given** the post-unit `bcc.cpp`, **When** a reviewer searches it, **Then** it contains no local `resolveStdlibFiles` body. Every stdlib-set selection (single-file compile, `bcc build`, `bcc test`, `bcc migrate`) goes through the shared selection function.
4. **Given** the language server, **When** it compiles an open document after this unit, **Then** it does so through the shared entry point in single-file mode, and publishes the same diagnostics, definitions, hovers and symbols as before (all `test_files/lsp/` transcripts unchanged).

---

### User Story 3 - The entry point is testable without either driver (Priority: P2)

A maintainer can exercise the shared entry point directly in a unit test, with no `qcc` or `blangd` process in the loop. A regression in routing, prelude promotion, namespace registration or shadowing is then caught at the component level.

**Why this priority**: It locks the seam's contract independently of the drivers that consume it, so U2–U4 can change how blangd builds its inputs without silently changing the routing. It is P2 only because Story 1's gates already cover the behavior end-to-end.

**Independent Test**: A new `ctest` named `project_compile` passes in `build` and `build-asan`.

**Acceptance Scenarios**:

1. **Given** a combined stdlib + user fixture with at least one error and at least one warning, **When** `project_compile` drives the shared entry point directly, **Then** the rendered diagnostic text equals what `qcc --combine` renders for the same inputs in the same order.
2. **Given** a combined stdlib + user fixture that uses `Map` and `Set` with zero imports, **When** the entry point compiles it, **Then** `Map` and `Set` resolve through the prelude scope (not the user scope, and not a namespace).
3. **Given** the same combined compile with `stdlib/net.b` among the inputs, **When** the entry point compiles it, **Then** `net` is registered as a namespace scope reachable from the user scope.
4. **Given** a combined fixture whose user file defines its own `struct Map`, **When** the entry point compiles it, **Then** the user's `Map` shadows the prelude `Map`:
   - name lookup from the user scope yields the user's definition;
   - the prelude-shadow information the entry point reports marks `Map` as shadowed, so codegen keeps the user's struct and drops the prelude's.
5. **Given** the test runs under `build-asan`, **When** it completes, **Then** LeakSanitizer reports no leaks (the compile context releases everything it owns before the resolver is destroyed).

---

### Edge Cases

These are existing behaviors the extraction must preserve exactly. They are not new behavior.

- **Parse failure mid-list**: one input fails catastrophically. The entry point records the failure, still compiles and reports the remaining inputs, and the compile is marked failed (accumulate, don't abort).
- **Malformed `// foreign-type:` header** in a `.bmod`: one located error per malformed line (file, line, column 1, same text). That `.bmod` is skipped and the compile is failed. Never a crash (Quality Gate 7).
- **`.bmod` format mismatch**: one located error at line 1 naming the found and expected versions, with the same text. The file is skipped.
- **`realpath` failure** for an input with no driver-supplied origin: one located hard error (same text). The module is never given an empty identity.
- **Input ordering**: `.bmod` inputs are always processed before `.b` inputs, each group in command-line order. In combine mode the user file is the **last** `.b` input.
- **Non-combine multi-file**: each source parses into its own module scope parented to the global scope, with no prelude scope and no namespaces. This is unchanged.
- **Import-diagnostics gating**: the pass runs only when the module set is authoritative (combine mode, or at least one `.bmod` input). A bare single-file parse (e.g. `import std;` with no deps) stays lenient, with no unknown-module error and no unused-import warning.
- **Prelude PROMOTE timing**: prelude types are promoted immediately after their provider module parses and passes through Sema, before later inputs parse. A user file parsed last therefore resolves `Map`/`Set`/`Buffer` with zero imports.
- **Prelude shadowing**: a user module defining a prelude-manifest name suppresses the prelude definition. The flat struct list handed to codegen contains the user's struct, not the prelude's.
- **Module prefixes**: namespaced stdlib modules get their module-name prefix in codegen, prelude providers are prefix-exempt, and user code has none. The entry point exposes enough per-module information for the driver to compute the same prefix it computes today.
- **`.bmod` emission scope**: `--emit-bmod` resolves foreign types through the scope of the last compiled non-`.bmod` module, as today.
- **Trace output under `-v`**: the parse-progress and per-token trace lines (stderr) appear exactly as before. They stay suppressed under `--dump-locations`.
- **Language server, unsaved or nonexistent document path**: blangd compiles in-memory text whose path may not exist on disk. In single-file mode the entry point must not introduce a `realpath` failure, a digest-stamping difference, or any import diagnostic that blangd does not produce today.
- **Usage error paths** (missing argument values, unknown options, no input file): exit codes and messages are unchanged. These stay in the `qcc` driver.

## Requirements *(mandatory)*

### Functional Requirements

**Behavior neutrality (the overriding constraint)**

- **FR-001**: The unit MUST be behavior-neutral. Before and after, `qcc`, `bcc` and `blangd` MUST produce identical:
  - exit codes;
  - diagnostic text, location, severity, order and count, in both human and `--json` renderings, with and without `-Werror`;
  - stdout and stderr content, including `-v` trace and `--dump-locations` output;
  - emitted IR, `.bmod` and schema files;
  - LSP transcripts.
- **FR-002**: The unit MUST NOT edit any golden or expected-output file: nothing under `test_files/` (including `test_files/golden/`, every `*.expected`, `*.expected.out`, and `// EXPECT-ERROR` annotation) and nothing under `test_files/lsp/`. It also MUST NOT change the `.bmod` format version, `BuildCache` keys, the quarantine list, or the D1–D17 decision record.
- **FR-003**: **Stop rule.** If the extraction cannot be completed without changing any golden, diagnostic text, exit code or other observable output, the implementer MUST stop. They raise the case as a question (a `BLOCKER:` line naming the file, the diff and why it seems forced) and MUST NOT accept or regenerate the golden. "Just regenerate it" is not a permitted resolution in this unit.

**The shared project-compile entry point**

- **FR-004**: A shared project-compile entry point MUST exist in the compiler frontend sources (the set linked by both `qcc` and `blangd`). It MUST have no dependency on LLVM, so the non-LLVM build and `blangd` link it.
- **FR-005**: The entry point MUST take an explicit, ordered **compile plan**. It MUST NOT read command-line arguments or global driver state. For each input the plan states:
  - its path, used in diagnostics;
  - its source: read from disk, or in-memory text supplied by the caller;
  - its kind: `.b` source or `.bmod` interface;
  - its canonical origin, when the driver supplies one.

  The plan also carries the compile-wide options: combine vs. non-combine mode, a whole-compile module origin, per-`.bmod` origins, and trace enablement. It MUST be able to express every input configuration `qcc` accepts today.
- **FR-006**: The entry point MUST own the per-input scope routing that `qcc` performs today, with identical results:
  - combine mode: a prelude scope plus a user scope as its child, with the last `.b` input routed to the user scope;
  - prelude providers (`buffer`, `collections`): LOADed into a prefix-exempt holding namespace and PROMOTEd (prelude-manifest types only) into the prelude scope, immediately after they parse;
  - every other `.b` input in combine mode becomes a namespaced module registered on the user scope;
  - non-combine mode: one module scope per input.
- **FR-007**: The entry point MUST own `.bmod` handling with identical results and diagnostics:
  - the foreign-type header pre-scan, including the malformed-header error;
  - the format-version check, including the mismatch error;
  - marking the module extern and its structs as from-interface;
  - incremental, in-order injection of the `.bmod`'s public functions, types, enums and protocols into its per-module, name-capability-granting namespace.
- **FR-008**: The entry point MUST own defining-origin stamping and module-identity digest stamping, with identical origin selection:
  - a `.bmod` uses its per-`.bmod` origin;
  - otherwise a `.b` uses the whole-compile module origin;
  - otherwise the input's real path is used, and a resolution failure is the same located hard error as today.
- **FR-009**: The entry point MUST run Sema on each non-interface module at the same point in the sequence as today: after that module parses and before the next input. Sema failure MUST mark the compile failed exactly as today.
- **FR-010**: The entry point MUST own the whole-program import-diagnostics pass (unknown-module error; unused-import warning keyed on the local qualifier). The gating MUST be identical (only when the module set is authoritative), and so MUST the message text and the slug.
- **FR-011**: The entry point MUST report diagnostics through a diagnostic engine supplied by the caller. Rendering (human vs. JSON, `-Werror`, `--debug-compiler`, when `finish()` runs) stays the caller's choice. The entry point itself MUST NOT render or print diagnostics.
- **FR-012**: The entry point MUST return a **compile context** that owns everything it built: modules, module scopes, namespace scopes, and the AST they reference. Following the KG-7 top-down ownership rule, the context MUST be releasable before the resolver that owns the global scope. The context MUST also expose what the `qcc` driver's later stages need:
  - the parsed modules in processing order, with each module's kind and module name;
  - each module's scope;
  - whether the compile had errors;
  - which modules are prelude providers, namespaced, or user-routed (so the driver computes today's codegen module prefix);
  - the set of prelude names a user module shadows (the allStructs shadow filter);
  - the scope `--emit-bmod` resolves foreign types through.
- **FR-013**: The entry point MUST work with the process's existing resolver (the global builtin scope owner). It MUST NOT construct a second global scope, and MUST leave the global scope and diagnostic aliases as the caller installed them once it returns.

**Callers**

- **FR-014**: The `qcc` driver MUST become a thin caller. It parses arguments, builds a compile plan, calls the entry point, and then runs its own output stages (`--dump-locations`, `--emit-bmod`, `--emit-schema`, LLVM codegen/verify/optimize/print, diagnostic rendering and exit codes) on the returned context. It MUST NOT retain the per-file routing branch, the `.bmod` pre-scan/format-check/injection, origin/digest stamping, the Sema invocation, or the import-diagnostics pass.
- **FR-015**: The language server's compile path MUST call the same entry point, still in **single-file mode**, with the open document's in-memory text as its only input. Its observable behavior MUST be unchanged:
  - the same diagnostics;
  - no new `realpath`-based error and no new digest or import diagnostics;
  - the same retained module and scope for navigation;
  - every existing `test_files/lsp/` transcript unchanged.

  The server MUST NOT acquire a blangd-only copy of any routing, stamping or diagnostics rule.

**Shared stdlib-set selection**

- **FR-016**: The stdlib-set selection `bcc` performs today MUST move into a shared function. Given a stdlib directory and a set of imported module names, it returns the same files in the same order as today:
  1. the unconditional base `sys, buffer, collections, fs, net`;
  2. then the import-gated `timer, math, time, random, env, cli` in that order;
  3. then any other imported name that has a `<name>.b` in the stdlib directory;

  each included only if the file exists.

  This shared code MUST be linkable by `bcc` without pulling in the parser or LLVM, and by the frontend (so `blangd` can call it in U2).
- **FR-017**: Every `bcc` stdlib-selection call site (single-file compile, `bcc build`, `bcc test`, `bcc migrate`) MUST go through the shared selection function. `bcc.cpp` MUST keep no local body or duplicate of it.
- **FR-018**: The import-name scan the selection is fed from (`bcc`'s `parseImports`: the leading identifier of each line beginning `import `, after stripping a trailing `//` comment) MUST also move into shared code, operating on source text. `bcc` keeps reading the file, and U2's `blangd` can feed in-memory buffer text to the identical rule. Its results MUST be identical to today's for every input.

**Tests**

- **FR-019**: A new `ctest` named `project_compile` MUST drive the shared entry point directly, with no `qcc` or `blangd` process acting as the system under test. It MUST assert, for combined stdlib + user fixtures:
  - (a) the rendered diagnostic text equals what `qcc --combine` renders for the same inputs;
  - (b) `Map` and `Set` resolve through the prelude scope;
  - (c) `net` is registered as a namespace scope;
  - (d) a user `struct Map` shadows the prelude `Map`, both in scope lookup and in the reported shadow set.

  It MUST also assert that the shared stdlib selection returns the expected ordered list for a representative import set. It MUST pass in `build` and `build-asan` (LSan-clean).
- **FR-020**: The existing `parser_no_leak`, `resolver_component` and `resolver_reuse` ctests MUST remain green and unmodified in their assertions. `parser_no_leak` MUST stay LSan-clean under `build-asan` with the language server now routed through the entry point.

### Key Entities

- **Compile plan**: an ordered, explicit description of one compile. It lists the inputs, each with path, source (disk or in-memory text), kind (`.b` / `.bmod`) and optional canonical origin. It also carries the compile-wide options: combine mode, module origin, per-`.bmod` origins and trace flags. It is built by a driver and consumed by the entry point. It holds no parsing state.
- **Compile context** (the entry point's result): owns the parsed modules, their scopes (prelude scope, user scope, namespace scopes, per-module scopes) and therefore the AST. It exposes:
  - per-module role and name: user-routed, prelude provider, namespaced stdlib, `.bmod` interface, or non-combine module;
  - the error flag;
  - the prelude-shadow set;
  - the emit-bmod scope.

  It is released by its owner before the resolver.
- **Stdlib selection**: a pure function from (stdlib directory, imported names) to an ordered list of stdlib file paths. A companion function scans source text for imported module names.
- **Resolver** (existing, Epic B U4): owns the global builtin scope and hands out module scopes. The entry point uses the caller's resolver and does not replace it.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of pre-existing gates pass with **zero** golden or expected-file edits:
  - `./run_tests.sh` in both build modes, at the baseline pass counts in `docs/epics/modules-v2-lsp/evaluation.md`;
  - `./test_codegen.sh`, `--leak-check` (Leaks: 0) and `--selfcheck`;
  - `test_build/run_build_tests.sh`;
  - `ctest` in `build` and `build-asan`;
  - `./test_lsp.sh`;
  - the reach-in gate;
  - all 6 example scripts and `make -C demos run`.

  The baseline is re-measured on the actual base before implementation starts. Any pre-existing red is reported, not absorbed. `git diff --stat <base>..HEAD -- test_files/` is empty.
- **SC-002**: Exactly one implementation of each rule exists: scope routing, prelude LOAD/PROMOTE, `.bmod` pre-scan/format-check/injection, origin/digest stamping, the import-diagnostics pass, stdlib-set selection and the import scan. A source search finds each rule's body once, in shared code. The entry point is called from both the compiler driver and the language server.
- **SC-003**: The `qcc` driver contains none of the extracted logic: no `isUserFile` / `isPreludeModule` selection, no import-diagnostics loop, no `injectBmodSymbols` body. `bcc.cpp` contains no `resolveStdlibFiles` or `parseImports` body.
- **SC-004**: The new `project_compile` test passes in both the normal and the sanitizer build. It covers all four required assertions plus the stdlib-selection ordering, and LeakSanitizer reports zero leaks.
- **SC-005**: For each of the six "false diagnostics" files in the epic overview, the `qcc --parse-only` and `bcc -v -S` results (exit code and full stderr) are byte-identical before and after the unit. This is a spot check of neutrality on real programs, beyond the suites.

## Assumptions

- **Scope of "routing"** (per workplan U1 Work, design.md Context): the extraction covers everything in `qcc`'s parse loop and the import-diagnostics pass, i.e. scope routing, the `.bmod` pre-scan, format check, extern/from-interface marking and injection, origin/digest stamping, Sema, and eager PROMOTE. It does **not** cover argument parsing, `--dump-locations` output, `.bmod`/schema emission, or LLVM codegen. Those are output stages that stay in `qcc` and consume the returned context. The prelude-shadow-set computation is non-LLVM and is part of the context (FR-012), so the LLVM stage only filters by it.
- **Branch name** (per the Spec Kit ceremony "branch named exactly after its spec directory"): the branch is `041-shared-project-compile`, created from `epic/modules-v2-lsp` @ `2f68efe`. The manager's instruction said "specs/041-shared-project-compile". The ceremony's convention and example (`034-provisioning`) name the branch without the `specs/` prefix, so that convention is followed.
- **Import scan moves too** (FR-018; derived from overview "Constraints": "Any rule blangd applies must come from the shared entry point … never a blangd-only reimplementation"). The workplan names only `resolveStdlibFiles`. But its input, the textual import scan, is equally a rule that decides the stdlib set. Without moving it, U2 would need either a blangd copy or a different (AST-based) scan that could disagree with `bcc` (e.g. an `import` inside a `/* */` comment). Moving it is behavior-neutral and cheap.
- **blangd single-file mode in U1** (per workplan U1 "still in single-file mode for this unit so blangd's observable behavior does not change yet"): blangd's plan is one in-memory `.b` input, non-combine, with no `.bmod`s. Today blangd stamps a defining origin but **no** identity digest, and it never fails on `realpath`. The plan therefore needs a way to express "no canonical origin; do not stamp a digest, do not resolve a real path" for this input. That keeps blangd's diagnostics identical (digests affect only mangling, not diagnostics, but a `realpath` failure on an unsaved path *would* be a new diagnostic). With a non-authoritative module set, no import diagnostics run, exactly as today. Making blangd stamp digests is deferred to U2/U3, where its plans carry real origins.
- **Dependency namespaces still live on the global scope** (existing behavior, kept for neutrality): `.bmod` injection registers each dependency's namespace on the global builtin scope (the common ancestor of the combine user scope and a non-combine module scope). In a one-shot `qcc` process this is harmless. In long-lived blangd it would accumulate across reparses, which violates C3. U1 preserves it byte-for-byte because blangd sends no `.bmod`s in U1. **This is flagged for the U3 design audit**: when blangd starts feeding dependency interfaces, those namespaces must be owned by the compile context (e.g. a context-owned dependency scope between the global and prelude scopes), not by the global scope. That change is not behavior-neutral to make here and is out of scope.
- **Process model**: `qcc` still constructs exactly one resolver per process and one compile. The `project_compile` ctest may construct a fresh resolver per case, so global-scope state never leaks between cases.
- **`project_compile` reference text** (per workplan U1 done-condition (c)): "equals `qcc --combine` on the same inputs" may be satisfied by comparing against `qcc`'s rendered output for the same inputs: either captured by the build/test harness, or a committed expected text verified to match `qcc` at commit time. The plan picks the mechanism. The asserted property is equality with `qcc`, not a hand-written string.
- **Shared stdlib-selection linkage** (derived from `CMakeLists.txt`): `bcc` links only `bcc.cpp`, `ProjectConfig.cpp`, `BuildCache.cpp` and `sha256.c`, not the frontend. The stdlib-selection and import-scan code must therefore live in a translation unit with no parser/LLVM dependency. That unit is linked by `bcc` and included in the frontend sources. The exact file name is a plan decision.
- **Names are indicative** (per design.md Architecture): `CompilePlan`, `ProjectCompile::run`, `CompileContext` and `selectStdlibFiles` are working names. The plan settles the final API names, subject to house style.
- **Design audit** (workplan U1 Audit; constitution Principle VI): this unit restructures the driver's core, so the architect's design audit of `plan.md` happens before implementation. Maintainability is the focus.
