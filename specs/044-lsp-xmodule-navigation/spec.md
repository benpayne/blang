# Feature Specification: Cross-module go-to-definition and hover in blangd

**Feature Branch**: `044-lsp-xmodule-navigation`

**Created**: 2026-11-26

**Status**: Draft

**Input**: User description: "U4 specs/044-lsp-xmodule-navigation — add cross-module go-to-definition (textDocument/definition) and hover (textDocument/hover) on top of the U3 project/stdlib compile context. Definition returns a location in the defining file (file:// URI, need not be open) for a qualified dep function, an aliased one, a name-capability type, a method on a use-capability-only foreign value, a stdlib symbol, a prelude type, and a sibling-source symbol; hover returns the foreign declaration's signature with the deterministic display name for each of those cases. A definition must never point into a generated .bmod. Each case is a transcript golden under test_files/lsp/. U4 must NOT change same-file definition/hover behavior or any existing defn_*/hover_*/symbols_* goldens; blangd must never spawn a process or touch the network; every rule comes from the shared project-compile entry point."

**Epic**: `docs/epics/modules-v2-lsp/` (unit U4; covers REQ-007, REQ-008; epic done-conditions 4 and 5).

## Context

After U2 and U3, blangd compiles every open document in the same context `bcc`
would: the stdlib and prelude, the project's other sources, and its path and
already-fetched git dependencies, resolved from source. Navigation was not
updated, though. Go-to-definition and hover still work only for declarations in
the open document. A user who asks "where is `mathlib.add` defined?" or "what
does `b.get()` return?" gets nothing back, even though the compiler has already
resolved that reference.

Dependencies reach the consumer's compile through **interfaces** derived from
the dependency's source by the one exporter (U3, design.md D2). These are
in-memory interface texts whose nominal path is `<depDir>/<name>.bmod`, a file
that need not exist and is never what the user wants to open. The dependency's
own source compiles are kept alongside the consumer's compile for exactly this
unit (U3: "kept for U4 navigation").

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Jump to a dependency's definition (Priority: P1)

A developer editing a `blang.toml` binary project puts the cursor on a
qualified call into a path dependency (`mathlib.add(3, 4)`), on an aliased one
(`import mathlib as m; m.add(3, 4)`), or on a dependency type they named after
importing its module (`Counter c = Counter(5);`). They ask for the definition
and the editor opens the dependency's **source file** at that declaration. The
file does not have to be open in the editor.

**Why this priority**: This is the most common cross-module navigation, and
the epic's done-condition 4 lists it first. Without it, a project with
dependencies has no navigation past its own file.

**Independent Test**: Open `test_build/myapp/main.b`, `test_build/aliasapp/main.b`
and `test_build/counterapp/main.b` in transcript fixtures and request a
definition at the qualified call, the aliased call and the type name. Each
result is a `file://` URI of the dependency's `.b` source at the declaration's
line/column, pinned by a golden.

**Acceptance Scenarios**:

1. **Given** `myapp/main.b` is open and `mathlib` is a path dependency, **When** definition is requested on `add` in `mathlib.add(...)`, **Then** the result is `file://${ROOT}/test_build/mathlib/math.b` at `pub fn add`'s location, and not a `.bmod` path.
2. **Given** `aliasapp/main.b` (`import mathlib as m;`) is open, **When** definition is requested on `add` in `m.add(...)`, **Then** the result is the same `mathlib` source location as in scenario 1.
3. **Given** `counterapp/main.b` is open, **When** definition is requested on the type name `Counter` (in a declaration or a construction), **Then** the result is `file://${ROOT}/test_build/counterlib/counter.b` at `pub struct Counter`.
4. **Given** the defining dependency file is **not** open in the editor, **When** any of the above is requested, **Then** the result is that file's on-disk `file://` URI.

---

### User Story 2 - Jump through a value held by use-capability only (Priority: P1)

