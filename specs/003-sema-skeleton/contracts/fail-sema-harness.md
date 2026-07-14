# Contract: `fail/sema/` Test Harness Enforcement

Extends the U2 expected-error harness for the new `test_files/fail/sema/`
category. Backward compatible with the rest of `fail/`.

## Discovery

- `run_tests.sh` already discovers `.b` files recursively under
  `test_files/fail/` (`find … -name '*.b'`), so files under
  `test_files/fail/sema/` are picked up with no change to the find, and judged
  via the existing negative-test path (invoked `--parse-only`, stderr captured).

## Judgement (fail/sema/ specifically)

For a file whose path is under `test_files/fail/sema/`:

1. Run `build/qcc --parse-only <file>`; capture exit code and stderr.
2. If exit code is 0 → **FAIL** (expected semantic rejection).
3. Else the test **PASSES** iff **both**:
   a. stderr matches the canonical regex `^[^:]+\.b:[0-9]+:[0-9]+: error: `
      (asserted for **every** `fail/sema/` file — the per-file check
      done-condition #3 requires), **and**
   b. stderr matches the test's own declared pattern (companion
      `<test>.b.expected` file, precedence, or inline `// EXPECT-ERROR:`).
4. Otherwise → **FAIL** (in `--verbose`, print expected vs. actual).

Files under `test_files/fail/` **not** in `sema/` keep the exact U2 behavior
(per-test pattern if declared, else exit-code-only).

## Build modes

- Runs in both Gate A (LLVM build) and Gate B (`BUILD_DIR=build-parse`, non-LLVM
  build). Sema-stage errors do not depend on LLVM, so both modes produce the
  same located stderr.

## Verification hooks

- SC-003: `fail/sema/` fixtures pass in both build modes, asserting canonical
  regex + message.
- SC-004: mutating a fixture's expected pattern to a non-matching string makes
  `run_tests.sh` fail that test; reverting restores green.
