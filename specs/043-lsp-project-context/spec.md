# Feature Specification: blangd project context — blang.toml, sibling sources, path/git dependencies from source

**Feature Branch**: `043-lsp-project-context`

**Created**: 2026-11-26

**Status**: Draft

**Input**: User description: "U3 specs/043-lsp-project-context. blangd finds a document's blang.toml (walking up), includes the project's other .b sources, and resolves [deps] path dependencies and ALREADY-FETCHED git dependencies from source, transitively, with the same module identities and D7 capabilities bcc build applies. blangd never spawns a process or touches the network; an unresolvable dependency yields exactly one located diagnostic, never a crash; unsaved buffers take precedence over on-disk copies wherever a file is part of another document's context; every rule comes from the U1 shared entry point. Do not reopen D1–D17."

**Epic / unit**: `docs/epics/modules-v2-lsp/` U3

**Covers**: REQ-004, REQ-005, REQ-006, REQ-009 (project half)

**Depends on**: U2 (`specs/042-lsp-stdlib-context`, PR #159)

**Binding inputs**:
- design.md **D2**: dependencies from source, with interface equivalence;
- design.md **D6**: the project locator mirrors `bcc`'s rules;
- the U3 design audit [`docs/epics/modules-v2-lsp/design-audit-U3.md`](../../docs/epics/modules-v2-lsp/design-audit-U3.md), verdict PROCEED. Its conditions R1–R3 bind the plan, and its R4–R10 are settled below;
- the module decision record `docs/epics/modules-v2/overview.md` D1–D17, which is not reopened.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A project with dependencies shows no false diagnostics (Priority: P1)

A developer opens `main.b` of a `blang.toml` project in the editor. The project depends on a path library (`mathlib = { path = "../mathlib" }`) or on a git dependency that `bcc build` has already fetched. It may use the dependency qualified (`mathlib.add`), aliased (`import mathlib as m;`), by name-capability (`Counter(5)`), or by use-capability only (`var b = midx.get_box(7); b.get()`).

Today the editor reports `undefined symbol 'mathlib'`, `unknown type 'Counter'` and similar false errors on every such file, because it compiles the file without its project. After this unit the editor shows exactly what `bcc build` shows: nothing for a project that builds clean, and `bcc`'s own errors and warnings otherwise.

**Why this priority**: This is overview done-condition 3, and it removes the remaining class of false diagnostics on real programs. After U2, the two project files in the overview table (`test_build/myapp/main.b`, `test_build/counterapp/main.b`) still show 15 and 8 errors that `bcc build` does not.

**Independent Test**: The project corpus check opens each corpus project's `main.b` through blangd. It compares blangd's diagnostics against the project's **reference compile**: the final `qcc` command that `bcc build -v` prints. The two must be identical: same severity, line, column and message, for errors and warnings.

**Acceptance Scenarios**:

1. **Given** `test_build/myapp/main.b` is opened (a path dep on `mathlib`, qualified calls), **When** blangd publishes, **Then** the list equals the project reference compile's diagnostics, which is none.
2. **Given** `test_build/usebox/main.b` is opened, **When** blangd publishes, **Then** the result is the reference compile's diagnostics (none). That file imports only `midx`, holds a `Box<int>` from `boxq` through `var` inference, and never names `boxq` (D7 use-capability through a transitive, un-named dependency).
3. **Given** `test_build/aliasapp/main.b` (`import mathlib as m;`) and `test_build/diamondapp/main.b` (a shared dependency reached twice) are opened, **When** blangd publishes, **Then** each equals its reference compile.
4. **Given** a project whose dependency exports a private `init`, a private method, or opaque fields, and a consumer that reaches into them, **When** the consumer is opened, **Then** blangd reports the same located privacy error `qcc` reports against that dependency's interface. Each `test_files/fail/xmodule/*` fixture is exercised this way.
5. **Given** a project with two source files, where `main.b` calls a function defined in a sibling `.b` file of the same project, **When** `main.b` is opened, **Then** the call resolves exactly as it does in `bcc build`.

---

### User Story 2 - Broken project setups are reported once, clearly, and never crash the editor (Priority: P1)

A developer's project has a problem: a path dependency that does not exist, a git dependency that has never been fetched, a dependency cycle, or a malformed `blang.toml`. The editor shows **one** located diagnostic that names the problem. It does not show a cascade of follow-on errors, blangd does not crash, and blangd never tries to fix the problem by running `git` or touching the network.

**Why this priority**: REQ-005. The epic's hard constraints are "never crash" and "never spawn a process or touch the network". `blang.toml` files and dependency paths in a workspace are untrusted input (constitution Quality Gate 7).

**Independent Test**: Negative transcript goldens cover a missing path dependency, an unfetched git dependency and a malformed `blang.toml`. Each shows exactly one located diagnostic for the setup problem.

A separate test runs blangd with a `PATH` whose first entry contains a `git` shim that writes a marker file. It opens a project with an unfetched git dependency and fails if the marker exists.

**Acceptance Scenarios**:

1. **Given** a project whose `[deps]` names a path that does not exist, **When** `main.b` is opened, **Then** exactly one diagnostic reports the unresolvable dependency. It is located at the importing `import` statement, or at the `blang.toml` entry when nothing imports it. The rest of the document is still checked.
2. **Given** a project with a git dependency that has never been fetched, **When** `main.b` is opened, **Then** exactly one diagnostic says the dependency is not fetched and tells the user to run `bcc build`. No `git` process is started and no network connection is made.
3. **Given** a malformed `blang.toml`, **When** a project file is opened, **Then** exactly one located diagnostic reports the malformation (see FR-016), and blangd does not crash.
4. **Given** a dependency cycle (A depends on B depends on A), **When** a file of A is opened, **Then** exactly one located diagnostic reports the cycle, and blangd does not crash or hang.

---

### User Story 3 - Unsaved edits are seen across the whole project (Priority: P2)

A developer has both `main.b` and a dependency's `lib.b` open, and edits `lib.b` without saving. For example, they rename a `pub` function. When they next edit `main.b`, its diagnostics reflect the **unsaved** `lib.b`, not the copy on disk. The same holds for an unsaved sibling source of the same project.

**Why this priority**: REQ-006. An editor that type-checks against stale disk copies contradicts what the user is looking at. It is P2 because the saved-file case (US1) already gives correct results.

**Independent Test**: A transcript golden opens a consumer and an open dependency file, changes the dependency's buffer (unsaved), and then re-triggers the consumer. The consumer's republished diagnostics change accordingly.

**Acceptance Scenarios**:

1. **Given** `main.b` (calls `lib.helper()`) and the dependency's source are both open, **When** the dependency's buffer is edited to remove `helper` and `main.b` is then changed or re-opened, **Then** `main.b`'s diagnostics include the error for the missing function, even though the disk copy still defines it.
2. **Given** the same setup, **When** the dependency's buffer is edited back, **Then** `main.b`'s next publish is clean again.
3. **Given** a document that is not open, **When** it is part of another document's context, **Then** its on-disk copy is used.

---

### User Story 4 - Editing a project stays responsive (Priority: P2)

A developer types in `test_build/usebox/main.b`. Every edit re-derives the project context, which means three small dependency compiles plus the program. Diagnostics still update promptly.

**Why this priority**: REQ-009, project half.

**Independent Test**: `tools/lsp_latency.py` runs 20 edits on `test_build/usebox/main.b` against the non-sanitizer build. The median `didChange` → `publishDiagnostics` time must be ≤ 500 ms.

**Acceptance Scenarios**:

1. **Given** the non-sanitizer blangd, **When** the latency tool runs on `test_build/usebox/main.b`, **Then** the median is ≤ 500 ms.

---

### Edge Cases

- **A file in a subdirectory of a project.** It is not one of the files `bcc build` compiles, because the project's sources are the `.b` files in the project root only. It is therefore compiled in U2's single-file stdlib context, exactly like `bcc file.b`, with no project context (R5).
- **A library project's own file.** It is compiled with the plan `bcc build` uses for a library: no stdlib and no combine (R4). It does **not** get U2's stdlib context.
- **Nested projects.** The nearest ancestor `blang.toml` wins, and the walk stops at the first one found.
- **A dependency whose own compile fails**, including a P9 export violation. `bcc build` aborts in this case, so there is no reference compile. blangd treats the dependency as unresolvable: one diagnostic, and the consumer is compiled without it (R6).
- **A dependency reached twice (diamond).** It is compiled once and appears once, deduplicated by canonical origin, exactly as `bcc` does.
- **A git dependency without a pin**, i.e. no `tag`/`rev`. `bcc` rejects it, so blangd reports one located diagnostic at the `blang.toml` entry.
- **The editor runs with a different `HOME`/`XDG_CACHE_HOME` than the user's shell.** blangd looks for fetched git dependencies using its **own** process environment. A different environment can therefore see a different (possibly empty) cache, and the result is an unfetched-dependency diagnostic, never a fetch (R9).
- **Hostile `blang.toml` contents and paths** (absolute paths, `..`, symlinks, huge files). Paths are resolved exactly as `bcc` resolves them, relative to the declaring `blang.toml`. Files are only read, never executed. Malformed input produces a located diagnostic, never a crash.
- **The open document is the dependency itself** (opening `mathlib/lib.b` while working on `myapp`). Each document gets its own project's context, so `lib.b` is compiled as the library it belongs to (R4).
- **A non-open project file changes on disk between reparses.** The next reparse reads the new disk copy. Dependents are not proactively re-diagnosed (overview non-goal); they pick the change up on their own next open or change.

## Requirements *(mandatory)*

### Functional Requirements

**Project discovery and plan (REQ-004)**

- **FR-001**: For an open document, blangd MUST look for `blang.toml` in the document's directory and then each ancestor directory, stopping at the first one found. If none is found, the document keeps U2's single-file stdlib context unchanged.
- **FR-002**: The document belongs to the found project **only if** it is one of the project's source files, using the same selection `bcc build` uses: the `.b` files in the project root, in the same order. Otherwise the document keeps U2's single-file context (R5).
- **FR-003**: For a member document, blangd MUST compile through the shared project-compile entry point (U1's `ProjectCompile::run`) with **the plan `bcc build` hands its final `qcc` invocation** for that project:
  - the project's sources;
  - for a binary project, the stdlib set that `bcc build` selects over all project sources, in combine mode;
  - for a library project, no stdlib and no combine (R4);
  - the transitive dependency interfaces, in `bcc`'s topological order;
  - each module stamped with the canonical origin `bcc` passes.

  The plan is the same whichever project file is open. Only diagnostics located in the open document are published (design C5).
- **FR-004**: Dependencies MUST be resolved **from source** (design D2):
  - **path dependencies** are resolved relative to the `blang.toml` that declares them, exactly as `bcc` does;
  - **git dependencies** are used only when already checked out in `bcc`'s git cache location (`<cache>/blang/git/<name>-<hash(url@pin)>`), where the cache root is chosen as `bcc` chooses it.

  Resolution is recursive and transitive, deduplicated by canonical origin.
- **FR-005**: Each dependency's **interface**, meaning what the consumer resolves against, MUST be byte-for-byte the `.bmod` that a cold `bcc build` of that dependency would emit. That covers:
  - `pub`-only items;
  - opaque struct bodies;
  - declared private `init`s;
  - D15 `table`/`@json` field metadata;
  - P9-filtered signatures;
  - protocol-conformance records;
  - `// foreign-type:` headers;
  - verbatim generic bodies.

  The consumer therefore sees exactly the privacy, D7 name/use capability, aliasing and collision behavior `bcc build` applies. The interface MUST be produced by the same code that produces a `.bmod`, never by a second export implementation (design D2; audit §1).
- **FR-006**: Module identities MUST match `bcc build`'s:
  - the project's own modules use the project directory's canonical path;
  - each dependency uses its directory's canonical path;
  - the interface path and the import name are the dependency's `[project] name`, as `bcc` names its `.bmod`.

  Mangling identities, and every diagnostic that depends on module identity, therefore equal `bcc`'s (audit §2).
- **FR-007**: **One rule, one copy** (overview constraint; audit R3). Every project rule blangd applies MUST come from code that `bcc` itself calls. That covers source selection, dependency path resolution, git cache location, the dependency closure and its order, origin selection and `blang.toml` parsing. blangd MUST NOT contain its own copy of any of these. Moving such a rule out of `bcc` into shared code MUST leave `bcc`'s behavior byte-identical.

**Failure handling (REQ-005)**

- **FR-008**: blangd MUST NOT spawn any process or open any network connection. In particular it never runs `git`. A git dependency that is not already checked out is a diagnostic, never a fetch (design C4). The existing binary-level `blangd_no_subprocess` check MUST keep passing.
- **FR-009**: Each unresolvable dependency MUST produce **exactly one** located diagnostic (design C5), located at the first `import` of that dependency in the open document, or at the dependency's `blang.toml` entry when the document does not import it. An unresolvable dependency is any of:
  - a missing path;
  - an unfetched or unpinned git dependency;
  - a dependency whose own compile fails (R6);
  - a cycle.

  The rest of the document is still checked, compiled without that dependency, and no cascade of follow-on errors for that dependency is published.
- **FR-010**: Malformed or hostile project input MUST never crash blangd, hang it, write to its protocol channel, or produce output on its stderr. That covers an unparsable `blang.toml`, unreadable files, dependency paths that do not exist, symlinks, and very large files. All diagnostic text goes through published diagnostics only, so the transcript harness's strict-framing check keeps passing.

**Unsaved buffers (REQ-006)**

- **FR-011**: Wherever a file is part of a document's compile context, its **open, possibly unsaved** buffer MUST be used instead of the disk copy. That covers the document itself, a sibling project source, and a dependency source. This applies both to compiling the file and to producing its interface, including the verbatim generic bodies an interface carries (audit R1).
- **FR-012**: Opening, changing or closing one document does not proactively re-publish other open documents (overview non-goal). Each document reflects the current buffers the next time it is compiled.

**Ownership and isolation**

- **FR-013**: Each document compile MUST be isolated from every other compile. No dependency namespace, symbol or interface built for one compile may be visible to, or reused by, another compile, whether of the same document, another document, or a nested dependency compile (audit R2, closing U1's known item I6).
- **FR-014**: Every module, scope, interface and source AST built for a document's context MUST be owned by that document's compile result. It MUST be released with the result, in an order that never leaves a live reference pointing at freed state. This is the KG-7 invariant: an expression's link to the definition it resolved to is non-owning (`specs/041-shared-project-compile/checklists/kg7-resolution-refs.md`).

  The dependency **source** ASTs are kept for later navigation (U4), and they follow the same rule. No state is reused across reparses unless it is held under shared ownership by every result that resolves into it.

**Tests and tools**

- **FR-015**: The corpus check (`tools/lsp_diag_corpus.py`, U2) MUST gain a `--corpus project` mode, run by `test_lsp.sh`.
  - **Projects.** For every `test_build/*` binary project that `test_build/run_build_tests.sh` builds successfully, it asserts that blangd's diagnostics on the project's `main.b` equal that project's reference compile: the final `qcc` command `bcc build -v` prints, errors and warnings, same severity, line, column and message.
  - **`fail/xmodule` fixtures.** For every `test_files/fail/xmodule/*` fixture, it synthesizes a temporary two-project layout in a scratch directory: a library containing `lib.b` and a binary containing `consumer.b` with a path dependency on it. It asserts that blangd's diagnostic matches `qcc consumer.b lib.bmod`'s on line, column and message, as `run_tests.sh` compiles these fixtures today.

  The list of projects is committed and checked for completeness, as the stdlib corpus is.
- **FR-016**: A malformed `blang.toml` MUST produce one located **Warning** at the offending line, after which blangd continues with the partial configuration, exactly as `bcc` continues (R7). Every diagnostic located in a `blang.toml` (malformed lines, unpinned git dependencies, dependencies that nothing imports) MUST be published **against the open document at line 1, column 1**, with the message naming the `blang.toml` path and line (R8).
- **FR-017**: Committed transcript goldens under `test_files/lsp/` MUST cover:
  - **(a)** a missing path dependency;
  - **(b)** an unfetched git dependency;
  - **(c)** a malformed `blang.toml`, with one located diagnostic each and no crash;
  - **(d)** an unsaved edit to an open dependency file that changes the consumer's diagnostics (US3).
- **FR-018**: A committed test MUST run blangd with a `PATH` whose first directory contains a `git` program that writes a marker file. It opens a project with an unfetched git dependency, and fails if the marker exists or if the expected single diagnostic is absent.
- **FR-019**: `tools/lsp_latency.py` MUST be run by `test_lsp.sh` on `test_build/usebox/main.b` in addition to U2's file, and MUST report a median ≤ 500 ms on the non-sanitizer build. If the floor is missed, the implementer MUST raise a question before adding any caching (R10; epic sequencing note).
- **FR-020**: The `parser_no_leak` ctest MUST gain a project-context reparse loop of at least 50 reparses, using a fixture whose user AST resolves into a dependency interface. It MUST be LeakSanitizer-clean under `build-asan`. Existing assertions stay unmodified.
- **FR-021**: `qcc` and `bcc` observable behavior MUST NOT change (design C2). This covers every rule moved into shared code (FR-007) and any change to interface emission (FR-011). Every pre-existing gate stays green with no golden edits:
  - `run_tests.sh` in both build modes;
  - `test_codegen.sh`, plus `--leak-check` and `--selfcheck`;
  - `test_build/run_build_tests.sh`, including the committed `.bmod` goldens under `test_files/golden/bmod/`;
  - `ctest`;
  - every existing `test_lsp.sh` golden and the U2 stdlib corpus.

### Key Entities

- **Project**: a `blang.toml` together with its directory. It has a name, a type (binary or library), its root `.b` sources in `bcc`'s order, and its declared dependencies.
- **Dependency**: a declared `[deps]` entry, resolved to a source directory with a `blang.toml`. A path entry is resolved relative to the declaring file. A git entry is resolved to the already-fetched cache checkout. It has a canonical origin, an import name (its `[project] name`) and its own transitive dependencies.
- **Dependency interface**: the exact `.bmod` text the dependency's own build would emit, produced from its current sources (buffers first), plus the dependency's source AST kept for navigation.
- **Project compile plan**: the plan `bcc build`'s final `qcc` invocation uses. It holds sources, optional stdlib, interfaces in topological order, and origins. It is fed to the shared entry point.
- **Setup diagnostic**: the single located diagnostic for an unresolvable dependency, a cycle or a malformed `blang.toml`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For every one of the 10 `test_build` binary projects `run_build_tests.sh` builds (`aliasapp`, `boxapp`, `counterapp`, `diamondapp`, `myapp`, `printapp`, `sizeapp`, `timerapp`, `todoapp`, `usebox`), blangd's diagnostics on `main.b` equal the project reference compile, a 100% match. For the two overview-table project files, `myapp` and `counterapp`, this means **zero** errors in the editor (down from 15 and 8 after U2), because their project reference compiles exit 0. A file is clean when its reference compile exits 0; diagnostics equal the reference exactly, errors and warnings.
- **SC-002**: For every one of the 11 `test_files/fail/xmodule/*` fixtures, compiled as a synthesized two-project layout, blangd reports the same located diagnostic `qcc` does (same line, column and message), a 100% match.
- **SC-003**: Each of the four failure setups (missing path dependency, unfetched git dependency, malformed `blang.toml`, dependency cycle) yields exactly one setup diagnostic and no crash. The first three are pinned by goldens, and the git shim test sees zero `git` invocations.
- **SC-004**: An unsaved edit to an open dependency changes the consumer's next diagnostics, pinned by a golden.
- **SC-005**: The median edit-to-diagnostics time on `test_build/usebox/main.b` is ≤ 500 ms over 20 edits.
- **SC-006**: At least 50 project-context reparses leak nothing under LeakSanitizer.
- **SC-007**: Every pre-existing gate stays green, with no `qcc`/`bcc` golden or diagnostic-text change, and the U2 stdlib corpus stays 179/179.

## Assumptions

Each R-numbered item below is a design-audit decision (`design-audit-U3.md` §3), recorded with Mira's recommended default. Items marked **UNCONFIRMED** are judgment calls that the PM may overrule, each with a matching `DECISION:` record.

- **R1–R3 are binding on the plan, not decisions.**
  - **R1**: interface emission slices verbatim bodies from the parsed text, buffers included, never from a stale disk cache. This is captured as FR-011.
  - **R2**: each compile is isolated, with no process-global dependency namespaces. This is captured as FR-013.
  - **R3**: `bcc`'s project helpers become shared code, plus an error sink for `blang.toml` parsing so blangd never writes to stderr. This is captured as FR-007/FR-010.
- **R4 — Library projects get the library plan.** **UNCONFIRMED.** A library's own file is compiled with no stdlib and no combine, mirroring `bcc build` for a library. It is not compiled in U2's stdlib context.
  - *Alternative:* give library files U2's stdlib context. That would disagree with `bcc build` for the library and is a blangd-only rule.
  - *If changed:* FR-003's library clause changes.
- **R5 — Membership means `bcc` would compile the file.** **UNCONFIRMED.** A `.b` file under a project's subdirectory is not a project source, so it falls back to U2's single-file context.
  - *Alternative:* treat any file under the project tree as a member. That would compile files `bcc build` never compiles.
  - *If changed:* FR-002 changes.
- **R6 — A dependency whose own compile fails is unresolvable.** **UNCONFIRMED.** It gets one diagnostic at the `import` (else the toml entry), and the consumer is compiled without it. `bcc build` aborts in this case, so no reference exists, and this keeps the editor useful.
  - *Alternative:* publish the dependency's own errors against the consumer. That violates C5.
  - *If changed:* FR-009's failure list changes. This does not affect the corpus, which covers successfully built projects only.
- **R7 — A malformed `blang.toml` is a Warning, and compilation continues.** **UNCONFIRMED.** This mirrors `bcc`, which warns and proceeds with the partial configuration.
  - *Alternative:* make it a hard error that stops the project context. That would be a blangd-only rule stricter than `bcc`.
  - *If changed:* FR-016's severity and continuation change.
- **R8 — Toml-located diagnostics are published on the open document at 1:1, naming the toml path and line.** **UNCONFIRMED.** The user sees them where they are working. A `blang.toml` usually is not open, and publishing to an unopened URI is weakly supported by clients.
  - *Alternative:* publish them on the `blang.toml` URI at the real line.
  - *If changed:* FR-016's placement and the (b)/(c) goldens change. FR-009's `import`-located diagnostics are unaffected.
- **R9 — The git cache is located from blangd's own environment** (`XDG_CACHE_HOME`, then `HOME`, then `bcc`'s temp fallback, all as `bcc` chooses). This is derived from audit R9 and is acceptable as stated. It is documented to users in the editor README, in U5.
- **R10 — No caching.** Each reparse re-derives the dependency chain (design D5). If the `usebox` latency floor is missed, raise a question before adding any cache, because caching re-opens the KG-7 bug class. This is derived from audit R10 and the epic's sequencing note.
- **Parity is defined against a cold `bcc build`** (audit §1). A `.bmod` served from `bcc`'s build cache that was built at another path can differ in foreign-type digests. That is pre-existing `bcc` behavior and is out of scope; the corpus builds in place.
- **The reference compile for a project** is the final `qcc` command `bcc build -v` prints, run as printed (overview done-condition 3). U2's stdlib corpus keeps using `bcc -v -S <file>` for single files. U2's corpus entries for `test_build/myapp/main.b` and `test_build/counterapp/main.b` move from that single-file reference to the project corpus.
- **Sibling-source coverage** needs a new committed multi-file project fixture, because every existing `test_build` project has exactly one source file. It goes into the project corpus (US1/AC5).
- **Out of scope**, per the overview's non-goals and later units:
  - cross-module go-to-definition and hover (U4), although this unit keeps the dependency source ASTs that U4 needs;
  - re-diagnosing dependents on change;
  - fetching dependencies;
  - reading built `.bmod` artifacts;
  - multi-root workspaces;
  - the module search path (KG-10).
