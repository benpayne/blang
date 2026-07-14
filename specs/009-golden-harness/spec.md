# Feature Specification: Golden-output harness

**Feature Branch**: `epic/test-validation/u1-golden-harness`

**Created**: 2026-07-14

**Status**: Draft

**Epic**: test-validation · **Unit**: U1 · **Covers**: REQ-001

**Input**: Extend `test_codegen.sh` so each deterministic codegen test's captured
stdout is compared against a committed golden file, a mismatch fails the test
with a diff, and the comparison is provably tooth-y via a `--selfcheck` mode.
This unit builds the **harness** only; the mass migration of goldens for all
deterministic tests is unit U2.

## Overview & Problem

`test_codegen.sh` compiles and runs each `codegen_*.b` test through the full
pipeline (qcc → llc → cc → run) and checks **only the binary's exit code**. It
captures the program's stdout into `run_output` (line ~274) and then discards
it. A program that prints the *wrong answer* passes as long as it exits 0. There
are no golden-output files anywhere in the tree. This unit closes that hole by
adding exact-match stdout golden comparison with a proven-tooth self-check,
without weakening any existing green suite.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Wrong output is caught by golden comparison (Priority: P1)

A developer changes codegen such that a compiled test binary prints a wrong
result but still exits 0. Running `./test_codegen.sh` must turn that test **red**
(non-zero exit, with a diff shown), instead of silently passing.

**Why this priority**: This is the entire purpose of REQ-001 and the epic —
"trust the tests." Without it, wrong output passes silently. It is the MVP.

**Independent Test**: Commit one sample golden for a deterministic test; run
`./test_codegen.sh` (green). Then apply a one-line wrong-output patch to that
test's source (or its golden) and re-run: the suite exits non-zero and shows the
mismatch. Reverting restores green.

**Acceptance Scenarios**:

1. **Given** a deterministic test `codegen_<name>.b` with a committed golden
   `test_files/codegen_<name>.expected.out` that matches its real stdout,
   **When** `./test_codegen.sh` runs, **Then** the test is reported PASS and the
   suite exits 0.
