# Spec: CI + known-issues consolidation + close-out

**Epic**: functional-hardening · **Unit**: U5 · **Branch**: `epic/functional-hardening/u5-closeout`
**Covers**: REQ-006 (CI green on the epic's final commit; known-issues consolidation; close-out)
**Speckit**: `ci-closeout` · **Status**: Draft (awaiting spec audit)
**Depends on**: U1, U2, U3, U4 (all merged to `master` @ ab142ec)

## Problem

U1–U4 authored the behavioral matrices and fixed both seeded bugs. The codegen
count is 107 (≥ 105). The epic-scoped gates are green: `run_tests.sh` 195/0
(LLVM), 190/0 (parse-only), `test_codegen.sh` 107/0, `ctest` 54/54, and the ARC
glob (`codegen_arc_*.b`) + `codegen_struct_field_reassign.b` +
`codegen_bcc_collections_map.b` + `codegen_ix_*.b` leak-check is 0 leaks.

But the **`sanitizers` CI job runs the FULL-SUITE `--leak-check`** (`./test_codegen.sh
--leak-check | grep 'Leaks: 0'`), and it has been **RED on master since U2
(c2eb656) through ab142ec**. Root cause (diagnosed on this branch):

> `codegen_db_query.b` leaks **276 bytes in 8 allocations**. The program runs
> `query User |> where { .active == active_flag };` **without binding the
> result**. `CodeGen::genQueryExpression` (CGRuntime.cpp) heap-allocates a fresh
> `Array<User>` (via `__blang_array_create`) whose element destructor releases
> each mapped row struct, then **returns it without registering it as a
> statement temporary**. Every other refcounted rvalue (`fn().method()` struct,
> `arr.pop()`, `Buffer.get_bytes()`) is tracked via `trackTempArray` /
> `releaseTempArrays()` at statement end; the query path was the one gap. A
> discarded query result is therefore never released.

This is a **pre-existing** leak introduced by feature-integration commit fd64714
(the DB result-mapping layer), NOT by any epic unit, and it is NOT a
string-returning-method issue. It does NOT affect the epic-scoped ARC gate
(`codegen_arc_*.b` is clean), but it DOES fail done-condition #6 (CI green on the
final commit) because the sanitizers job leak-checks the whole suite.

Under the fix-or-file policy this is a **bounded, low-risk fix** (one-line-class:
mirror the existing temp-array tracking already used everywhere else), so it is
FIXED, not filed and not quarantined.

## Scope

**In scope**
1. **Fix the `codegen_db_query.b` full-suite leak.** In
   `CodeGen::genQueryExpression` (CGRuntime.cpp), call `trackTempArray(arr)`
   before each `return arr;` (both the unknown-table early return and the normal
   return). This makes a discarded `query T;` result a statement temporary
   released at statement end; consumers that keep the array (var-decl init,
   assignment, return) already `untrackTempArray` it, so bound results are
   unaffected. `genInsertExpression`/`update`/`delete` return `int` counts, not
   arrays — no change needed there.
2. **Consolidate `known-issues.md`.** Create
   `docs/epics/functional-hardening/known-issues.md`. As of U5 there are **0**
   deferred matrix bugs (every authored test passes; both seeded bugs fixed), so
   the file records that explicitly and holds **0** `### KI-N` entries
   (`grep -c '^### KI-' ≤ 3` trivially). The two seeded bugs (S1 struct-field
   reassign, S2 `Map` via `bcc`) never appear here.
3. **Confirm CI auto-globs the new tests and the sanitizers job goes green.**
   `test_codegen.sh` with no args globs `test_files/codegen_*.b`; the golden
   floor check counts `codegen_*.expected.out`; the quarantine list is unchanged
   (== approved). Verify the `--leak-check` clean-suite step passes with the fix.
4. **Update `CLAUDE.md`** codegen test count (85 → 107) and the functional-
   coverage / epic status note; ensure no seeded bug remains listed as an open
   issue.
5. **Run the full epic-level acceptance block** from `evaluation.md` on a clean
   build and confirm every clause passes.

**Out of scope**
- Widening the golden/leak quarantine (an epic-scope decision, not a hire call).
- Any change to the DB result-mapping semantics beyond ownership tracking of the
  discarded rvalue.
- Authoring new matrix tests (that was U1–U4). U5 is consolidation + close-out.

## Requirements traceability

| REQ | Covered by |
|-----|-----------|
| REQ-006 (CI green on final commit; ≥ 105 codegen; ARC glob leak-clean; KI ≤ 3; both suites both modes green) | tasks 1–5 above; verified by `evaluation.md` acceptance block |

## Test plan / done condition

- **The leak fix has teeth:** `./test_codegen.sh --leak-check
  test_files/codegen_db_query.b` reports `CLEAN` / `Leaks: 0` (was `LEAK … 276
  byte(s)`); the discarded-query golden stdout is unchanged (the result was
  already discarded, so behavior is identical — only the release is added).
- **Full-suite leak-clean:** `./test_codegen.sh --leak-check` → `Leaks: 0`
  (mirrors the sanitizers CI job).
- **All four unit-boundary gates green:** `run_tests.sh` (LLVM + parse-only),
  `test_codegen.sh`, `ctest --test-dir build`.
- **Acceptance block** (`evaluation.md` §Epic-level acceptance) passes clause by
  clause: both suites both modes; codegen count ≥ 105; S1 + S2 pass; ARC glob
  leak-clean; `known-issues.md` exists with `grep -c '^### KI-' ≤ 3`; CI green on
  the final commit.

## Risks

- **DB result mapping regression.** Mitigated: the fix only adds a
  `trackTempArray` at the return sites; bound query results are still
  `untrackTempArray`d by their consumers, so `Array<T> xs = query T;` retains
  ownership exactly as before. Verified by the full `test_codegen.sh` golden
  suite (107/0) and the DB-specific goldens (`codegen_db_query`,
  `codegen_db_query_rows`).
- **CI environment drift** (SQLite/libuv presence). Mitigated: the sanitizers
  job already installs sqlite/libuv for the DB codegen tests; no CI change.
