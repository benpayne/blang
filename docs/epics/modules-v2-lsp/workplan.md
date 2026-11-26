# Workplan: modules-v2-lsp

**Epic**: [overview.md](overview.md) · **Archetype**: evolve

Every unit's gates (in addition to its own done condition): `./run_tests.sh`
green in both build modes, `./test_codegen.sh` green, `./test_lsp.sh` green,
`ctest --test-dir build` and `ctest --test-dir build-asan` green,
`test_build/run_build_tests.sh` green; units touching ownership of
compile-context structures also keep `parser_no_leak` LSan-clean under
`build-asan`. Baseline numbers are in [evaluation.md](evaluation.md).

## Unit map

```text
U1 ─▶ U2 ─▶ U3 ─▶ U4 ─▶ U5
```

Strictly sequential. U1–U4 are dev units on one stacked branch chain (Spec Kit
allows one active feature at a time); U5 is an exempt close-out.

- **U1** extracts the shared project-compile entry point — everything else
  consumes it.
- **U2** gives blangd the stdlib/prelude context: the largest share of today's
  false diagnostics, and the simplest context to prove the pipeline.
- **U3** adds `blang.toml` projects and dependencies.
- **U4** builds the navigation features on the full context.
- **U5** closes out docs, the real-client smoke and the epic evidence.

## Units

### U1 — Shared project-compile entry point (behavior-neutral extraction)

- **Covers**: REQ-001
- **Preconditions**: PR #157 merged to `master` and this epic branch based on it
  (see overview "Launch precondition").
- **Work**: Move the combine-mode compile that `qcc.cpp` `main()` performs
  inline into shared frontend code (in `BLANG_FRONTEND_SOURCES`, so blangd links
  it with no LLVM). That covers per-file scope routing (user scope,
  prelude-module holding scope + PROMOTE, namespaced stdlib modules),
  `.bmod` injection, origin and digest stamping, the whole-program
  import-diagnostics pass, and Sema. The entry point takes an explicit, ordered
  description of the compile: which inputs, in what roles, with which origins.
  It returns owned modules, scopes and diagnostics. Also move `bcc`'s stdlib-set
  selection (`resolveStdlibFiles`: the unconditional
  `sys/buffer/collections/fs/net` base plus the import-gated extras) into shared
  code, so `bcc` and blangd choose the same stdlib files. `qcc` becomes a thin
  caller; `lsp/Compile.cpp` calls the same entry point, still in single-file
  mode for this unit so blangd's observable behavior does not change yet.
  This is a refactor: no golden, diagnostic text or exit code changes.
- **Done condition**: (a) `grep` shows the shared entry point called from both
  `qcc.cpp` and `lsp/Compile.cpp`, and `qcc.cpp` no longer contains the
  per-file combine-routing branch (the `isUserFile` / `isPreludeModule` scope
  selection) or its own copy of the import-diagnostics pass; (b) `bcc.cpp`
  selects stdlib files through the shared function (no local
  `resolveStdlibFiles` body); (c) a new `ctest` (`project_compile`) drives the
  entry point directly and asserts, for a combined stdlib + user fixture: the diagnostic text equals
  `qcc --combine` on the same inputs; `Map`/`Set` resolve through the prelude
  scope; `net` is registered as a namespace scope; and a user `struct Map`
  shadows the prelude `Map`; (d) all unit gates green with **zero golden edits** across
  `test_files/` and `test_files/lsp/`; (e) `parser_no_leak` still LSan-clean;
  (f) converge gate: `/speckit-converge` reports `converged` for
  `specs/041-shared-project-compile`, or two converge→implement cycles have run
  and only LOW/MEDIUM `unrequested` findings remain.
