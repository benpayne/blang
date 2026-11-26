# Epic: modules-v2-lsp — blangd resolves modules (Epic C of modules-v2)

**Archetype**: evolve (reworks the existing `blangd` compile pipeline on top of
the Resolver seam Epic B extracted; `qcc`/`bcc` behavior must not change)

**Status**: ready

**Owner**: Ben Payne

**Created**: 2026-11-26 · **Updated**: 2026-11-26

**Source documents**:
- Design record [`docs/epics/modules-v2/overview.md`](../modules-v2/overview.md)
  — pain point **P4** ("The LSP is single-file"), goal "a shared resolution
  service both `qcc` and `blangd` use", original unit 11, and the decision
  record **D1–D17**, which binds this epic exactly as it bound Epics A and B.
- Epic B [`docs/epics/modules-v2-graph/overview.md`](../modules-v2-graph/overview.md)
  — delivered the extractable `Resolver` (`Resolver.h/.cpp`, REQ-007/DC5) that
  `lsp/Compile.cpp` already constructs "present-but-dormant until Epic C".
- [`docs/epics/modules-v2-graph/known-issues.md`](../modules-v2-graph/known-issues.md)
  — KG-7 (closed by PR #157: scope ownership is now top-down, module scopes are
  owned by the caller — blangd's `CompileResult` included).

## Why

`blangd` compiles every open document **alone**, exactly like `qcc file.b`. The
module system Epics A and B built (enforced imports, qualified access, the
prelude, `.bmod` dependencies, D7 capabilities) only exists in `bcc`'s combined
compile, so the editor sees none of it. The result is worse than "no
cross-module features": blangd reports **false errors on essentially every real
program**. Measured on `master` @ `7a68fcd` with the single-file pipeline blangd
uses (`qcc --parse-only <file>`):

| File | False errors | First one |
|------|-------------:|-----------|
| `test_files/codegen_prelude_no_import.b` (zero imports, uses `Map`) | 14 | `unknown type 'Map'` |
| `test_build/myapp/main.b` (path dep `mathlib`) | 14 | `undefined symbol 'mathlib'` |
| `test_files/codegen_http_routing.b` (`import net;`) | 16 | `Expected type argument` |
| `test_build/counterapp/main.b` (`Counter` from a dep) | 7 | `unknown type 'Counter'` |
| `examples/kv/main.b` (`import fs;`, `cli`) | 7 | `undefined symbol 'fs'` |
| `examples/todo_app/main.b` | 4 | `expected ',' or ')'` |

Every one of these compiles clean through `bcc`. An editor that flags correct
code as broken is worse than none: users learn to ignore its diagnostics, which
throws away the located-diagnostic work of the last three epics.

This epic makes blangd compile a document **in the same context `bcc` would**:
the stdlib and prelude, the project's `blang.toml`, its other sources, and its
path/git dependencies, all through **one shared project-compile entry point**
that `qcc` also uses, so the editor and the compiler cannot disagree. On top of
that context it delivers the design record's three cross-module features:
**diagnostics, go-to-definition, and hover** across module boundaries.

## Done condition (epic level)

All of the following hold on the epic branch `epic/modules-v2-lsp` after its spec
PRs merge, each backed by a committed test that CI runs (the `lsp` job, plus the
existing jobs for regression):

1. **One shared project-compile entry point.** The combine-mode routing that
   today lives inline in `qcc.cpp` `main()` (stdlib/prelude LOAD-vs-PROMOTE,
   namespaced-module scopes, user scope, origin + digest stamping, `.bmod`
   injection, the whole-program import-diagnostics pass, Sema) and `bcc`'s stdlib
   set selection (`resolveStdlibFiles`) live in shared frontend code that is
   called from **both** `qcc.cpp` and `lsp/Compile.cpp` (grep-verifiable), and a
   `ctest` drives it independent of either driver. `qcc`/`bcc` behavior is
   unchanged: every pre-existing gate (`run_tests.sh` both build modes,
   `test_codegen.sh` + `--leak-check`, `test_build/run_build_tests.sh`,
   `ctest`, existing `test_lsp.sh` goldens) stays green with no golden edits
   other than ones the PR explicitly justifies.
2. **No false diagnostics in stdlib/prelude context.** With no `blang.toml`
   (single-file), blangd compiles an open document with the same stdlib set and
   prelude `bcc file.b` uses. A committed corpus check
   (`tools/lsp_diag_corpus.py`, run by `test_lsp.sh`) opens every file in its
   listed corpus — at minimum the six files in this overview's table, every
   `examples/*/main.b`, and every `test_files/codegen_*.b` whose reference compile exits 0 — and asserts blangd
   publishes **exactly the diagnostics of the reference compile**. The **reference compile** for a single file is the `qcc` command line that
   `bcc -v -S <file>` prints; for a project it is the final `qcc` command
   `bcc build -v` prints. "Same diagnostics" means the same errors **and
   warnings** (severity, line, column, message); a file is *clean* when its
   reference compile exits 0. The stdlib
   location is discoverable (blangd-relative `stdlib/`, an LSP
   `initializationOptions.stdlibPath`, and `BLANG_STDLIB_DIR`), each covered by
   a transcript golden.
3. **No false diagnostics in project context.** blangd finds the document's
   `blang.toml` (walking up from the file), includes the project's other `.b`
   sources, and resolves `[deps]` path dependencies and **already-fetched** git
   dependencies **from source**, transitively, with the same module identities
   and D7 use/name capabilities `bcc build` applies. The corpus check covers
   every `test_build/*` binary project that `run_build_tests.sh` builds
   successfully (blangd's diagnostics on its `main.b` equal its reference compile's) and every `test_files/fail/xmodule/*` fixture
   (blangd reports the same located diagnostic `qcc` does: same line, column and
   message). An unresolvable dependency (missing path, unfetched git dep,
   malformed `blang.toml`) yields **one located diagnostic** at the import
   statement or toml, never a crash, and blangd **never spawns a process or
   touches the network** (a test runs blangd with a `PATH` shim that fails the
   test if `git` is invoked). An open document's **unsaved buffer** takes
   precedence over the on-disk copy wherever that file is part of another
   document's context (transcript golden).
4. **Cross-module go-to-definition.** `textDocument/definition` returns a
   location in the **defining file** (a `file://` URI that need not be open) for:
   a qualified dependency function (`mathlib.add`), an aliased one
   (`import mathlib as m; m.add`), a dependency type named via name-capability
   (`Counter`), a method called on a foreign value held via use-capability only
   (`var b = midx.get_box(7); b.get()` → `boxq`'s source), a stdlib symbol
   (`net.http_ok` → `stdlib/net.b`), a prelude type (`Map` →
   `stdlib/collections.b`), and a symbol from a sibling source file of the same
   project. Each case is a transcript golden under `test_files/lsp/`; a
   definition never points into a generated `.bmod`.
5. **Cross-module hover.** `textDocument/hover` on each of the done-condition 4
   cases returns the foreign declaration's signature with a **deterministic
   display name** for the type/module (the D3 rule stated in `design.md` D4),
   pinned by transcript goldens.
6. **Latency floor.** `tools/lsp_latency.py` (committed, run by `test_lsp.sh`)
   drives 20 `didChange` edits on `examples/todo_app/main.b` and on
   `test_build/usebox/main.b` and asserts the **median** `didChange` →
   `publishDiagnostics` time is ≤ 500 ms each.
7. **Real-client smoke.** `ci/nvim_smoke.lua` additionally opens a project
   fixture with a dependency and asserts zero error diagnostics and a
   cross-module definition result (still a yellow SKIP when `nvim` is absent).
8. **Docs.** `grep -n "blangd" CLAUDE.md | grep -i "single-file"` prints nothing
   (no line still describes blangd as single-file), and `grep -c` finds each of
   `stdlibPath`, `BLANG_STDLIB_DIR` and `blang.toml` in `editors/README.md`.

and every unit's done condition in workplan.md is met.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | A single shared project-compile entry point (combine routing + stdlib-set selection) is called by both `qcc` and `blangd`; `qcc`/`bcc` behavior unchanged | P1 | Done cond. 1 — grep of both call sites, new `ctest`, all prior gates green |
| REQ-002 | blangd compiles a single-file document with `bcc`'s stdlib set + prelude; no false diagnostics on the stdlib/prelude corpus | P1 | Done cond. 2 — `tools/lsp_diag_corpus.py` |
| REQ-003 | The stdlib directory is discoverable (blangd-relative, `initializationOptions.stdlibPath`, `BLANG_STDLIB_DIR`) with a located/logged fallback when absent | P2 | Done cond. 2 — transcript goldens per discovery route |
| REQ-004 | blangd discovers `blang.toml`, includes sibling project sources, and resolves path + fetched git deps from source transitively with `bcc`'s identities and D7 capabilities | P1 | Done cond. 3 — corpus over `test_build/*` bins and `fail/xmodule/*` |
| REQ-005 | Unresolvable dependencies and malformed `blang.toml` produce one located diagnostic, never a crash; blangd never spawns a process or touches the network | P1 | Done cond. 3 — negative transcripts + `PATH` git shim test |
| REQ-006 | An open document's unsaved text overrides the on-disk file wherever it is part of another document's compile context | P2 | Done cond. 3 — transcript golden |
| REQ-007 | Cross-module go-to-definition into the defining source file for qualified, aliased, name-capability, use-capability, stdlib, prelude and sibling-file symbols; never into a `.bmod` | P1 | Done cond. 4 — transcript goldens |
| REQ-008 | Cross-module hover renders the foreign signature with a deterministic D3 display name | P1 | Done cond. 5 — transcript goldens |
| REQ-009 | Median `didChange`→`publishDiagnostics` ≤ 500 ms on the two named files | P2 | Done cond. 6 — `tools/lsp_latency.py` |
| REQ-010 | The headless-Neovim smoke covers a project with a dependency | P3 | Done cond. 7 — `ci/nvim_smoke.lua` |
| REQ-011 | `CLAUDE.md` and `editors/README.md` document project-context compilation and stdlib discovery | P2 | Done cond. 8 — docs grep check (evaluation.md) |

## Non-goals

- **LSP completion, find-references, rename, semantic tokens, formatting, code
  actions** — out of scope (completion is an explicit non-goal of the design
  record; the others were declined for this epic).
- **Re-diagnosing dependents on change.** When a sibling or dependency file
  changes, open documents that import it are not proactively re-published; they
  pick up the change on their own next `didOpen`/`didChange` (REQ-006 covers
  unsaved buffers that are *in* a compile context). No `didSave` /
  `workspace/didChangeWatchedFiles` fan-out.
- **A qcc/blangd parity CI gate over every fixture.** Parity is verified over the
  named corpus in done conditions 2–3, not as a blanket gate on all of
  `test_files/`.
- **Fetching dependencies.** blangd never clones a git dependency or runs any
  subprocess; an unfetched git dep is a located diagnostic telling the user to
  run `bcc build`.
- **Reading built `.bmod` artifacts.** Dependencies are resolved from source
  (owner decision); the build cache is not consulted.
- **Changes to language semantics, the module model (D1–D17), `.bmod` format, or
  `qcc`/`bcc` user-visible behavior.** The module search path (KG-10/DC11) stays
  its own follow-on epic.
- **Multi-root workspaces and incremental (sub-file) reparsing.** One project
  context per document; a full context recompile per edit is acceptable within
  REQ-009's budget (measured: a full stdlib + program parse + Sema is ~0.09 s
  for the whole `qcc` process).

## Companion documents

| File | Purpose |
|------|---------|
| [workplan.md](workplan.md) | units U1–U5, dependencies, per-unit done conditions, budgets |
| [design.md](design.md) | recon context, architecture, contracts, key decisions, invariants |
| [evaluation.md](evaluation.md) | harness commands, audit plan, regression baseline, evidence |
| manifest.yaml | machine-readable run definition |

## Constraints & context for the manager

- **Launch precondition — base on PR #157.** PR #157
  (`wip-coercion-hash-and-open-bugs`) changes scope ownership that blangd relies
  on (KG-7: module scopes are owned by the caller; `Scope::mParent` is
  non-owning) and fixes ARC/identity bugs this epic's fixtures exercise. The
  epic branch was planned off `master` @ `7a68fcd` with docs only; **before
  launch, #157 must be merged to `master` and this branch's doc commits replayed
  onto the new `master`** (no spec branches exist yet, so this is safe). Do not
  start U1 on a base without #157.
- **The editor and the compiler must not disagree.** Any rule blangd applies must
  come from the shared entry point (REQ-001), never a blangd-only
  reimplementation. If a unit finds it needs one, stop and raise a question.
- **`qcc.cpp` `main()` is large and load-bearing** (combine routing, prelude
  LOAD/PROMOTE, the import-diagnostics pass, origin/digest stamping). U1 is a
  behavior-neutral extraction: no golden changes, no diagnostic text changes.
- **blangd's process model.** It is long-lived; a single `static Resolver`
  owns the global builtin scope for the process, and each `CompileResult` owns
  its module scope(s) and AST (KG-7). Every new context structure (stdlib and
  dependency modules) must follow the same ownership rule: owned by the
  `CompileResult` (or an explicit cache it owns) and released before the
  Resolver. Leaks here grow without bound across reparses. The `parser_no_leak`
  ctest under `build-asan` is the guard; extend it for the project path.
- **Untrusted input.** `blang.toml`, dependency sources and paths in a workspace
  are untrusted (constitution Quality Gate 7): no subprocess, no network, no
  path escape surprises (a dep path is resolved relative to its `blang.toml`
  exactly as `bcc` does), malformed input → located diagnostic.
- **The transcript harness is strict.** `tools/lsp_client.py` fails on any stray
  stdout/stderr byte from blangd; `${ROOT}` scrubbing means goldens can cite
  files outside `test_files/lsp/` (e.g. `test_build/`, `stdlib/`). Regenerate
  goldens only with `./test_lsp.sh --update-goldens` and justify every golden
  diff in the PR.
- **Never touch**: the `.bmod` format version, `BuildCache` keys, the D1–D17
  decision record, the quarantine list
  (`docs/epics/test-validation/approved_quarantine.txt`).
- Constitution applies: `.specify/memory/constitution.md` (v1.2.0) — audit
  pattern, Quality Gates 1–7, Principle VI design audit before implementation.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| 1 | The D3 display-name tie-break rule was never written down in Epic B (its OQ#1 stayed "open"; spec 039 references it without stating it). design.md D4 proposes a rule. Confirm, or the U4 architect proposes an alternative in its spec. | U4 | open — architect confirms or amends D4 in U4's speckit spec; manager approves against D3; raise to owner only if it would change a `qcc` diagnostic | |
| 2 | Default stdlib location for an *installed* blangd (not run from `build/`): is `<blangd-dir>/stdlib` enough, or is a `<prefix>/share/blang/stdlib` fallback wanted? | U2 | open — default: blangd-relative + `initializationOptions.stdlibPath` + `BLANG_STDLIB_DIR`, no share-dir fallback | |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-11-26 | — | epic planned | Epic C of the modules-v2 split. Owner decisions: evolve; deps resolved from source (not `.bmod`); scope = design record only (diagnostics/definition/hover across modules; no extras); wait for PR #157 then launch fully autonomous with generous limits. Recon on `master` @ `7a68fcd`. Next: `/devbot-review modules-v2-lsp`. |
| 2026-11-26 | — | review passed | `/devbot-review`: all 12 checks pass after fixes F1–F4 (reference compile defined as the `qcc` line `bcc -v -S` / `bcc build -v` prints, errors+warnings compared; docs done-condition made a grep check; `project_compile` ctest assertions spelled out; latency measured on non-sanitizer `build/` only). Launch still waits on PR #157 merging + rebasing this branch onto the new `master`. Next: `/devbot-launch modules-v2-lsp`. |
