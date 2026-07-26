# Feature Specification: Runtime unit tests + --leak-check teeth

**Feature Branch**: `epic/test-validation/u4-runtime-unit-tests`

**Created**: 2026-07-14

**Status**: Draft

**Epic**: test-validation · **Unit**: U4 (this run) · **Covers**: REQ-005 (runtime units) + REQ-004 (leak-check teeth)

**Input**: Add CTest-registered C-runtime unit tests (≥30, known-answer, running
under ASan via U3's `build-asan`), prove a runtime test fails when a bounds
check is removed, and give `./test_codegen.sh --leak-check` real teeth
(fatal-on-leak + injected-leak fixture + clean `Leaks: 0`).

## Overview & Problem

The runtime C libraries (`blang_array/string/buffer/net/fs/json`) have **no unit
tests** — they are exercised only transitively through compiled programs.
Separately, `./test_codegen.sh --leak-check` counts leaks into a **cosmetic**
`LEAK_TOTAL` that never affects the exit code, so the leak gate has **no teeth**
today (design.md D5, confirmed at launch). This unit adds a tiny dependency-free
C unit-test harness wired into CTest (built and run in both `build` and U3's
`build-asan`), proves bounds-check enforcement has teeth, and makes leaks fatal.

**Real leaks surfaced (recon).** Running `--leak-check` over the current suite
reports leaks in **4** tests, all allocated by `__blang_rc_alloc` (refcounted
temporaries not released): `codegen_method_chain` (128B, pure codegen — an
`fn().method()` rvalue `Info` temporary is never released), `codegen_file_io`
(232B), `codegen_fs_convenience` (335B), `codegen_http` (272B, already
golden-quarantined). These are **codegen ARC gaps** (large/risky to fix within
this unit). Per the workplan they are **tracked with justification** (a leak
quarantine) and escalated as an **Open Question**, not silently masked; the leak
gate keeps teeth for the injected fixture and any *new* leak.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Runtime libraries have real unit tests (Priority: P1)

**Why**: REQ-005 — the runtime's core + error/bounds paths must be directly
tested, not just transitively.

**Independent Test**: `ctest --test-dir build -N | grep -c 'Test #'` ≥ 30 and
`ctest --test-dir build` exits 0.

**Acceptance Scenarios**:

1. **Given** the runtime libs, **When** CTest runs, **Then** ≥ 30 registered
   tests execute known-answer assertions of library behavior (create/length,
   push/pop/get/set, string ops, buffer ops, json roundtrip, fs read/write,
   net error paths) and the run exits 0.
2. **Given** U3's `build-asan`, **When** `ctest --test-dir build-asan` runs,
   **Then** the same tests run under ASan/UBSan and exit 0 (no sanitizer error).
3. Tests are known-answer assertions, not exit-0 stubs (each `CHECK` compares an
   actual value to an expected one; a wrong value fails the test).

---

### User Story 2 - Bounds enforcement has teeth (Priority: P1)

**Why**: A safety claim needs a test that fails when the guard is removed.

**Independent Test**: Removing a library bounds check turns a specific runtime
test red; reverting restores green.

**Acceptance Scenarios**:

1. **Given** `__blang_array_get`'s out-of-bounds `exit(1)` guard, **When** a
   dedicated test invokes an out-of-bounds access (in a forked child) and asserts
   the child terminates non-zero, **Then** it PASSES with the guard present.
2. **Given** the guard is removed, **When** that test runs (normal build), **Then**
   the out-of-bounds read returns normally, the child exits 0, and the test FAILS
   — demonstrated in the PR, then reverted.

---

### User Story 3 - `--leak-check` is fatal on leaks (Priority: P1)

**Why**: REQ-004 leak leg — a leak gate that never fails the build is no gate.

**Independent Test**: `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b`
exits non-zero; `./test_codegen.sh --leak-check` exits 0 reporting `Leaks: 0` on
the non-quarantined suite.

**Acceptance Scenarios**:

1. **Given** an injected-leak fixture (`leak_probe.b`, a deliberately leaked
   allocation), **When** `./test_codegen.sh --leak-check <fixture>` runs, **Then**
   the leak is detected and the script exits **non-zero** (`LEAK_TOTAL > 0` is
   fatal).
2. **Given** the deterministic suite with the 4 known ARC-leak tests in the leak
   quarantine, **When** `./test_codegen.sh --leak-check` runs, **Then** it exits 0
   and prints `Leaks: 0` (no *unexpected* leaks), while each known-leak test is
   run and shown with a distinct `KNOWN-LEAK` status (tracked, not hidden).
3. **Given** a *new* leak appears in a non-quarantined test, **When** `--leak-check`
   runs, **Then** it is fatal (teeth against regressions).

---

### Edge Cases

- **LSan on threaded/socket quarantined tests**: the golden-quarantined network/
  threading tests may report allocations at exit; `codegen_http` is one of the 4
  tracked known-leakers. The leak quarantine is a **separate, minimal, justified**
  list — not the golden quarantine — and must not be widened to dodge new leaks.
- **Bounds test under ASan**: removing the guard under `build-asan` would be caught
  by ASan itself (heap-overflow) rather than exit-0; the removal-teeth demo is on
  the **normal** build where the OOB read returns → child exits 0 → test fails.
- **CTest in both build dirs**: tests must compile & register in the same
  CMakeLists so `build` and `build-asan` both expose them; no external framework.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Add a tiny dependency-free C unit-test harness (assert-style
  `CHECK`/`CHECK_EQ` macros; a fork-based `expect_abort` helper) under
  `runtime/tests/`, with per-library test programs for
  `array/string/buffer/json/fs/net`.
- **FR-002**: Register the tests with CTest (`enable_testing()` + `add_test`) so
  `ctest --test-dir build -N | grep -c 'Test #'` ≥ 30. Each registered test is a
  focused known-answer assertion (dispatch one behavior per `add_test`).
- **FR-003**: `ctest --test-dir build` exits 0; with U3's `build-asan`,
  `ctest --test-dir build-asan` exits 0 (tests run under ASan/UBSan).
- **FR-004**: At least one test asserts a library bounds check by expecting an
  out-of-bounds access to terminate the process non-zero; it MUST fail if the
  check is removed (removal demonstrated in the PR, then reverted).
- **FR-005**: Fix `./test_codegen.sh --leak-check` so a detected leak
  (`LEAK_TOTAL > 0`) forces a **non-zero** script exit. It MUST NOT change
  non-leak-check behavior or the default suite.
- **FR-006**: Add an injected-leak fixture `test_files/testblock/leak_probe.b`
  (a deliberately leaked allocation) so `./test_codegen.sh --leak-check
  <fixture>` exits non-zero. The fixture MUST NOT be picked up by the default
  `codegen_*.b` suite (kept outside the glob).
- **FR-007**: `./test_codegen.sh --leak-check` on the deterministic suite MUST
  exit 0 and report `Leaks: 0`. Achieve this by a minimal, justified **leak
  quarantine** (`test_files/codegen_leak_quarantine.txt`) listing exactly the 4
  known ARC-leak tests; quarantined leakers are still run and shown with a
  distinct `KNOWN-LEAK` status, and do NOT count toward the fatal `LEAK_TOTAL`. A
  leak in a **non-quarantined** test stays fatal.
- **FR-008**: The 4 known ARC leaks are escalated as an **Open Question** (root
  cause + tests recorded) for a dedicated ARC-leak fix; the leak quarantine is the
  non-guessing interim, not a silent mask.
- **FR-009**: The default build and existing suites remain green at the unit
  boundary; runtime unit-test targets do not perturb `qcc`/`bcc`/normal builds.

### Key Entities

- **`runtime/tests/`**: `test_util.h` + `test_<lib>.c` dispatchers.
- **`test_files/testblock/leak_probe.b`**: injected-leak fixture.
- **`test_files/codegen_leak_quarantine.txt`**: minimal tracked list of the 4
  known ARC-leak tests (justified).

## Success Criteria *(mandatory)*

- **SC-001**: `test "$(ctest --test-dir build -N | grep -c 'Test #')" -ge 30` holds.
- **SC-002**: `ctest --test-dir build` exits 0.
- **SC-003**: `ctest --test-dir build-asan` exits 0.
- **SC-004**: Removing a bounds check turns its test red; reverting restores green
  (shown in PR).
- **SC-005**: `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b`
  exits non-zero.
- **SC-006**: `./test_codegen.sh --leak-check` exits 0 and its output matches
  `Leaks:[[:space:]]*0`.
- **SC-007**: Unit boundary green — `./run_tests.sh` 186, `BUILD_DIR=build-parse
  ./run_tests.sh` 181, `./test_codegen.sh` 63/63, `BUILD_DIR=build-asan
  ./run_tests.sh` 186; default build artifacts unchanged.

## Assumptions

- No external C test framework (design D7); a ~40-line assert harness suffices.
- Tests link the runtime static libs from the same build tree, so `build-asan`
  builds them instrumented automatically.
- fs/net tests use `/tmp` files and deterministic error paths (connect-refused,
  selector lifecycle) — no flaky real-network binding.
- The 4 known ARC leaks are out-of-scope-to-fix here (codegen ARC, risky);
  tracked + escalated per FR-008. The leak quarantine holds exactly those 4 and
  is a review-gated, non-widenable list.
- `--leak-check` uses AddressSanitizer/LeakSanitizer (the existing machinery,
  per design D5 "reuse, don't reinvent"); the injected fixture proves detection.