2. **Given** the same test, **When** its golden (or the program's output) is
   altered so stdout no longer matches, **Then** that test is reported FAIL with
   a diff of expected-vs-actual and the suite exits non-zero.
3. **Given** the full codegen suite, **When** `./test_codegen.sh` runs with the
   sample golden(s) committed, **Then** all 63 codegen tests remain green
   (exit-code behavior preserved for tests without goldens yet) and the suite
   exits 0.

---

### User Story 2 - The comparison is provably tooth-y (`--selfcheck`) (Priority: P1)

A reviewer must be able to prove, in one command, that the golden comparator
actually detects a corrupted golden — that it is not a no-op that always passes.

**Why this priority**: A testing epic's cardinal sin is a gate that passes
trivially. `--selfcheck` is the teeth-proof and is a named epic done-condition.

**Independent Test**: Run `./test_codegen.sh --selfcheck`; confirm it exits
non-zero and its output contains the literal line `SELFCHECK: OK`.

**Acceptance Scenarios**:

1. **Given** at least one committed golden, **When** `./test_codegen.sh
   --selfcheck` runs, **Then** it copies that real golden to a temp location,
   corrupts the copy, runs the golden comparison against the corrupted value,
   observes that the comparison reports a mismatch (goes red), prints the literal
   line `SELFCHECK: OK` to stdout, and exits **non-zero**.
2. **Given** `--selfcheck` runs, **When** it completes, **Then** the committed
   golden file on disk is **byte-for-byte unchanged** (corruption happens only in
   a temp copy).
3. **Given** a (hypothetical) broken comparator that fails to detect the
   corruption, **When** `--selfcheck` runs, **Then** it does **not** print
   `SELFCHECK: OK` and signals failure through a distinct path (so a passing
   comparator and a broken comparator are distinguishable).

---

### User Story 3 - Non-deterministic tests are quarantined explicitly (Priority: P2)

Some codegen tests have timing/order-dependent output or bind real sockets.
These must skip golden comparison **explicitly** (never via loose matching) while
still running for exit code.

**Why this priority**: Honest handling of non-determinism is required by
design.md D2; loose matching to paper over flakiness is a review-blocking
finding. U1 provides the *mechanism*; U2 populates the fixed approved list.

**Independent Test**: Add a test name to `test_files/codegen_quarantine.txt`;
confirm that test skips golden comparison but is still compiled and run for exit
code.

**Acceptance Scenarios**:

1. **Given** `test_files/codegen_quarantine.txt` lists a test name, **When**
   `./test_codegen.sh` runs, **Then** that test is compiled and run (exit code
   checked) but its stdout is **not** compared to any golden, and the absence of a
   golden for it is not a failure.
2. **Given** the quarantine file contains comment lines (starting `#`) and blank
   lines, **When** it is parsed, **Then** those lines are ignored.
3. **Given** a test that is **not** quarantined and has **no** golden file,
   **When** `./test_codegen.sh` runs, **Then** the missing golden is made visible
   per FR-008 (reported distinctly, not silently passed).

---

### User Story 4 - Goldens can be (re)generated safely (`--update-goldens`) (Priority: P3)

A developer needs to (re)generate golden files for deterministic tests from
current stdout without hand-writing each — but must never overwrite quarantined
tests' (absent) goldens.

**Why this priority**: Convenience/authoring aid that U2 will lean on heavily.
Not required for the teeth-proof but needed for practical golden management.

**Independent Test**: Delete a sample golden, run `./test_codegen.sh
--update-goldens`, confirm the golden is regenerated and matches; confirm no
golden is written for a quarantined test.

**Acceptance Scenarios**:

1. **Given** a deterministic (non-quarantined) test, **When** `./test_codegen.sh
   --update-goldens` runs, **Then** its golden `test_files/<name>.expected.out`
   is written from the current captured stdout (with the same single-trailing-
   newline normalization used for comparison).
2. **Given** a quarantined test, **When** `--update-goldens` runs, **Then** no
   golden file is created or modified for it.

---

### Edge Cases

- **Empty stdout**: a test that legitimately prints nothing has an empty (0-byte)
  golden; an empty golden matching empty stdout is a PASS, not a "missing golden."
- **Trailing newline**: the only permitted normalization is stripping a **single**
  trailing newline from both sides before comparison — nothing else. A program
  that prints `foo\n` and a golden containing `foo` (no newline) match; `foo\n\n`
  vs `foo` do **not** match (only one newline is stripped).
- **Binary/most-likely-text**: goldens are treated as exact byte streams after the
  single-trailing-newline rule; no whitespace collapsing, no locale transforms.
- **Quarantined-and-goldened**: if a quarantined test also happens to have a
  golden on disk, the quarantine wins (comparison skipped) — but `--update-goldens`
  still must not write one.
- **Selfcheck with zero goldens**: `--selfcheck` requires at least one committed
  golden to corrupt; if none exist it must fail loudly (non-zero, distinct
  message), never print `SELFCHECK: OK`.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: `test_codegen.sh` MUST, for each non-quarantined test, compare the
  compiled binary's captured stdout against a committed golden file at
  `test_files/<name>.expected.out` (where `<name>` is the test basename, e.g.
  `codegen_simple`), and report the test FAIL with a diff when they differ.
- **FR-002**: The comparison MUST be **exact match** after the only permitted
  normalization: stripping a **single** trailing newline from both the captured
  stdout and the golden. No loose, substring, regex, whitespace-collapsing, or
  locale-dependent matching is permitted.
- **FR-003**: The suite MUST exit 0 iff every test passes both its exit-code
  check and (for non-quarantined tests) its golden comparison; any golden
  mismatch MUST cause a non-zero suite exit.
- **FR-004**: `test_codegen.sh` MUST support a `--selfcheck` mode that copies a
  real committed golden to a temp location, corrupts the copy, drives the golden
  comparison against the corrupted value, asserts the comparison detected the
  mismatch, prints the literal line `SELFCHECK: OK`, and exits **non-zero**. It
  MUST NOT mutate any committed golden file. If the comparator fails to detect the
  corruption, `--selfcheck` MUST NOT print `SELFCHECK: OK` and MUST signal failure
  through a distinct path.
- **FR-005**: `test_codegen.sh` MUST support a `--update-goldens` mode that writes
  golden files for deterministic (non-quarantined) tests from current stdout,
  using the same single-trailing-newline normalization, and MUST NOT create or
  modify goldens for quarantined tests.
- **FR-006**: `test_codegen.sh` MUST read a quarantine list at
  `test_files/codegen_quarantine.txt` (one test name per line; `#` comments and
  blank lines ignored). Quarantined tests SKIP golden comparison but are STILL
  compiled and run for exit code, and their missing golden is not a failure.
- **FR-007**: The quarantine file MUST exist in the tree at U1 merge (U1 seeds the
  mechanism; the fixed approved contents are U2's responsibility). U1 MUST NOT
  weaken the mechanism to force green.
- **FR-008**: A test that is neither quarantined nor has a golden file MUST be
  handled visibly — its missing golden reported distinctly (e.g. a `MISS`/`NO
  GOLDEN` status) — so that it is never silently treated as a golden PASS. (U1
  policy note: to preserve the 63/63 exit-code baseline while only a sample
  golden exists, a missing golden for a non-quarantined test is reported as a
  visible non-fatal status and still exit-code-checked; U2 flips remaining
  missing-golden tests to goldened. The choice made MUST be documented in the
  spec's Assumptions and MUST NOT let wrong output pass once a golden exists.)
- **FR-009**: At least **one** real sample golden MUST be committed with U1 so
  `--selfcheck` has a real golden to corrupt and the default run has a real
  comparison to perform.
- **FR-010**: Existing behavior MUST be preserved: `--verbose`, `--leak-check`,
  `--valgrind`, `BUILD_DIR` override, single-file mode, and explicit multi-file
  argument mode all continue to work as before. U1 MUST NOT change `--leak-check`
  leak-fatality semantics (that is U4's scope).
- **FR-011**: The change set MUST be limited to `test_codegen.sh`, the new
  `test_files/codegen_quarantine.txt`, and at least one committed sample golden
  (plus, if needed for the wrong-output demo, a documented scripted step). No
  changes to compiler/runtime source, CMake, or CI in U1.

### Key Entities *(include if feature involves data)*

- **Golden file** (`test_files/<name>.expected.out`): the exact expected stdout
  of a compiled test binary, modulo a single trailing newline. One per
  deterministic test.
- **Quarantine list** (`test_files/codegen_quarantine.txt`): newline-delimited
  set of test basenames exempt from golden comparison; `#` comments / blanks
  ignored. The mechanism is U1; the fixed approved contents are U2.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `./test_codegen.sh` exits 0 with the committed sample golden(s)
  matching real stdout, and all 63 codegen tests remain green (exit-code baseline
  preserved).
- **SC-002**: `./test_codegen.sh --selfcheck` exits **non-zero** AND its output
  contains the literal line `SELFCHECK: OK`. Formally:
  `./test_codegen.sh --selfcheck; test $? -ne 0` holds, and
  `./test_codegen.sh --selfcheck 2>&1 | grep -q 'SELFCHECK: OK'` holds.
- **SC-003**: A scripted one-line wrong-output patch to one deterministic test
  (its source or its golden) flips `./test_codegen.sh` from exit 0 (green) to
  exit non-zero (red); reverting the patch restores exit 0.
- **SC-004**: `test_files/codegen_quarantine.txt` exists; a name listed in it
  causes that test to skip golden comparison while still being compiled and run
  for exit code (proven by adding a name and observing skip + still-run).
- **SC-005**: After `--selfcheck` runs, every committed golden file is
  byte-for-byte unchanged (no committed golden is mutated).
- **SC-006**: `./run_tests.sh` (LLVM build) stays 186/186 green and
  `BUILD_DIR=build-parse ./run_tests.sh` (parse-only) stays 181/181 green;
  default build artifacts are unchanged.

## Assumptions

- **Missing-golden policy (FR-008)**: while only sample goldens exist at U1,
  a non-quarantined test with no golden is reported with a distinct visible status
  (e.g. `NO GOLDEN`) and still exit-code-checked; it is NOT counted as a golden
  pass. This keeps the 63/63 exit-code baseline green through U1 without letting
  wrong output pass once a golden is present. U2 eliminates the remaining
  missing-golden cases by goldening every deterministic test.
- **Normalization**: exactly one trailing newline is stripped from both sides;
  this matches how shell command substitution and typical `printf`/`println`
  output behave, and is the single documented transform from design.md D2.
- **Selfcheck target**: `--selfcheck` corrupts an existing committed golden
  (the sample golden from FR-009 is a stable choice) in a temp copy; it never
  depends on network/threading tests.
- **Environment**: LLVM 18 build present (`build/qcc` + runtime `.a` libs), same
  as today's `test_codegen.sh` precondition; quarantine/golden logic is pure
  shell and adds no external dependency.
- **Scope boundary**: goldening all deterministic tests (U2), the fixed approved
  quarantine contents (U2), `--leak-check` teeth fix (U4), and CI wiring (U7) are
  explicitly out of scope for U1.
