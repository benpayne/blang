# Feature Specification: blangd stdlib/prelude context + stdlib discovery

**Feature Branch**: `042-lsp-stdlib-context`

**Created**: 2026-11-26

**Status**: Draft

**Input**: User description: "U2 specs/042-lsp-stdlib-context. blangd compiles an open single-file document (no blang.toml) with the SAME stdlib set and prelude that `bcc file.b` uses, through the shared ProjectCompile::run entry point from U1 — never a blangd-only rule copy. Deliver the stdlib-location discovery chain (blangd-relative stdlib/, LSP initializationOptions.stdlibPath, BLANG_STDLIB_DIR), each with a transcript golden; the committed corpus check tools/lsp_diag_corpus.py (run by test_lsp.sh) asserting blangd publishes exactly the reference compile's diagnostics for the overview table's six files, every examples/*/main.b, and every test_files/codegen_*.b whose reference compile exits 0; the latency floor tool tools/lsp_latency.py. Carry-forward constraint from U1's KG-7 change: AST back-references are non-owning, so a cached stdlib context must be kept alive by every result that resolves into it. blangd must never spawn a process or touch the network."

**Epic / unit**: `docs/epics/modules-v2-lsp/` U2 · **Covers**: REQ-002, REQ-003, REQ-009 (single-file half) · **Depends on**: U1 (`specs/041-shared-project-compile`, PR #158) · **Binding record**: `docs/epics/modules-v2/overview.md` D1–D17 (not reopened)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Correct code is not flagged as broken (Priority: P1)

A developer opens a single BLang file in their editor. The file uses the prelude (`Map`, `Set`, `Buffer`) with no imports, or imports stdlib modules (`import net;`, `import fs;`, `import cli;`). Today the editor shows a dozen false errors on such a file (`unknown type 'Map'`, `undefined symbol 'fs'`) even though `bcc file.b` compiles it cleanly. After this unit, the editor shows exactly what the compiler shows: nothing for a clean program, and the compiler's own errors and warnings for a broken one.

**Why this priority**: False diagnostics on essentially every real program are the core problem the epic exists to fix (overview "Why"). An editor that flags correct code teaches users to ignore it. This story is the largest share of those false diagnostics.

**Independent Test**: Open each corpus file in blangd. Separately run the reference compile (the `qcc` command line that `bcc -v -S <file>` prints). The diagnostics blangd publishes for the document must equal the reference compile's diagnostics for that file: same severity, line, column and message, errors and warnings both.

**Acceptance Scenarios**:

1. **Given** `test_files/codegen_prelude_no_import.b` (zero imports, uses `Map`/`Set`/`Buffer`) is opened, **When** blangd publishes diagnostics, **Then** the list is empty, because its reference compile exits 0 with no diagnostics.
2. **Given** `examples/kv/main.b` (`import fs;`, `import cli;`, qualified `cli.has_flag(...)`) is opened, **When** blangd publishes diagnostics, **Then** they equal the reference compile's: none.
3. **Given** a document with a genuine Sema error and an unused stdlib import, **When** it is opened, **Then** blangd publishes the same error and the same `unused-import` warning, at the same positions with the same text, that the reference compile reports.
4. **Given** a document that is edited (`didChange`) to add `import timer;` and use `timer.after(...)`, **When** blangd republishes, **Then** the import-gated module is now part of the context and the use resolves, exactly as a fresh reference compile of the edited text would.
5. **Given** a document importing a module that is neither stdlib nor available (`import nonesuch;`), **When** it is opened, **Then** blangd publishes the reference compile's located `unknown module 'nonesuch'` error.

---

### User Story 2 - The stdlib is found wherever blangd is installed (Priority: P2)

A developer runs blangd from a build tree, from an install location, or from an editor configuration that points at a specific stdlib checkout. blangd locates the stdlib directory through a fixed, documented precedence: the editor's `initializationOptions.stdlibPath`, then the `BLANG_STDLIB_DIR` environment variable, then the `stdlib/` directory next to the blangd executable. When no usable stdlib is found, the developer sees one clear diagnostic saying so, and blangd falls back to today's single-file behavior rather than failing.

**Why this priority**: Without discovery, Story 1 only works from the build tree. The precedence is fixed by design record D3 (epic design.md). P2 because the build-tree default already serves CI and developers.

**Independent Test**: One transcript golden under `test_files/lsp/` per route: `initializationOptions.stdlibPath`, `BLANG_STDLIB_DIR`, the blangd-relative default, and the stdlib-missing fallback. Each shows a prelude-using document resolving, or, for the fallback, the single fallback diagnostic.

**Acceptance Scenarios**:

1. **Given** `initialize` carries `initializationOptions.stdlibPath` naming a valid stdlib directory, **When** a prelude-using document is opened, **Then** it resolves against that directory, even when `BLANG_STDLIB_DIR` names a different valid directory and a blangd-relative `stdlib/` exists.
2. **Given** no `stdlibPath` but `BLANG_STDLIB_DIR` names a valid stdlib directory, **When** the document is opened, **Then** it resolves against the environment directory.
3. **Given** neither is set, **When** the document is opened, **Then** it resolves against `<blangd-executable-dir>/stdlib`.
4. **Given** the selected route names a directory that is missing or not a usable stdlib, **When** any document is opened or changed, **Then** each publish for that document carries exactly one stdlib-not-found diagnostic naming the route and path, and the document's remaining diagnostics are today's single-file ones.

---

### User Story 3 - Editing stays responsive with the larger context (Priority: P2)

A developer types in a real program (`examples/todo_app/main.b`). Each edit now recompiles the program together with the stdlib. The developer still sees updated diagnostics promptly.

**Why this priority**: REQ-009. A correct but slow editor is not adopted. P2 because the measured cost (about 0.09 s for the whole stdlib + program compile) is far inside the budget.

**Independent Test**: `tools/lsp_latency.py` drives 20 `didChange` edits on `examples/todo_app/main.b` against the non-sanitizer blangd and reports the median `didChange` → `publishDiagnostics` time, which must be ≤ 500 ms.

**Acceptance Scenarios**:

1. **Given** the non-sanitizer `build/` blangd, **When** the latency tool runs 20 edits on `examples/todo_app/main.b`, **Then** the median is ≤ 500 ms and the tool exits 0.
2. **Given** `test_lsp.sh` runs with a sanitizer `BUILD_DIR`, **When** it reaches the latency leg, **Then** it skips that leg with a printed notice instead of measuring.

---

### Edge Cases

- **Diagnostics inside stdlib files.** A diagnostic located in a stdlib file is never published against the user's document (design C5). For every corpus file, the reference compile must report no stdlib-located diagnostic. The harness asserts this premise so a stdlib regression fails loudly rather than being silently filtered.
- **The document is itself a stdlib file** (e.g. opening `stdlib/net.b`). blangd builds the same plan the reference compile builds for that file, with no special case, and publishes the diagnostics located in that document. Parity with `bcc stdlib/net.b` is the rule. This case is not in the corpus.
- **The document is unsaved or has no on-disk path.** The import scan reads the editor's in-memory text, never the disk copy. The document keeps U1's no-identity policy (no `realpath`, no digest). Digests feed only code generation, never diagnostics (U1 research R4).
- **The document has a `blang.toml` nearby.** It is ignored in this unit; project context is U3. Such a document (e.g. `test_build/myapp/main.b`) is compiled exactly like `bcc main.b` in single-file mode. Its published diagnostics must still equal that single-file reference compile, including the unresolved-dependency errors that reference reports.
- **Imports that name a non-stdlib module.** Only names with a `<stdlib>/<name>.b` file join the context (the shared selection rule). An unknown name produces the reference compile's located unknown-module error. A combined compile is an authoritative module set, so the import-diagnostics pass now runs for every document, exactly as it does under `bcc file.b`.
- **Hostile or odd stdlib paths** (relative path, symlink, a directory without the base modules, a path with spaces, a non-directory). Each is handled by the validity rule (FR-007). blangd only reads `<dir>/<name>.b` for names from the fixed module list or from import identifiers (`[A-Za-z0-9_]+`), so a path can never escape the chosen directory through an import name.
- **The stdlib directory disappears or changes while blangd runs.** Discovery happens once, at `initialize`. Later reparses read the stdlib files as they are on disk at that moment. A file that has vanished produces the same outcome the reference compile would have for a missing input.
- **Rapid edits.** Each `didChange` is a full recompile of the context (design D5). No result from a previous reparse is reused unless FR-012's ownership rule is met.

## Requirements *(mandatory)*

### Functional Requirements

**Context selection (REQ-002)**

- **FR-001**: For every open document, blangd MUST compile through the shared project-compile entry point (`ProjectCompile::run`, U1) with a combine-mode plan:
  - the stdlib inputs are exactly the files `BlangStdlib::selectStdlibFiles( <discovered stdlib dir>, BlangStdlib::scanImports( <document text> ) )` returns, in that order;
  - the document's in-memory text is the last (user) input.

  This is the plan `bcc <file>` hands `qcc`. blangd MUST NOT contain its own copy of any selection, routing, prelude, import-diagnostic or Sema rule (overview constraint; REQ-001).
- **FR-002**: The import scan MUST run over the document's current in-memory text, including unsaved edits, on every open and every change.
- **FR-003**: blangd MUST publish, for the document, exactly the diagnostics the reference compile reports for that document's file: every error and warning, with the same severity, line, column and message. The **reference compile** is the `qcc` command line that `bcc -v -S <file>` prints (overview done condition 2).
- **FR-004**: Diagnostics located in any file other than the document (stdlib files) MUST NOT be published against the document (design C5).
- **FR-005**: The document's identity policy MUST stay U1's no-identity policy: no `realpath`, no digest stamping. Defining-origin stamping still runs for every module, exactly as the shared entry point does it.

**Stdlib discovery (REQ-003)**

- **FR-006**: blangd MUST select the stdlib directory once, when it handles `initialize`, by this precedence (design D3):
  1. `initializationOptions.stdlibPath`, if present and a non-empty string;
  2. else the `BLANG_STDLIB_DIR` environment variable, if set and non-empty;
  3. else `<directory of the blangd executable>/stdlib`.

  The **first route that is set** is the selected route. A later route is never consulted to rescue a set-but-unusable earlier route.
- **FR-007**: A selected directory is **usable** iff it is a readable directory containing every base-set module file the shared selection always includes (`sys.b`, `buffer.b`, `collections.b`, `fs.b`, `net.b`). The path is used as given; no normalization or execution is applied to it.
- **FR-008**: When the selected directory is not usable, every `publishDiagnostics` for every document MUST contain exactly one **stdlib-not-found** diagnostic, in addition to the document's own diagnostics:
  - located at line 1, column 1 of the document;
  - severity Warning;
  - its message names the route used and the path tried, and lists the three ways to configure the stdlib.

  The document is then compiled exactly as before this unit: single-file, no stdlib, no import-diagnostics pass.
- **FR-009**: Discovery MUST only test for and read files. It MUST NOT spawn a process, open a network connection, or follow any instruction found in the path or the files (constitution Quality Gate 7; design C4). The `blangd_no_subprocess` ctest from U1 keeps holding.

**Tests and tools**

- **FR-010**: A committed **corpus check**, `tools/lsp_diag_corpus.py --corpus stdlib`, MUST be run by `test_lsp.sh`. It reads a committed corpus list, and for each file it:
  - opens the file through blangd and collects the published diagnostics;
  - obtains the reference compile from that build's `bcc -v -S <file>`;
  - runs that `qcc` command;
  - asserts the two diagnostic sets are equal (severity, line, column, message);
  - asserts that the reference compile reports no diagnostic located outside the file (the FR-004 premise).

  It exits non-zero on any mismatch, naming the file and the first differing diagnostic.

  The committed list MUST contain at least:
  - the six files of the overview's false-diagnostics table: `test_files/codegen_prelude_no_import.b`, `test_build/myapp/main.b`, `test_files/codegen_http_routing.b`, `test_build/counterapp/main.b`, `examples/kv/main.b`, `examples/todo_app/main.b`;
  - every `examples/*/main.b`;
  - every `test_files/codegen_*.b` whose reference compile exits 0.

  The list MUST be regenerable and checkable: the tool can report any qualifying file missing from the list, and that check fails the run.
- **FR-011**: A committed **latency tool**, `tools/lsp_latency.py`, MUST be run by `test_lsp.sh` on `examples/todo_app/main.b`:
  - it drives 20 `didChange` edits;
  - it reports the median `didChange` → `publishDiagnostics` time;
  - it fails when that median exceeds 500 ms.

  It accepts several files, so U3 can add `test_build/usebox/main.b` without changing the tool. Under a sanitizer `BUILD_DIR`, `test_lsp.sh` skips this leg and prints a notice.
- **FR-012**: **Ownership (KG-7 carry-forward).** Every module, scope and AST built for a document's context, stdlib modules included, MUST be owned by that document's compile result, or by an explicit cache structure that result holds, and MUST be released before the process resolver.

  Since U1, an expression's link to the definition it resolved to is **non-owning**; the definition's scope owns it (`specs/041-shared-project-compile/checklists/kg7-resolution-refs.md`). Therefore:
  - the default is a full recompile per reparse (design D5);
  - **if** any parsed stdlib state is reused across reparses, every compile result whose AST can resolve into that state MUST hold shared ownership of it, so the state outlives every such result.

  Reusing it any other way is a use-after-free and is forbidden.
- **FR-013**: The `parser_no_leak` ctest MUST be extended with a stdlib-context `compileDocument` loop of at least 50 reparses. It MUST stay LSan-clean under `build-asan`. Its existing assertions stay unmodified.
- **FR-014**: The stdlib-discovery transcript goldens MUST exist under `test_files/lsp/`:
  - one for `initializationOptions.stdlibPath`, which also shows it beating a set `BLANG_STDLIB_DIR`;
  - one for `BLANG_STDLIB_DIR`;
  - one for the blangd-relative default;
  - one for the stdlib-not-found fallback.

  Each runs under `test_lsp.sh` with strict framing. Any harness support they need (e.g. setting an environment variable for the server) MUST be added to the shared transcript driver, not to a one-off script.
- **FR-015**: Every pre-existing `test_lsp.sh` golden MUST still pass, or change only because the document now has the stdlib/prelude context. Each changed golden carries a one-line justification in the PR (workplan U2 (e)). A golden whose diagnostic **text** changes for any other reason is a defect to raise, not to regenerate.
- **FR-016**: The `fail/sema` re-verification leg of `test_lsp.sh` MUST keep covering every fixture:
  - fixtures that the reference context still rejects MUST be rejected through blangd with their expected pattern, as today;
  - `fail/sema/buffer_without_prelude.b` is rejected only because no prelude is present, and compiles clean under `bcc`. It MUST be exercised through the stdlib-not-found fallback route, where it is still rejected by the same expected pattern.

  No fixture or `.expected` file is edited.
- **FR-017**: `qcc` and `bcc` observable behavior MUST NOT change (design C2). Every pre-existing gate stays green:
  - `run_tests.sh` in both build modes;
  - `test_codegen.sh`, plus `--leak-check` and `--selfcheck`;
  - `test_build/run_build_tests.sh`;
  - `ctest` in `build` and `build-asan`, including `project_compile` and `blangd_no_subprocess`;
  - the reach-in gate;
  - examples and demos.

### Key Entities

- **Stdlib location**: the directory blangd resolved at `initialize`. Its attributes are the route that selected it (`initializationOptions`, environment or blangd-relative), the path, and whether it is usable. It is fixed for the session.
- **Document compile plan**: the shared entry point's ordered input list for one document. It holds the selected stdlib files followed by the document's in-memory text as the user input, in combine mode, under the no-identity policy.
- **Document compile result**: what one reparse produces. It owns the compile context (all modules, scopes and AST, stdlib included) and the diagnostics filtered to the document. It holds shared ownership of any reused stdlib state (FR-012).
- **Corpus list**: a committed file naming each corpus input, with the corpus it belongs to (`stdlib` now; `project` in U3).
- **Reference compile**: the `qcc` command line that `bcc -v -S <file>` prints for a corpus file, and its diagnostics.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On every file in the committed stdlib corpus, blangd and the reference compile report identical diagnostics, with a 100% match. The corpus includes the six overview files, all 6 `examples/*/main.b`, and every exit-0 `test_files/codegen_*.b`. A file is **clean** when its reference compile exits 0 (the overview's definition). For the four clean overview files, the editor shows **exactly** the reference compile's diagnostics, errors and warnings both, and **zero errors**. Before this unit each showed 4–16 false errors. (`examples/todo_app/main.b` correctly keeps the 2 `unused variable` warnings that `bcc` itself reports.)
- **SC-002**: Each of the four stdlib-discovery routes is pinned by a passing transcript golden, and the precedence case (configured path beats environment) is pinned too.
- **SC-003**: The median edit-to-diagnostics time on `examples/todo_app/main.b` is ≤ 500 ms over 20 edits on the non-sanitizer build.
- **SC-004**: Memory stays bounded across edits. At least 50 stdlib-context reparses report zero leaks under LeakSanitizer.
- **SC-005**: Every pre-existing gate stays green. Every changed LSP golden carries a per-file justification, and no `qcc`/`bcc` golden or diagnostic text changes.
- **SC-006**: blangd still starts no process and opens no network connection. The binary-level check passes, and its teeth leg still catches `bcc`.

## Assumptions

- **Precedence** is `initializationOptions.stdlibPath` > `BLANG_STDLIB_DIR` > `<blangd-dir>/stdlib`, per design.md D3 and workplan U2. The user description lists the three routes without an order; the design record's order governs. No `<prefix>/share/blang/stdlib` fallback is added (overview open question 2, default answer).
- **Set-but-unusable route does not fall through** (FR-006/FR-008). **UNCONFIRMED.** The first route that is set wins, and if it is unusable the user sees the not-found diagnostic. The alternative, trying the next route, would silently use a different stdlib than the one the user configured and hide the misconfiguration; it also makes the fallback untestable from the build tree. If the PM prefers fall-through, only FR-006's last sentence, the fallback golden's setup and FR-016's route change.
- **"Usable" means all five base modules are present** (FR-007). **UNCONFIRMED.** These are the files `bcc` always combines; a directory missing one cannot reproduce `bcc`'s context. The alternative is "directory exists", which matches `bcc`'s lenient per-file `access()` check but lets a wrong directory produce false diagnostics.
- **Fallback diagnostic: Warning at 1:1, published with every document publish** (FR-008). **UNCONFIRMED.** Warning, because the user's code is not wrong; the editor is misconfigured. It is repeated per publish because each LSP publish replaces the previous set. The alternative is an LSP `window/showMessage` notification, which is not a diagnostic and is not what the workplan's done condition (b) names.
- **Discovery is resolved once per session at `initialize`.** It is not re-read per reparse. This is derived from design D3 (configuration from `initialize`) and keeps transcripts deterministic.
- **No-identity policy kept for blangd** (FR-005), per U1 research R4: digests affect only mangling, never diagnostics. A future unit (U3) may introduce real origins for on-disk projects.
- **The `buffer_without_prelude` fixture** is exercised through the fallback route (FR-016). **UNCONFIRMED.** It asserts behavior without a prelude, which the stdlib context legitimately removes. I measured every one of the 58 `fail/sema` fixtures under the base-stdlib combine compile, and it is the only one that changes outcome (it becomes clean, matching `bcc`). The alternative is to exclude it from the LSP leg, which loses coverage.
- **Corpus scope** includes all six overview files, including the two project files `test_build/myapp/main.b` and `test_build/counterapp/main.b`. In this unit their reference compile is the single-file `bcc -v -S` line, which reports unresolved-dependency errors that blangd must match exactly. U3 re-points them at the project reference compile. This follows the user description and overview done condition 2, which name all six; workplan U2 (a) names four, so the six are a superset.
- **The reference compile runs as printed**, including code generation. Code-generation-only diagnostics cannot be reproduced by blangd (no LLVM). For files whose reference compile exits 0 none are expected, and the harness reports any it sees as a mismatch rather than ignoring them. The corpus check is required in the LLVM build that CI's `lsp` job uses.
- **No stdlib caching in this unit by default** (design D5). FR-012 states the rule any cache must follow, in case the latency floor requires one. Per the epic's sequencing notes, a latency miss is raised as a question before caching is added.
- Out of scope, per the overview's non-goals and later units: `blang.toml` project context and dependencies (U3); unsaved-buffer overlay for files *inside* another document's context (U3, REQ-006); cross-module definition and hover (U4); completion, references and rename.
