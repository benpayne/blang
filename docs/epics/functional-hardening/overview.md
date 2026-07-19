# Epic: functional-hardening — Behavioral coverage for feature interactions, aggregate ARC, and unguarded primitives

**Archetype**: evolve (additive test authoring + fixing the bugs it surfaces)

**Status**: launched

**Owner**: Ben Payne

**Created**: 2026-07-19 · **Updated**: 2026-07-19

**Source documents**: the 2026-07-19 functional-coverage evaluation (this
session) — an empirical probe (2 of 16 casual feature-combination programs
broke) plus a systematic coverage audit; the production-readiness roadmap.

## Why

BLang's test suite is genuinely strong for **core features in isolation**: 85
end-to-end tests, ~92% with stdout goldens, dense assertions, and a
teeth-checked harness (golden self-check, ASan/leak-fatal gates, fuzzing). It
is **not** a neglected suite.

But its bugs cluster in a structurally under-sampled category. Only ~13% of
tests exercise feature **interactions**; the rest test features in isolation.
And interaction tests have historically been added **reactively** — after a
bug was found. In one session, probing casually, we surfaced **5 real
functional bugs**: nested-field read, nested-field write, string-field-
assignment ARC (all fixed), plus **struct-valued field reassignment** and
**`Map` unusable via `bcc`** (`collections.b` not auto-included). Two clear
primitive gaps are also completely unguarded: **bitwise/shift operators have
zero behavioral tests**, and generic protocol constraints are behaviorally
untested.

This epic closes that gap the way it should be closed once: proactively, with
behavioral test **matrices** over the exact under-sampled surface — feature
interactions, ARC in aggregates/fields, and the unguarded primitives — plus
testing stdlib through the **real `bcc` driver** (which is how the `Map` bug
hid). Writing these tests will surface more bugs (the probing proved it), so
the epic pairs authoring with fixing under a bounded fix-or-file policy.

## Done condition (epic level)

All of the following hold on a clean checkout of the epic's final state
(concrete commands in `evaluation.md`):

1. **Both suites green, both build modes**: `./run_tests.sh` (LLVM and
   parse-only), `./test_codegen.sh`, and `ctest --test-dir build` all exit 0.
2. **The four matrices exist**: the `test_files/codegen_*.b` count is ≥ 105
   (launch baseline 85 + ≥ 20 new) across the aggregate-ARC, operator,
   interaction, and stdlib-via-`bcc` matrices; every deterministic one has a
   committed stdout golden and passes. The ARC-matrix tests are named
   `test_files/codegen_arc_*.b` (the leak gate globs on that prefix).
3. **The two seeded bugs are fixed**: a committed test for struct-valued field
   reassignment passes and is leak-clean; a program using `import collections;`
   + `Map` **compiles and runs via `bcc`** (not just the test harness).
4. **ARC-matrix tests are leak-clean**: `./test_codegen.sh --leak-check
   test_files/codegen_arc_*.b` exits 0 with 0 leaks (the glob is non-empty).
5. **Fix-or-file, bounded**: `docs/epics/functional-hardening/known-issues.md`
   exists with structured `### KI-N` entries (each a repro + justification);
   `grep -c '^### KI-'` is ≤ 3 (more is a bulk-defer → Open Question, not a
   pass); no committed matrix test is left failing; the seeded bugs never
   appear there.
