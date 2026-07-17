# Epic: feature-integration — Reconcile the devbot epics with origin/master's feature line

**Archetype**: evolve (structural + semantic integration of two diverged histories)

**Status**: planning

**Owner**: Ben Payne

**Created**: 2026-07-16 · **Updated**: 2026-07-16

**Source documents**: this session's reconciliation analysis (2026-07-16);
`git` divergence between local `master` and `origin/master` from common
ancestor `e8c6fc5`.

## Why

Local `master` and `origin/master` diverged into **two independent feature
streams** from common ancestor `e8c6fc5` (ahead 40 / behind 27):

- **Local (40 commits)** — the devbot epics: **blang-ast** (a dedicated `Sema`
  pass, source locations, `DiagnosticEngine`, type checking, match/generics
  soundness, ownership, concurrency) which **split the monolithic
  `CodeGen.cpp` into 7 `CG*.cpp` modules**; and **test-validation** (golden
  tests, real `bcc test`, ASan/fuzz CI, runtime unit tests).
- **origin/master (27 commits, PRs #125–#128)** — a parallel feature line:
  **channel send/recv**, **exhaustive match checking**, **built-in
  `Option`/`Result`**, an **event loop with timers**, **HTTP routing**, and a
  **database layer** (SQLite + libpq driver dispatch, `bcc migrate`, query →
  `Array<T>`, a todo web app). These were added inline to the **still-monolithic
  `CodeGen.cpp`** (+1102/−103 lines).

A naive `git merge` is **broken by construction**: origin's ~1,100 lines of new
codegen land in a `CodeGen.cpp` whose surrounding logic local moved into `CG*`
modules, and the semantic checks local moved into `Sema`. The full merge touches
**65 files / +12,484 lines**, with a genuine **duplicate feature** —
match-exhaustiveness is implemented on *both* sides, differently (origin inline
in `CodeGen.cpp`; local in `Sema`/`CGEnum`). This epic ports origin's features
into local's architecture and reconciles the overlaps, ending with one history
that has both feature sets and both test suites green.

## Done condition (epic level)

On the integration branch, merged to `master` as a single merge commit and
pushed to `origin`, all of the following hold (concrete commands in
`evaluation.md`):

1. **Both build configs clean**: LLVM build, parse-only build
   (`-DBLANG_ENABLE_LLVM=OFF`), and the sanitizer build (`-DBLANG_SANITIZE`)
   all build with no errors.
2. **Both test suites green**: `./run_tests.sh` (LLVM + parse-only) and
   `./test_codegen.sh` (with goldens) pass; `ctest --test-dir build` passes;
   `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0. (The
   test-validation gates survive the merge.)
3. **blang-ast enforcement intact**: `test_files/fail/sema/*.b` (26 fixtures
   incl. `audit_01..10`) still rejected with located `file:line:col: error:`;
   `Sema` still runs in all build modes.
4. **Origin's features work**: channel send/recv, event loop, `Option`/`Result`,
   exhaustive match, HTTP routing, and the database layer each have a passing
   codegen/E2E test (origin's own tests + demos, incl. the todo app, build and
   run); `bcc migrate` functions.
5. **No duplicate/dead code**: match-exhaustiveness has a single implementation
   (reconciled through `Sema`); no orphaned copies of ported functions remain in
   `CodeGen.cpp` vs the `CG*` modules.
6. **History**: `git log` shows one merge commit joining the two lines; `git
   status` reports `master` up to date with `origin/master` (pushed), working
   tree clean.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | The base merge is performed and all mechanical/additive conflicts (`ci.yml`, `CLAUDE.md`, `CMakeLists.txt`, `test_codegen.sh`, additive `CodeGen.h`/`qcc.cpp`/`bcc.cpp` sites, the `Type.h` setter rename) resolved keeping both sides; a compiling baseline exists. | P1 | Done-condition #1 (builds clean) |
| REQ-002 | Origin's **database layer** (SQLGen, SchemaMigration, ProjectConfig, `blang_db`, driver dispatch, `bcc migrate`, query→`Array<T>`) is integrated into local's structure and works. | P1 | Done-condition #4 (DB tests + `bcc migrate`) |
| REQ-003 | Origin's **channel send/recv** codegen + runtime is ported into the `CG*` module structure and routed through `Sema`. | P1 | Done-condition #4 (channel test) |
| REQ-004 | Origin's **event loop + timers** (`on` handler registration, run()/idle-exit) is ported and works. | P1 | Done-condition #4 (event-loop test) |
| REQ-005 | **Built-in `Option`/`Result`** and **exhaustive match** are integrated and **de-duplicated against local's `Sema` match-exhaustiveness** — a single implementation. | P1 | Done-conditions #4, #5 (exhaustive-match test; no dup) |
| REQ-006 | Origin's **HTTP routing** (.get/.post route table) + builtin `to_json` is integrated with local's stdlib/net BLang-native HTTP. | P2 | Done-condition #4 (HTTP routing demo/test) |
| REQ-007 | **CI reconciled**: both epics' CI legs coexist (parse/golden/sanitizer/leak/ctest/fuzz/demos + origin's DB codegen leg); the merged `ci.yml` runs green. | P1 | Done-condition #2 + CI run green |
| REQ-008 | **Full verification + push**: both test suites and all "Verified by" checks pass on the merge commit; `master` is pushed to `origin`. | P1 | Done-condition #6 |

## Non-goals

- **New features** beyond what already exists on either side — this is an
  integration, not a feature epic. Bugs found are fixed; scope is not widened.
- **Rewriting origin's DB/event-loop/channel designs** — port and wire them
  into local's architecture; don't redesign them.
- **Reworking the `CG*` split or `Sema`** — local's architecture is the target
  end state; origin's features move into it, not vice versa.

## Companion documents

| File | Purpose |
|------|---------|
| `workplan.md` | 8 units, dependency map, per-unit done conditions |
| `design.md` | integration strategy, the architecture decision, conflict map |
| `evaluation.md` | harness commands (both suites + origin demos), audit rubric |
| `manifest.yaml` | machine-readable run definition |

## Constraints & context for the manager

- **Target architecture is LOCAL's** (`CG*` modules + `Sema` + `DiagnosticEngine`).
  Origin's features are *ported into* it; do not resurrect the monolithic
  `CodeGen.cpp`.
- **Do the merge with a merge commit** (not rebase) so both histories and the
  full devbot audit trail are preserved.
- **Match-exhaustiveness is duplicated** — keep local's `Sema` version, port any
  cases origin's version covered that local's doesn't, delete origin's inline copy.
- **Both prior epics are verified/complete** — their gates (sema fixtures,
  goldens, leak-check, fuzz) must stay green; a regression in them is a merge bug.
- Work on `epic/feature-integration/uN-<slug>` branches off an integration base
  branch; the final result merges to `master` and pushes to `origin`.
- Constitution applies: `.specify/memory/constitution.md`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| — | (none yet — the match-exhaustiveness reconciliation is design guidance in design.md, decided at implement time) | | | |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-07-16 | — | epic created | Reconciliation of local (devbot epics) vs origin (feature line) found to be a structural+semantic re-integration; scoped as an epic. Ancestor e8c6fc5; local ahead 40 / behind 27; merge = 65 files / +12k lines. |