- **Audit**: design audit before implementation (Principle VI: it restructures
  the driver's core). Code review (five dimensions); maintainability is the
  focus, since this is the seam every later unit builds on.
- **Budget hint**: ~40 hire turns
- **Team hint**: architect (design audit) + implementer + independent reviewer
- **Speckit**: `041-shared-project-compile`

### U2 — blangd stdlib/prelude context + stdlib discovery

- **Covers**: REQ-002, REQ-003, REQ-009 (single-file half)
- **Depends on**: U1
- **Work**: Make blangd compile every document with the stdlib set and prelude
  that `bcc file.b` uses, through the U1 entry point: the program's imports
  pick the import-gated extras, the base set and the prelude are always loaded.
  Discover the stdlib directory in a fixed precedence order:
  `initializationOptions.stdlibPath` (from `initialize`), then
  `BLANG_STDLIB_DIR`, then `<blangd-executable-dir>/stdlib`. When none exists,
  publish a single clear diagnostic and fall back to today's single-file
  behavior. Diagnostics from stdlib files themselves are never published against
  the user's document. Keep compile-context ownership per the KG-7 rule. Caching
  the parsed stdlib between reparses is allowed only if it keeps ownership
  explicit and `parser_no_leak` clean; the default is a full recompile.
  Introduce the corpus harness `tools/lsp_diag_corpus.py`: it opens each corpus
  file through blangd, runs the same compile through `qcc`, compares the
  diagnostics, and is wired into `test_lsp.sh`. Also introduce
  `tools/lsp_latency.py`.
- **Done condition**: (a) `tools/lsp_diag_corpus.py --corpus stdlib` passes over:
  `test_files/codegen_prelude_no_import.b`, `test_files/codegen_http_routing.b`,
  `examples/kv/main.b`, `examples/todo_app/main.b`, every `examples/*/main.b`, and
  every `test_files/codegen_*.b` whose reference compile (the `qcc` command
  `bcc -v -S <file>` prints) exits 0. blangd's published diagnostics equal the
  reference compile's — errors and warnings, same severity/line/column/message. The corpus list is a committed file and must include at least those
  named; (b) transcript goldens under `test_files/lsp/` for each stdlib
  discovery route (initializationOptions, env var, blangd-relative) and for the
  stdlib-missing fallback diagnostic; (c) `tools/lsp_latency.py` median ≤ 500 ms on `examples/todo_app/main.b`,
  measured with the non-sanitizer `build/` blangd (`test_lsp.sh` skips the latency
  leg with a printed notice when `BUILD_DIR` is a sanitizer build); (d) `parser_no_leak` extended with a
  stdlib-context `compileDocument` loop (≥ 50 reparses) and LSan-clean; (e) every
  pre-existing `test_lsp.sh` golden still passes or carries a per-file
  justification in the PR (a single-file fixture that newly gets the prelude
  may legitimately change); (f) converge gate for `specs/042-lsp-stdlib-context`.
- **Audit**: code review; security review (stdlib path from untrusted
  `initializationOptions`/env: path handling only, no execution).
- **Budget hint**: ~40 hire turns
- **Team hint**: implementer + independent reviewer
- **Speckit**: `042-lsp-stdlib-context`

### U3 — blangd project context: blang.toml, sibling sources, path/git deps from source

- **Covers**: REQ-004, REQ-005, REQ-006, REQ-009 (project half)
- **Depends on**: U2
- **Work**: Find the document's `blang.toml` by walking up from the file. The
  compile context becomes the project's own `.b` sources (as `bcc build`'s
  `discoverSourceFiles` selects them) plus its `[deps]`, resolved **from
  source**: path deps relative to their `blang.toml`, git deps only when already
  checked out in `~/.cache/blang/git/<name>-<hash(url@pin)>`. Resolution is
  recursive and transitive in topological order, as `bcc`'s `buildProject`
  does, with each dependency stamped with the same canonical origin `bcc`
  passes (`--module-origin` / `--bmod-origin`). Mangling identities, D7
  capabilities, privacy, aliasing and collision diagnostics therefore match
  `bcc build` exactly. Each dependency's **interface** must be exactly what its
  `.bmod` would expose (`pub` only, opaque fields, D15 data-contract metadata);
  see design.md D2 for the recommended way to guarantee that. Never spawn a
  process or touch the network. An unresolvable dependency or malformed
  `blang.toml` becomes one located diagnostic. When another file of the context
  is open in the editor, its in-memory text is used instead of the disk copy.
- **Done condition**: (a) `tools/lsp_diag_corpus.py --corpus project` passes over
  every `test_build/*` **binary** project that `run_build_tests.sh` builds
  successfully (at least `myapp`, `counterapp`, `usebox`, `boxapp`, `aliasapp`,
  `diamondapp`, `todoapp`, `printapp`, `sizeapp`, `timerapp`): blangd's
  diagnostics on each `main.b` equal the project's reference compile (the final
  `qcc` command `bcc build -v` prints; errors and warnings). It also passes over every `test_files/fail/xmodule/*`
  fixture: blangd's diagnostic matches `qcc`'s (line, column, message) when the
  fixture's `lib.b` is resolved as a project dependency. These fixtures are bare
  `lib.b` + `consumer.b` pairs, so the harness synthesizes a temporary
  `blang.toml` pair (lib + bin with a path dep) in a scratch directory and
  compares against `qcc consumer.b lib.bmod`, as `run_tests.sh` does today; (b) negative transcript
  goldens for a missing path dep, an unfetched git dep and a malformed
  `blang.toml` — one located diagnostic each, no crash; (c) a test runs blangd
  with a `PATH` containing a `git` shim that writes a marker file, opens a
  project with an unfetched git dep, and fails if the marker exists; (d) a
  transcript golden where an unsaved edit to an open dependency file changes the
  consumer's diagnostics; (e) `tools/lsp_latency.py` median ≤ 500 ms on
  `test_build/usebox/main.b`; (f) `parser_no_leak` extended with a project-context
  reparse loop (≥ 50 reparses) and LSan-clean; (g) converge gate for
  `specs/043-lsp-project-context`.
