# Feature Specification: Golden migration (all deterministic codegen tests)

**Feature Branch**: `epic/test-validation/u2-goldens-at-scale`

**Created**: 2026-07-14

**Status**: Draft

**Epic**: test-validation · **Unit**: U2 · **Covers**: REQ-002 · **Depends on**: U1

**Input**: Generate and commit stdout goldens for every deterministic
`codegen_*.b` test, using the U1 harness. Pin the quarantine list to the fixed
approved set so it cannot be widened to nullify goldens.

## Overview & Problem

U1 built the golden-comparison harness and committed 2 sample goldens; the other
55 deterministic tests still run exit-code-only (`NO GOLDEN`). Wrong output for
those still passes silently. U2 closes the hole across the whole deterministic
suite: a committed golden for every non-quarantined `codegen_*.b`, each verified
(where feasible) against the test's stated intent rather than blindly snapshotted,
with the quarantine list frozen to the approved 6-test set.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Every deterministic codegen test has a committed golden (Priority: P1)

**Why this priority**: This is REQ-002 and the point of the epic — no
deterministic test may pass with wrong output. It is the MVP.

**Independent Test**: `ls test_files/codegen_*.expected.out | wc -l` ≥ 55 and
`./test_codegen.sh` exits 0 with every non-quarantined test golden-compared.

**Acceptance Scenarios**:

1. **Given** the 63-test codegen suite with 6 quarantined tests, **When** U2
   completes, **Then** at least 55 committed goldens exist and every
   non-quarantined test is reported as a golden-checked `PASS` (no `NO GOLDEN`
   remaining among non-quarantined tests).
2. **Given** all goldens committed, **When** `./test_codegen.sh` runs, **Then**
   it exits 0 comparing captured stdout to each golden.

---

### User Story 2 - The quarantine list is frozen to the approved set (Priority: P1)

**Why this priority**: If the quarantine could be widened, a hire could dodge a
hard golden by quarantining the test — nullifying the epic. Design.md D2 forbids
this; the list is an epic-scope decision.

**Independent Test**: The name-set of `test_files/codegen_quarantine.txt` equals
the name-set of `docs/epics/test-validation/approved_quarantine.txt` (comments/
blanks ignored) — empty diff.

**Acceptance Scenarios**:

1. **Given** `codegen_quarantine.txt` and `approved_quarantine.txt`, **When**
   both are compared with comment/blank lines ignored, **Then** the diff is empty
   (exactly the 6 approved tests: `codegen_http`, `codegen_http_blang`,
   `codegen_http_streaming`, `codegen_tcp_echo`, `codegen_selector`,
   `codegen_spawn_threaded`).
2. **Given** a hire adds a 7th name to `codegen_quarantine.txt`, **When** the diff
   runs, **Then** it is non-empty (the gate catches widening) — teeth.

---

### User Story 3 - Goldens are verified known-answers, not blind snapshots (Priority: P2)

**Why this priority**: A golden snapshotted from wrong output would lock in a bug.
Design.md and the workplan require verifying goldens reflect *correct* output and
surfacing (not papering over) any wrong output found.

**Independent Test**: A representative sample of goldens (≥ 5) is spot-checked
against the test source's stated expected values; discrepancies are logged as
bugs, not silently accepted.

**Acceptance Scenarios**:

1. **Given** a test whose source states an expected value (comment or `assert`),
   **When** its golden is reviewed, **Then** the golden matches that stated value.
2. **Given** a test whose current output is *wrong* relative to its intent,
   **When** found, **Then** it is surfaced as a bug (fixed if in scope, else raised
   as an Open Question) — never snapshotted as the golden.

---

### Edge Cases

- **Non-deterministic test not on the approved list**: if a non-quarantined test
  (e.g. a spawn/async/wait test) produces order-unstable output, it MUST NOT be
  silently added to the quarantine (that would widen the frozen list). It is
  raised as an Open Question; the epic owner decides whether to extend the
  approved list. Determinism is verified by running the suite multiple times.
