# Workplan: functional-hardening

**Epic**: [overview.md](overview.md) · **Archetype**: evolve

Each unit is one implementing hire's assignment through the constitution's
five-phase lifecycle (create spec → spec audit → implement → code audit →
merge) on a branch `epic/functional-hardening/uN-<slug>`. Every unit boundary
leaves `./run_tests.sh` and `./test_codegen.sh` green. Each matrix unit both
**authors tests** and **fixes (or files) the bugs those tests surface**, under
the fix-or-file policy (design.md).

## Unit map

```text
U1 (aggregate/field ARC matrix + seed fix) ─┐
U2 (operator matrix) ───────────────────────┤
U3 (interaction matrix) ─────────────────────┼─▶ U5 (CI + known-issues consolidation + close-out)
U4 (stdlib-via-bcc + seed fix) ──────────────┘
```

U1–U4 are independent and parallelizable (they add distinct `codegen_*.b`
files; a bug fix that touches shared codegen — likely U1 vs U3 on `CGStruct` —
is serialized by the manager). U5 is the capstone.

## Fix-or-file policy (applies to every matrix unit)

When a test the unit authors fails, the hire must, in order of preference:
1. **Fix** the bug so the test passes (and is leak-clean if ARC-related); or
2. If the fix is large/risky or needs a language decision, **file** it: add a
   structured entry to `known-issues.md` — a `### KI-N` heading with a fenced
   `Repro:` block and a `Justification:` line — and do **not** commit the
   failing test into the passing suite (reference it in the doc instead). A
   large fix, or one that raises a design question, is an Open Question to the
   manager, not a silent defer.

**Bounded escape hatch (machine-enforced):** acceptance FAILS if `known-issues.md`
holds **more than 3** unfixed matrix bugs (`grep -c '^### KI-'` ≤ 3). Beyond 3,
the hire must raise an Open Question rather than bulk-defer. The two seeded bugs
(struct-field reassignment, `Map` via `bcc`) are **required fixes** — they may
never appear in `known-issues.md`.

## Units

### U1 — Aggregate/field ARC matrix (+ seeded fix)
- **Covers**: REQ-001 (+ REQ-005 seed)
- **Preconditions**: none
- **Work**: author ~6–8 tests over refcounted values in aggregate contexts —
  string/struct into struct fields (incl. **reassignment**), `Array<struct>` /
  `Map<_,struct>` / `Option<struct>` store-then-read-then-drop, nested struct
  write-through, self-assignment. Each asserts values AND has a stdout golden.
  **All ARC-matrix test files MUST be named `test_files/codegen_arc_*.b`** — the
  `--leak-check` acceptance glob depends on this prefix; a different name makes
  the leak gate vacuous (see design.md Invariants). **Fix the seeded
  struct-valued field reassignment bug** (its test is
  `codegen_struct_field_reassign.b`, also covered by the leak glob) so it passes
  leak-clean.
- **Done condition**: `ls test_files/codegen_arc_*.b` is non-empty; those tests
  pass under `./test_codegen.sh` with goldens; `./test_codegen.sh --leak-check
  test_files/codegen_arc_*.b test_files/codegen_struct_field_reassign.b` exits 0,
  0 leaks; the struct-field-reassignment test passes; any other ARC bug found is
  fixed or filed (≤ 3 total, structured); suites green.
- **Audit**: per constitution; **memory-safety evidence required**.
- **Speckit**: `arc-matrix`

### U2 — Operator matrix
- **Covers**: REQ-002
- **Preconditions**: none
- **Work**: author behavioral tests (goldens) for the unguarded primitives —
  all bitwise/shift (`& | ^ << >>`), `%`, compound assigns (`%= ^=`), and
  `&& / ||` short-circuit **with side-effect ordering** (e.g. a call in the RHS
  that must/must-not run). Cover signed/unsigned (`byte`) where relevant. Fix or
  file anything that misbehaves.
- **Done condition**: bitwise/shift/`%`/compound/short-circuit each have a
  passing golden test; a short-circuit test proves the RHS side effect is
  skipped when it should be; suites green.
- **Speckit**: `operator-matrix`

### U3 — Interaction matrix
- **Covers**: REQ-003
- **Preconditions**: none
- **Work**: author tests for feature combinations in value contexts —
  match-bind→field/method, `Option`/`Result` unwrap→field, method-chain→field,
  array-of-struct element mutate, for-in over aggregates using element fields.
  Goldens; fix or file failures.
- **Done condition**: each interaction shape has a passing golden test; suites
  green.
- **Speckit**: `interaction-matrix`

### U4 — Stdlib-via-`bcc` integration (+ seeded fix)
- **Covers**: REQ-004 (+ REQ-005 seed)
- **Preconditions**: none
- **Work**: **fix the seeded `Map`-from-module bug** — make `import collections;`
  + `Map` compile and run through `bcc`. Wiring `collections.b` into the driver's
  stdlib resolution (`bcc.cpp` ~line 279) is **necessary but not sufficient**:
  the probe shows a generic struct from a combined/imported module also fails to
  resolve in a variable declaration (`Map<K,V> x` → "Failed parse varible") even
  via `qcc --combine` — U4 must diagnose that too (only inline-defined `Map`
  works today; see design.md S2). Add behavioral tests that go through `bcc`
  (not only the combine-based harness) for `Map`, and for currently-untested
  `net`/`fs` utility functions (e.g. `fs.read_into`, `net` buffered helpers).
  Fix or file failures.
- **Done condition**: `bcc` compiles+runs a `Map` program via `import
  collections;`; new stdlib behavioral tests pass; suites green.
- **Speckit**: `stdlib-bcc`

### U5 — CI, known-issues consolidation, close-out
- **Covers**: REQ-006
- **Depends on**: U1, U2, U3, U4
- **Work**: confirm the new tests are picked up by CI (auto-globbed) and the CI
  run is green, including the `--leak-check` leg over the ARC matrix; consolidate
  `known-issues.md` (every deferred bug is a `### KI-N` entry with a `Repro:`
  block + `Justification:` line; `grep -c '^### KI-'` ≤ 3, else it was an Open
  Question); confirm the codegen count is ≥ 105 (baseline 85 + 20); update
  `CLAUDE.md` test counts + the functional-coverage note and remove any
  known-issue this epic closed.
- **Done condition**: the epic-level done condition in `overview.md` holds,
  checked by `evaluation.md`'s acceptance block; CI green on the final commit.
- **Audit**: per constitution; **functional review** = full epic done condition.
- **Speckit**: `ci-closeout`

## Sequencing notes for the manager
- **Soft conflict U1 ∥ U3** (recorded in `manifest.yaml` `soft_conflicts`):
  both may touch `CGStruct.cpp` when fixing field/aggregate bugs — serialize
  them if a fix lands there; rebase the later onto the base after each merge and
  re-run gates. The DAG marks them independent, so the manager must apply this
  hint proactively rather than discover the collision only at merge time.
- The seeded fixes (U1 struct-field reassignment, U4 `Map`-via-`bcc`) are the
  first concrete work in their units and are required, not deferrable.
- If a matrix surfaces a bug whose fix touches core ARC or the type system in a
  risky way, raise an Open Question before landing — do not expand scope silently.
- U5 must run after all matrices merge, or CI will gate on absent tests.