- **Audit**: design audit before implementation (interface-equivalence approach,
  D2); security review (untrusted toml/paths, no-subprocess guarantee, Quality
  Gate 7); code review.
- **Budget hint**: ~60 hire turns (largest unit)
- **Team hint**: architect + implementer + independent reviewer
- **Speckit**: `043-lsp-project-context`

### U4 — Cross-module go-to-definition and hover

- **Covers**: REQ-007, REQ-008
- **Depends on**: U3
- **Work**: Extend `textDocument/definition` and `textDocument/hover` (today
  `AstLocator` + `Server.cpp`, same-file only) to resolve across the compile
  context. A definition in another file returns that file's `file://` URI and
  the definition's location: a dependency's source file, a stdlib file, or a
  sibling project file, never a `.bmod`. Covered cases:
  - a qualified dependency function;
  - an aliased qualifier;
  - a name-capability type;
  - a method called on a value held only via use-capability;
  - a stdlib function and type;
  - a prelude type;
  - a sibling-file symbol.

  Hover renders the foreign declaration's signature with a deterministic D3
  display name (design.md D4; confirm or amend the rule in the spec, per
  overview Open question 1). Same-file behavior and its existing goldens must
  not change.
- **Done condition**: (a) one transcript golden per case listed above for
  definition, and one per case for hover, under `test_files/lsp/` (fixtures may
  cite `test_build/` and `stdlib/` via `${ROOT}`); (b) a golden proving that a
  definition target in a file that is not open resolves to its on-disk URI; (c)
  every pre-existing `defn_*`/`hover_*`/`symbols_*` golden unchanged; (d) the D3
  rule used is written in `specs/044-lsp-xmodule-navigation/spec.md` and matches
  the rendered hover text; (e) converge gate for
  `specs/044-lsp-xmodule-navigation`.
- **Audit**: code review; spec audit checks the D3 rule against the design
  record.
- **Budget hint**: ~40 hire turns
- **Team hint**: implementer + independent reviewer
- **Speckit**: `044-lsp-xmodule-navigation`

### U5 — Close-out: real-client smoke, docs, epic evidence

- **Covers**: REQ-010, REQ-011
- **Depends on**: U4
- **Work**: Extend `ci/nvim_smoke.lua` (and `test_lsp_nvim.sh` if needed) to open
  a project fixture with a dependency, asserting zero error diagnostics and a
  cross-module definition result. It stays a yellow SKIP without `nvim`.
  Update `CLAUDE.md` (the `blangd`/`lsp/` repository-structure entries, the
  `blangd` build-target row, the test-count lines, and Known Issues text
  describing blangd as single-file) and `editors/README.md` (stdlib-path
  configuration and project discovery). Confirm the `lsp` CI job runs the corpus
  and latency harnesses, then assemble the epic's functional-review evidence.
- **Done condition**: (a) `./test_lsp_nvim.sh` passes on a machine with `nvim`
  (or the manager records that CI's `lsp` job ran it green); (b) `grep -n "blangd" CLAUDE.md | grep -i "single-file"` prints nothing, and
  `editors/README.md` contains each of `stdlibPath`, `BLANG_STDLIB_DIR` and
  `blang.toml` (documenting stdlib discovery and project discovery); (c) the epic branch's CI is green on every job; (d)
  every bullet of overview.md's done condition is mapped to the committed test
  that proves it, in this unit's PR body.
- **Audit**: functional review (epic completion, per evaluation.md).
- **Budget hint**: ~15 hire turns
- **Team hint**: single implementer + reviewer
- **Speckit**: none — exempt (docs + CI smoke + close-out evidence; no shipped
  compiler/server behavior)

## Sequencing notes for the manager

- **Do not start U1 without PR #157 in the base** (overview "Launch
  precondition"). If the epic branch's base lacks commit `efb099a` (the KG-7
  scope-ownership fix), stop and raise a question.
- **U1 must be behavior-neutral.** If extracting the combine routing forces a
  golden or diagnostic change, stop and raise a question rather than accepting
  it. The point of U1 is that nothing observable moves.
- **U2 may legitimately change existing single-file LSP goldens**: a fixture
  that used a prelude type now resolves it. Each such diff must be justified
  individually in the PR. A change to a diagnostic's *text* (not its presence)
  is a red flag; raise it.
- **U3's interface-equivalence approach is the main design risk** (design.md
  D2). The architect settles it in the design audit before implementation; if
  the recommended approach can't reach exact `.bmod` parity, raise a question
  before building an alternative.
- If REQ-009's 500 ms median fails after U3, raise a question before adding
  caching. Caching introduces ownership state across reparses (the KG-7 class
  of bug), so it needs an explicit design.
- Stop and raise a question if any unit appears to need a change to `.bmod`
  format, `BuildCache` keys, D1–D17, or `qcc`/`bcc` user-visible behavior.