- **Empty-output test**: a legitimately silent test has a 0-byte golden (allowed).
- **Environment-dependent output** (paths, pids, timestamps): if a test prints
  such values, its golden would be flaky — treat as the non-determinism case above
  (Open Question), not a loose match.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: A committed golden `test_files/<name>.expected.out` MUST exist for
  every non-quarantined `codegen_*.b` test, produced via the U1 harness
  (`--update-goldens`). `ls test_files/codegen_*.expected.out | wc -l` MUST be ≥ 55.
- **FR-002**: `./test_codegen.sh` MUST exit 0 with every non-quarantined test
  golden-compared (0 remaining `NO GOLDEN` among non-quarantined tests).
- **FR-003**: `test_files/codegen_quarantine.txt` MUST diff-equal
  `docs/epics/test-validation/approved_quarantine.txt` on the name-set (comments/
  blanks ignored) — exactly the 6 approved tests, no widening.
- **FR-004**: Each golden MUST reflect the test's *correct* intended output. A
  representative sample (≥ 5, chosen to include known-answer tests) MUST be
  verified against the source's stated expectation. Any test whose current output
  is wrong MUST be surfaced (fixed in scope, or raised as an Open Question), never
  snapshotted.
- **FR-005**: Determinism of every non-quarantined test MUST be established (the
  golden must be stable across repeated runs). A test found non-deterministic and
  not on the approved list MUST be raised as an Open Question, not silently
  quarantined.
- **FR-006**: The U1 harness behavior (exact match, single-trailing-newline
  normalization, `--selfcheck` teeth) MUST remain intact; U2 adds data (goldens)
  and freezes the quarantine, not harness logic — with one permitted correction:
  if the epic-acceptance quarantine-diff command in `evaluation.md` is
  asymmetric (strips comments on only one side, making a correct list fail), U2
  MAY correct it to strip comments/blanks symmetrically (the semantics both files
  already state), documenting the correction. This preserves teeth (widening still
  fails) and is not a rubric weakening.
- **FR-007**: `./run_tests.sh` (both build modes) and `./test_codegen.sh` MUST
  stay green; `./test_codegen.sh --selfcheck` MUST still print `SELFCHECK: OK` and
  exit non-zero.

### Key Entities *(include if feature involves data)*

- **Golden set**: ~57 `test_files/codegen_*.expected.out` files (55+ committed),
  one per deterministic test.
- **Frozen quarantine list**: `test_files/codegen_quarantine.txt`, name-set equal
  to the approved 6.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `ls test_files/codegen_*.expected.out | wc -l` ≥ 55.
- **SC-002**: `diff <(sort -u test_files/codegen_quarantine.txt | grep -vE '^\s*#|^\s*$') <(sort -u docs/epics/test-validation/approved_quarantine.txt | grep -vE '^\s*#|^\s*$')`
  is empty.
- **SC-003**: `./test_codegen.sh` exits 0; the summary shows `No golden: 0`
  (every non-quarantined test golden-checked) and `Quarantined: 6`.
- **SC-004**: `./test_codegen.sh --selfcheck` exits non-zero AND emits
  `SELFCHECK: OK`.
- **SC-005**: a scripted one-line wrong-output patch to one test flips
  `./test_codegen.sh` from exit 0 to non-zero; revert restores exit 0.
- **SC-006**: `./run_tests.sh` (LLVM) stays 186 and `BUILD_DIR=build-parse
  ./run_tests.sh` stays 181; default build artifacts unchanged.
- **SC-007**: ≥ 5 goldens spot-checked against source intent, recorded in the code
  audit; any wrong-output finding surfaced.

## Assumptions

- The 6 approved quarantined tests are the *only* legitimately non-deterministic
  ones; U2 verifies this by repeated-run determinism testing and raises an Open
  Question if a 7th surfaces.
- `--update-goldens` (U1) is the generation mechanism; goldens store literal
  program stdout, compared under the single-trailing-newline rule.
- Verifying "correct output" means checking against explicit source expectations
  (comments like `// Expected: …`, `assert` statements, `println` of a computed
  known value); tests without an explicit stated answer are verified for
  plausibility and stability.
- Scope: U2 adds goldens + freezes quarantine; it does not change harness logic
  (except the FR-006 acceptance-command correction) and does not touch CI (U7).