6. **CI runs the new tests green** (they are auto-globbed into the suite; the
   CI run on the epic's final commit is green).

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | Aggregate/field ARC matrix: refcounted values stored into struct fields, `Array<struct>` / `Map<_,struct>` / `Option<struct>` element store-then-drop, nested write-through, struct-field reassignment — behavioral tests, leak-clean. Fixes the seeded **struct-valued field reassignment** bug. | P1 | Done-conditions #2, #3, #4 |
| REQ-002 | Operator matrix: all bitwise/shift operators (`& \| ^ << >>` — zero behavioral tests today), `%`, compound assigns (`%= ^=`), and `&& / \|\|` short-circuit with side-effect ordering — behavioral tests with goldens. | P1 | Done-condition #2 |
| REQ-003 | Interaction matrix: feature combinations in value contexts — match-bind→field/method, `Option`/`Result` unwrap→field, method-chain→field, array-of-struct element mutate, for-in over aggregates. | P1 | Done-condition #2 |
| REQ-004 | Stdlib-via-`bcc` integration: stdlib exercised through the real `bcc` driver — `Map`/`collections` usable via `bcc` (fixes the seeded auto-include bug), plus behavioral tests for currently-untested `net`/`fs` utility functions. | P1 | Done-conditions #2, #3 |
| REQ-005 | Fix-or-file policy: every bug a matrix surfaces is fixed (its test passes) or recorded in `known-issues.md` with a minimal repro + justification; the two seeded bugs are **required fixes**, not deferrable. | P1 | Done-conditions #3, #5 |
| REQ-006 | Coverage hygiene: new deterministic tests carry stdout goldens; ARC tests run under `--leak-check`; the suites stay green in both build modes; CI runs the new tests. | P1 | Done-conditions #1, #4, #6 |

## Non-goals

- **Rewriting the test harness** — `test_codegen.sh` goldens, `--leak-check`,
  `--selfcheck`, `ctest`, and the fuzzer already exist (test-validation epic);
  this epic is additive test authoring + bug fixes, not new infrastructure.
- **Exhaustive coverage of every feature** — the four matrices target the
  *under-sampled* surface, not 100% coverage. Well-covered core features
  (arithmetic, control flow, strings, JSON, DB) are out of scope.
- **New language features** — nothing new is added; bugs found are fixed.
- **Deep generic-protocol dispatch** beyond a basic constraint-satisfaction
  behavioral test — full generic-protocol support is its own effort.

## Companion documents

| File | Purpose |
|------|---------|
| `workplan.md` | 5 units (four matrices + capstone), dependency map, done conditions |
| `design.md` | the matrices in detail, the fix-or-file mechanism, seeded bugs |
| `evaluation.md` | harness commands, audit rubric, epic acceptance |
| `manifest.yaml` | machine-readable run definition |
| `known-issues.md` | (created by the run) deferred bugs with repros + justification |

## Constraints & context for the manager

- **The two seeded bugs are known and reproduce today** (on branch
  `fix/nested-field-access`): struct-valued field reassignment
  (`o.inner = Inner{...}` reads inconsistently) and `Map` from the `collections`
  module unusable — deeper than the `bcc.cpp:279` auto-include list: a
  `Map<K,V> x` declaration fails to resolve even via `qcc --combine`, only
  inline-defined `Map` works (see design.md S2 for U4's fuller diagnosis).
  These are required fixes (REQ-005), not deferrable.
- **ARC bugs are the highest-value class** — 4 of the 5 session bugs were
  refcount handling in field/aggregate contexts. Prioritize the ARC matrix and
  run it under `--leak-check`; a leak or double-free is a fix, not a defer.
- **Test through the real driver** — the `Map` bug hid because the test harness
  combines stdlib manually while `bcc` does not. Stdlib tests must go through
  `bcc` where driver integration is the point.
- **Do not weaken existing green suites** — `run_tests.sh`, `test_codegen.sh`,
  `ctest`, `--leak-check` are green today; every unit boundary keeps them green.
- Constitution applies: `.specify/memory/constitution.md` (quality gates,
  two-hire spec-audit/code-audit/merge lifecycle, reject-don't-coerce, verified
  memory safety). Per-unit branches `epic/functional-hardening/uN-<slug>`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| — | (none — bug-fix policy is fix-or-file, decided at plan time) | | | |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-07-19 | — | epic created | From the functional-coverage evaluation: suite strong for isolated core, thin on interactions/aggregate-ARC/unguarded primitives; 5 session bugs (3 fixed, 2 seeded). Scope: 4 matrices, fix-or-file (bounded). |
| 2026-07-19 | — | readiness review round 1 | 5 findings. C1: +20 gate was commented-out + undefined BASELINE → pinned BASELINE=85, uncommented, added run.baseline. C2: ARC leak glob vacuous → U1 must name tests codegen_arc_*.b (invariant). C3: fix-or-file gameable → cap <=3 structured ### KI- entries. M1/S2: Map acceptance program refined; probe found the bug is DEEPER (Map from module unresolved via import AND --combine, not just bcc auto-include) — U4 scope updated. M2: soft_conflicts [U1,U3] recorded. done_condition re-synced. |
| 2026-07-19 | — | readiness review passed (10/10); status → ready | All 5 findings resolved (BASELINE pinned=85, ARC-glob naming required, fix-or-file cap=3, S2 deepened, soft_conflicts recorded); branch merged to master + pushed (91298d6). |
| 2026-07-19 | 7708a1f6 | launched on devbot | endpoint http://localhost:8000, dir a6b2f628, fully_autonomous, no_progress_threshold 10, 50M tokens, max 400 turns / 14 days. Team: 1 implementer + 1 independent reviewer (per PM). Baseline codegen=85 (master 91298d6). |
| 2026-07-19 | 7708a1f6 | launch baseline confirmed GREEN | Both builds OK (LLVM `build` + parse-only `build-parse`). `run_tests.sh`=195 pass/0 fail (LLVM), 190 pass/0 fail (`BUILD_DIR=build-parse`). `test_codegen.sh`=85 pass/0 fail (78 golden-checked, 7 quarantined). `ctest --test-dir build`=54/54. codegen_*.b count=85 (matches manifest baseline); codegen_arc_*.b=0. All gates green. |
| 2026-07-19 | 7708a1f6 | seeded bugs S1+S2 reproduced | **S1** (struct-field reassignment): `o.inner = Inner{v:99}` write dropped — prints `after 1`, assert fails, exit 1 (bcc). **S2** (Map via module): `Map<string,int> m` from `import collections;` → `error: Failed parse varible` at declaration, fails identically via `bcc` AND `qcc --combine stdlib/collections.b`. Both match design.md S1/S2. |
| 2026-07-19 | 7708a1f6 | U1 spec drafted → spec audit | `specs/017-arc-matrix/spec.md` on branch `epic/functional-hardening/u1-arc-matrix`: 7 `codegen_arc_*.b` + `codegen_struct_field_reassign.b` (S1 fix), all leak-checked. OQ-U1-1 raised (S1 fix lands in CGStruct.cpp → soft conflict [U1,U3], serialize). STOP — handed to reviewer for spec audit. |
| 2026-07-19 | 7708a1f6 | U1 spec APPROVED (SPEC-1/SPEC-2) | Reviewer approved with 2 required changes, folded in: SPEC-1 (S1 test must println reassigned field under golden — fails pre-fix); SPEC-2 (real symptom is read-path-dependent UAF, not a dropped store). Manager: SERIALIZE U1 before U3 CGStruct edits; U2/U4 parallel. |
| 2026-07-19 | 7708a1f6 | U1 IMPLEMENTED → code audit | S1 fixed in `CGStruct.cpp` genFieldAssignment (struct-field `=` now takes ownership of new struct + releases old, retain-before-release; root cause = missing ARC transfer → temp freed at stmt end = UAF; valgrind-confirmed pre/post). 8 new tests (7 `codegen_arc_*.b` + `codegen_struct_field_reassign.b`) w/ goldens, S1 test verified fail-pre-fix/pass-post-fix. Gates: test_codegen 93/0 (86 golden), leak-check 8/8 CLEAN 0 leaks, run_tests 195/190, ctest 54/54. **Handoff note for U3**: generic-method-chain→field (`m.get(k).field` on a generic `V`-returning method) reads empty — U3's method-chain→field shape, not filed (not an ARC bug). STOP — handed to reviewer for code audit. |