A developer holds a foreign value that their module never names
(`var b = midx.get_box(7);`, where `Box` is owned by `boxq`, which `usebox`
never imports) and calls a method on it (`b.get()`). Go-to-definition on `get`
opens `boxq`'s source at the method, even though the value reached the consumer
transitively through `midx`'s interface.

**Why this priority**: This is the D7 use-capability case and the hardest
route. The declaration arrives through a generic body that the interface
copied verbatim, two dependency levels away. If this case works, the other
interface-derived cases work too.

**Independent Test**: Open `test_build/usebox/main.b` and request a definition
on `get` in `b.get()`. The golden pins `boxq`'s `.b` source location.

**Acceptance Scenarios**:

1. **Given** `usebox/main.b` is open, **When** definition is requested on `get` in `b.get()`, **Then** the result is `file://${ROOT}/test_build/boxq/boxq.b` at the `get` method's declaration.
2. **Given** the same document, **When** definition is requested on `get_box` in `midx.get_box(7)`, **Then** the result is `midx`'s source file at `pub fn get_box`.

---

### User Story 3 - Jump into the stdlib, the prelude and sibling files (Priority: P1)

A developer jumps from `net.http_ok(...)` (a stdlib function), from a stdlib
type such as `net.HttpServer`, from a prelude type such as `Map` (no import
needed), and from a call into a sibling source file of the same project
(`helpers.triple(14)`) to the declaring source file.

**Why this priority**: These cases cover the stdlib/prelude context from U2 and
the sibling sources from U3. Together with Stories 1 and 2 they make up the
full list in done-condition 4.

**Independent Test**: Transcript fixtures over a stdlib-using document (with
`initializationOptions.stdlibPath` set to the repository's `stdlib/` so the
golden is independent of the build directory), a zero-import prelude document,
and `test_files/lsp/projects/multifile/main.b`.

**Acceptance Scenarios**:

1. **Given** a document that imports `net` is open, **When** definition is requested on `http_ok` in `net.http_ok(...)`, **Then** the result is `file://${ROOT}/stdlib/net.b` at `pub fn http_ok`.
2. **Given** the same document, **When** definition is requested on a stdlib type name (`HttpServer` in `net.HttpServer`), **Then** the result is `stdlib/net.b` at that struct.
3. **Given** a document that uses `Map` with zero imports, **When** definition is requested on `Map`, **Then** the result is `stdlib/collections.b` at `struct Map`.
4. **Given** `projects/multifile/main.b` is open, **When** definition is requested on `triple` in `helpers.triple(14)`, **Then** the result is `projects/multifile/helpers.b` at `pub fn triple`.

---

### User Story 4 - Hover shows the foreign signature with a stable display name (Priority: P1)

For every case in Stories 1–3, hovering shows the **declaration's signature**
in the same one-line markdown code block hover already uses. Each type or
function that belongs to another module is rendered with its deterministic
display name (the rule in FR-008), so the text is the same on every run and
is something the user could type in that module.

**Why this priority**: This is epic done-condition 5. Hover also checks
definition from the user's side: it shows *which* `add` the cursor resolved to.

**Independent Test**: One hover golden per case in Stories 1–3. The rendered
text is checked against the FR-008 rule as written here (workplan U4 done
condition (d)).

**Acceptance Scenarios**:

