# Spec: Real `bcc test` runner (U6 / REQ-003)

**Epic**: test-validation · **Unit**: U6 · **Branch**: `epic/test-validation/u6-bcc-test`
**Covers**: REQ-003 · **Design decisions honored**: D4 (reuse `genTestRunner` +
AST `SourceLocation`), invariants in `design.md` §Invariants.

## Problem

Today `bcc test` is vestigial: it shells `qcc <file>` for a parse-only exit-code
check (`bcc.cpp:runTests` → `parseFile`) and never compiles or runs `test{}`
blocks. The emitted `__blang_run_tests()` (`CGRuntime.cpp:genTestRunner`) has no
pass/fail counting, is never invoked from a normal build, and aborts the whole
process on the first failed `assert`. The built-in test feature validates
nothing about test behavior.

This unit makes `bcc test` **compile and run** `test{}` blocks: per-test
PASS/FAIL, a total count, a `--filter` by name, `<file>:<line>` on assertion
failure (built on the AST `SourceLocation` from blang-ast U1), and a process
exit code that is non-zero iff any test failed — with test isolation so one
failing test does not abort the rest.

## Scope

**In scope**
- A `qcc` opt-in flag (`--emit-test-main`) that makes `CodeGen` emit a real
  test entry point (`main`) that registers each `test{}` block and dispatches to
  a C runtime test driver, and makes `assert` failures inside test mode print a
  located `<file>:<line>:` diagnostic instead of a bare message.
- A tiny dependency-free C runtime test driver
  (`runtime/blang_testrunner.{c,h}`) that runs each registered test in
  isolation (fork per test), counts pass/fail, honors `--filter <substr>`,
  prints per-test PASS/FAIL and a summary line containing `N passed`, and
  returns a non-zero exit code iff any test failed.
- Rewire `bcc test <file.b>` to compile the file with `--emit-test-main`, link
  the test-runner runtime, run the resulting binary, forward `--filter`, and
  propagate its exit code.
- Test fixtures under `test_files/testblock/`: `all_pass.b` (multiple named
  tests including one named `add_two`, all passing) and `has_failure.b` (at
  least one failing test that asserts, so a located failure is printed).

**Out of scope (non-goals)**
- Changing normal (`bcc build` / single-file `bcc`) codegen output — the
  test-mode `main`/assert changes are strictly behind the opt-in flag and only
  affect the module that contains `test{}` blocks.
- Parallel test execution, test timeouts, per-test setup/teardown, tags, or a
  test-discovery redesign beyond what the done condition needs.
- Making concurrency/async test bodies work under fork isolation (documented
  limitation; fixtures use deterministic synchronous logic).

## Functional requirements

- **FR-001** `qcc --emit-test-main <file>` sets `CodeGen` test mode. When a
  module has one or more `test{}` blocks, `CodeGen` emits a `main(argc, argv)`
  that: (a) registers each test block by name with its generated test function
  via `__blang_test_register`, and (b) tail-calls
  `__blang_test_main(argc, argv)` and returns its result. Without the flag,
  codegen is byte-for-byte unchanged (the legacy `__blang_run_tests` path is
  retained for the no-flag case).
- **FR-002** In test mode, a failed `assert` inside a test function prints a
  single line matching `^[^:]*<file>:<line>:` (the AST `SourceLocation` of the
  `assert`) followed by an "assertion failed" message, then exits non-zero.
  Outside test mode the existing assert output is unchanged.
- **FR-003** The C runtime driver runs each selected test in a forked child so a
  failing `assert` (which exits non-zero in the child) is recorded as a FAIL and
  does **not** abort sibling tests. The parent prints `FAIL  <name>` for a
  non-zero child and `PASS  <name>` otherwise.
- **FR-004** The driver prints a summary line that contains `<passed> passed`
  and `<failed> failed`, and returns exit code `1` iff `failed > 0`, else `0`.
- **FR-005** `--filter <substr>` (accepted as `--filter x` or `--filter=x`)
  restricts the run to tests whose name contains `substr` as a substring; a
  filter that matches a strict, non-empty subset runs fewer tests than the
  unfiltered run.
- **FR-006** `bcc test <file.b>` compiles `<file.b>` with `--emit-test-main`,
  links `libblang_testrunner.a`, runs the produced binary, forwards a
  `--filter <name>` argument if present, and exits with the binary's exit code.