1. **Given** `myapp/main.b`, **When** hovering `add` in `mathlib.add(3, 4)`, **Then** the hover is `fn mathlib.add(int a, int b) -> int` (the parameter spelling is taken from the declaration).
2. **Given** `aliasapp/main.b` (`import mathlib as m;`), **When** hovering `add` in `m.add(3, 4)`, **Then** the hover is `fn m.add(int a, int b) -> int`: the alias, never `mathlib`.
3. **Given** `counterapp/main.b`, **When** hovering `Counter`, **Then** the hover is `struct counterlib.Counter`.
4. **Given** `usebox/main.b`, **When** hovering `get` in `b.get()`, **Then** the hover is `fn boxq.Box.get(self) -> T`: the owner is shown by the defining module's human name, because `usebox` binds no qualifier to `boxq`.
5. **Given** a zero-import document, **When** hovering `Map`, **Then** the owner renders bare (`struct Map<K, V>`), because prelude and core types are never qualified.
6. **Given** a `net` document, **When** hovering `http_ok`, **Then** the hover is `fn net.http_ok(string body) -> net.HttpResponse`: the foreign types in the signature are `net.`-qualified, while core and prelude types stay bare.
7. **Given** `projects/multifile/main.b`, **When** hovering `triple`, **Then** the hover is `fn helpers.triple(int x) -> int`.

---

### User Story 5 - Same-file navigation is unchanged (Priority: P1)

Every existing same-file definition, hover and document-symbol result is
exactly what it was before U4.

**Why this priority**: This is a hard constraint (workplan U4 done condition
(c), design.md invariants).

**Independent Test**: Every pre-existing `defn_*`, `hover_*` and `symbols_*`
golden passes with zero diffs.

**Acceptance Scenarios**:

1. **Given** the existing `defn_*`/`hover_*`/`symbols_*` fixtures, **When** `./test_lsp.sh` runs, **Then** each matches its golden byte for byte, and no golden file is modified by this unit.

### Edge Cases

- **Generic declarations whose bodies the interface copies verbatim** (`Box<T>`'s methods, a dependency's generic function). The compiler resolves the reference to the copy inside the in-memory interface. The definition MUST still land on the dependency's source declaration.
- **The defining dependency file is open with unsaved edits.** The location comes from the compile that used the open buffer (U3 overlay precedence), so it points at the declaration's position in the **buffer**, under the same `file://` URI.
- **The dependency's own compile failed, or the dependency is unresolved** (missing path, unfetched git dep, cycle). The consumer has no interface for that dependency, so definition and hover on references into it return `null`. They never crash, and they never return a location without a file.
- **The interface declaration cannot be matched to a source declaration** (no source compile, or no matching declaration in it). The result is `null`, never the `.bmod` location (C6).
- **Core/builtin targets** (`int`, `Array`, `Option`/`Result` built-ins, `print`) have no source location. As today, the result is `null`.
- **Cursor on the qualifier token itself** (`mathlib` in `mathlib.add`) or on an `import` line. This unit adds no new behavior there. Whatever the existing locator returns is kept, and it is not pinned by a new golden.
- **A type name written in type position** (`Counter c`, `net.HttpServer server`) navigates when the type is declared in another file. When the type is declared in the document's own file, the result stays what it was before U4 (`null`), as FR-007 requires.
- **A document outside any project, with the stdlib not found.** The document compiles alone (U2), so only same-file navigation applies.
- **Stdlib located through the default `<blangd-dir>/stdlib` copy.** The definition points at that copy, because that is the file the compile actually read. Goldens set `stdlibPath` explicitly so that they are independent of the build directory.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001 (definition, cross-file)**: `textDocument/definition` on a reference whose resolved declaration lives in another file of the document's compile context MUST return a `Location` whose `uri` is that file's `file://` URI and whose range is the declaration's location (zero-length, the same convention as same-file results). The covered reference kinds are: a qualified dependency function call, an aliased-qualifier call, a dependency type named via name-capability, a method called on a foreign value held only via use-capability, a stdlib function, a stdlib type, a prelude type, and a sibling-source function.
- **FR-002 (source, never interface)**: A definition result MUST always be a real `.b` source location: a dependency's source file, a stdlib file, or a sibling project file. It MUST never be a `.bmod` path (generated or in-memory) and never a location with an empty file. When a declaration resolved through a dependency interface (including verbatim-copied generic bodies) cannot be matched to the dependency's source declaration, the result MUST be `null`.
- **FR-003 (matching rule comes from the compile)**: Mapping an interface declaration to its source declaration MUST rely only on the data that the shared project-compile entry point and the kept dependency source compiles already provide: the dependency's module identity, and the declaration's kind, name and owning type. It MUST NOT reimplement any resolution, export or visibility rule. A declaration that is not exported (and so not in the interface) is never reachable this way.
- **FR-004 (not-open files)**: A definition target in a file that is not open in the editor MUST resolve to that file's on-disk `file://` URI. A dedicated golden proves this (workplan U4 (b)).
- **FR-005 (open buffers)**: When the defining file is open, the location MUST reflect the text the compile actually used (the buffer, per U3's overlay), under the same URI the editor uses for that document.
- **FR-006 (hover, cross-file)**: `textDocument/hover` on each FR-001 reference kind MUST return the foreign declaration's signature in the existing hover format (a markdown block holding a single ` ```blang ` line), rendered with display names per FR-008.
- **FR-007 (same-file frozen)**: For a declaration in the viewing document's own module, definition, hover and documentSymbol results MUST be byte-identical to pre-U4 behavior. Every existing `defn_*`, `hover_*` and `symbols_*` golden MUST pass unmodified.
- **FR-008 (deterministic display name; design.md D4 confirmed, with one member extension)**: In a cross-module hover, each named type or free function that belongs to a module other than the viewing module renders as follows:
  1. **Bound module:** `<q>.<name>`, where `<q>` is the qualifier the **viewing module** binds the defining module to. That is its import name, or its alias after `import x as y` (the alias replaces the import name; the original name is not bound, per D8). This applies equally to dependencies, stdlib modules and sibling project sources, because each is reached through an `import` binding.
  2. **Prelude and core types render bare** (`Map`, `Set`, `Buffer`, `int`, `Array`, `Option`), as do a declaration's own generic parameters (`T`).
  3. **Unbound module:** a type the viewing module holds only via use-capability renders as `<m>.<name>`, where `<m>` is the defining module's **human name**: the same module name a `.bmod` `// foreign-type:` header prints (e.g. `boxq`).
  4. **Ties:** when the viewing module binds one module under two qualifiers, the lexicographically smallest qualifier wins.
  5. **Members:** a method or `init` renders as `fn <owner>.<member>(<params>) -> <ret>`, where `<owner>` is the owning type's display name by rules 1–4. This is a display-only composed form, which D3 permits.
  6. **Signature text:** the signature is the declaration's **declared** signature (parameter names and generic parameters as written at the declaration, not substituted type arguments). Every foreign type named within it renders by rules 1–4, relative to the viewing module.
  7. **Existing diagnostics unchanged:** the rule governs hover text only. No `qcc` diagnostic text changes in this unit.
- **FR-009 (no process, no network)**: Definition and hover MUST NOT spawn a process, touch the network, or trigger a recompile or file fetch. They answer from the document's current compile result and its kept dependency compiles. `blangd_no_subprocess` stays green.
- **FR-010 (unresolved/failed context)**: When the referenced module is unresolved, or its source compile failed, definition and hover on a reference into it MUST return `null` without error output. blangd's strict stdio framing is preserved.
- **FR-011 (goldens)**: Each FR-001 case MUST have one definition transcript golden and one hover transcript golden under `test_files/lsp/`. Cases may be grouped as several requests in one fixture, provided every case is pinned. The FR-004 not-open-file golden is also required. Fixtures cite `test_build/`, `stdlib/` and `test_files/lsp/projects/` paths via `${ROOT}`.
- **FR-012 (memory)**: Any structure that navigation adds for mapping interface declarations to source declarations MUST be owned by the document's compile result (KG-7 / C3) and rebuilt with it. `parser_no_leak` stays LSan-clean under `build-asan`.

### Key Entities

- **Viewing module**: the module of the open document whose reference is being navigated. Its import bindings (names and aliases) determine qualifiers.
- **Defining module**: the module whose source declares the target. It has an identity (canonical origin) and a human name.
- **Interface declaration**: the copy of an exported declaration that the consumer's compile resolved against. It is derived in memory from the defining module's source and is never a navigation target.
- **Source declaration**: the declaration in a real `.b` file (dependency source, stdlib file, sibling file) that navigation returns.
- **Display name**: the deterministic rendering of a foreign type/function in hover text (FR-008).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 8 reference kinds in FR-001 return the defining source file's location. There are 8 of 8 definition goldens, plus the not-open-file golden.
- **SC-002**: All 8 reference kinds return a hover whose text follows FR-008 exactly. There are 8 of 8 hover goldens.
- **SC-003**: No new golden's definition result contains a `.bmod` URI. This is checked by inspecting every new golden.
- **SC-004**: All pre-existing LSP transcript goldens pass with zero modified golden files. `git diff --stat` on existing `test_files/lsp/*.expected.out` is empty.
- **SC-005**: Every epic gate stays green: `run_tests.sh` in both build modes, `test_codegen.sh`, `test_lsp.sh` (including the corpus and latency legs, with the latency median still ≤ 500 ms), `ctest` in `build` and `build-asan` (including `parser_no_leak` LSan-clean and `blangd_no_subprocess`), and `test_build/run_build_tests.sh`.
- **SC-006**: The `test_lsp.sh` transcript count only grows.

## Assumptions

- The **case list** is fixed by overview.md done-condition 4 and workplan U4: qualified dependency function, aliased qualifier, name-capability type, use-capability method, stdlib function **and** stdlib type (the workplan lists both), prelude type, and sibling-file symbol. That gives 8 kinds. The suggested fixtures are the existing projects `test_build/myapp`, `aliasapp`, `counterapp` and `usebox`, plus `test_files/lsp/projects/multifile`, and a stdlib document and a zero-import prelude document (new or existing under `test_files/`). The plan chooses the exact cursor positions.
- **Sibling sources are namespaced modules** reached as `helpers.triple` after `import helpers;`, exactly as in `bcc`'s combine plan (per U3's `projects/multifile` fixture). So FR-008 rule 1 applies to them like any other bound module.
- **The display-name rule (overview Open question 1) is settled.** FR-008 is design.md D4, confirmed for types and free functions. It adds two amendments: rule 5 (members render as `<owner>.<member>`) and rule 6 (the declared signature is shown, and foreign types inside it render relative to the viewer). The manager **accepted** the amendment on 2026-11-26; it is recorded as the "D4 addendum" in `docs/epics/modules-v2-lsp/design.md`. D3 permits composed forms as display names, so the amendment stays within the frozen record. It changes no `qcc` diagnostic, so no escalation to the owner was needed.
- **Same-module hover is untouched** (FR-007): today's format is kept exactly, and the display-name rule applies only to declarations of other modules. This is required by the hard constraint that no existing `defn_*`/`hover_*`/`symbols_*` golden changes.
- **Stdlib goldens pin the repository `stdlib/`** by setting `initializationOptions.stdlibPath` to `${ROOT}/stdlib` (the U2 discovery route, design.md D3). Otherwise a golden would embed `build/`, `build-asan/` or `build-nollvm/`.
- **Mapping data is already available**: U3 keeps each dependency's source compile in the compile result ("kept for U4 navigation"). The stdlib, prelude and sibling modules are compiled from their real `.b` files in the same `ProjectCompile::run` context, so their declarations already carry real source locations. Only interface-derived declarations need mapping (per lsp/ProjectContext.h and design.md D2).
- **Hover format** keeps the existing markdown code-block convention (`hover_*` goldens). Only the inner signature text is new.
- **Non-goals** (per overview.md): completion, references, rename, re-diagnosis of dependents, fetching, reading built `.bmod` artifacts, and the module search path. This unit also adds no new behavior for cursors on qualifier tokens or `import` lines.