- **FR-007** Fixtures committed: `test_files/testblock/all_pass.b` with several
  named passing tests including a test literally named `add_two`, and
  `test_files/testblock/has_failure.b` with at least one failing test.

## Success criteria (machine-checkable)

Run from the repo root after `cmake --build build`:

- **SC-001** `bcc test test_files/testblock/all_pass.b` exits `0` and its output
  matches `[0-9]+ passed`.
- **SC-002** `bcc test test_files/testblock/has_failure.b` exits non-zero, and
  its output matches **both** `FAIL` and the location regex `[^:]+\.b:[0-9]+:`.
- **SC-003** With
  `full=$(bcc test test_files/testblock/all_pass.b | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+')`
  and
  `filt=$(bcc test --filter add_two test_files/testblock/all_pass.b | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+')`,
  both hold: `filt > 0` and `filt < full` (strict, non-empty subset).
- **SC-004** Isolation teeth: `has_failure.b` contains a passing test *after* the
  failing one, and the failing run still reports that later test PASS (the fail
  did not abort the run).
- **SC-005** Non-regression: `./run_tests.sh` (LLVM) green, `BUILD_DIR=build-parse
  ./run_tests.sh` green, `./test_codegen.sh` green, `BUILD_DIR=build-asan
  ./run_tests.sh` green.
- **SC-006** Normal-build invariant: a normal `bcc <file>` compile of a file with
  no `test{}` blocks is unchanged; the default `build/` codegen tests
  (`test_codegen.sh`, 63/63) stay green (proves the flag is opt-in and inert by
  default).

## Design notes (contracts the implementation must honor)

- **D4 conformance**: reuse `genTestBlock` (unchanged) for each test's body and
  the AST `SourceLocation` for the failure line; add `genTestMain` alongside the
  retained `genTestRunner`.
- **Opt-in, off by default** (mirrors `BLANG_SANITIZE`/`BLANG_FUZZ` discipline):
  the test entry point is only emitted under `--emit-test-main`, and only for the
  module carrying the `test{}` blocks (so combine-mode stdlib modules never emit
  a second `main`).
- **No external test framework** (design D7): the driver is a ~60-line C file
  using only libc + `fork`/`waitpid`; registered as a normal static lib in CMake
  and baked into `bcc` like the other runtime libs.
- **Isolation via fork** keeps the existing `assert`→`exit` semantics intact
  (no setjmp/longjmp rewrite): a failing assert exits the child, the parent
  counts it. The child's located diagnostic reaches the shared stdout, so both
  `FAIL` (parent) and `file:line:` (child) appear in one run's output.

## Tasks

1. `qcc.cpp`: add `--emit-test-main` flag; call `codegen.setTestMode(true)` in
   both combine and normal codegen paths; update `--help`.
2. `CodeGen.h/.cpp`: add `mTestMode` + `setTestMode`; branch in `generate()` to
   `genTestMain` when `mTestMode` and the module has test blocks.
3. `CGRuntime.cpp`: implement `genTestMain` (register + dispatch); add a
   test-mode branch in `genAssertStatement` (`CGStatements.cpp`) that prints the
   located diagnostic.
4. `runtime/blang_testrunner.{c,h}`: fork-isolated runner with `--filter`,
   counting, summary, exit code.
5. `CMakeLists.txt`: `blang_testrunner` static lib; bake `BCC_TESTRUNNER_LIB`
   into `bcc`.
6. `bcc.cpp`: rewrite `runTests` file path to compile+run+filter; keep the old
   discovery behavior as a fallback when no `.b` file argument is given.
7. Fixtures: `test_files/testblock/all_pass.b`, `has_failure.b`.
8. Docs: update `CLAUDE.md` (`bcc test` is now a real runner) — the epic's
   known-issues closure is finalized in U7; note the change here.

## Traceability

| FR | REQ | SC |
|----|-----|----|
| FR-001,FR-002 | REQ-003 | SC-002, SC-006 |
| FR-003,FR-004 | REQ-003 | SC-001, SC-004 |
| FR-005 | REQ-003 | SC-003 |
| FR-006,FR-007 | REQ-003 | SC-001..004 |
| (all) | REQ-003 | SC-005 (non-regression) |
