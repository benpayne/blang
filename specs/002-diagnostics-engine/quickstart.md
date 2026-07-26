# Quickstart: Validate U2 (Diagnostics Engine + Expected-Error Harness)

Run from the repo root. Proves REQ-002 (located clean errors), REQ-003 (quiet
by default), and REQ-011 harness half, without reading the implementation.

## Prerequisites

Both builds present:

```bash
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build -j"$(nproc)"
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF
cmake --build build-parse -j"$(nproc)"
```

## 1. Quiet by default (SC-001 / Gate D)

```bash
out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out" && echo "SILENT OK"
```

Expect `SILENT OK` and exit 0. No `Completed parse`, no `Symbol …` trace.

## 2. Verbose re-enables developer output

```bash
./build/qcc --parse-only -v test_files/pass/func_simple.b
```

Expect `Completed parse` (and/or trace) to appear; exit 0.

## 3. Single located error (SC-002)

```bash
./build/qcc --parse-only test_files/fail/missing_brace.b 2>&1
```

Expect exactly one line matching `^[^:]+\.b:[0-9]+:[0-9]+: error: `, no
`Compiler Error in` line, no raw LLVM text. Verify the `line:col` points at
the offending token in the fixture (SC-003; repeat for two more `fail/`
fixtures).

## 4. Debug-compiler opt-in

```bash
./build/qcc --parse-only --debug-compiler test_files/fail/missing_brace.b 2>&1
```

Expect the canonical error line **plus** a compiler-internal throw-site line;
without the flag (step 3) that internal line is absent.

## 5. Expected-error harness (SC-004)

```bash
# A known fail/ test now carries a declaration; the suite passes it:
./run_tests.sh 2>&1 | grep -E "missing_brace|Passed:"

# Mutate the declared pattern to something impossible, re-run, observe FAIL,
# then revert:
#   edit its .expected / // EXPECT-ERROR: pattern to "ZZZ_NO_MATCH"
#   ./run_tests.sh   -> that test now FAILs
#   git checkout -- <the test declaration>
```

## 6. Gates (SC-005 / SC-006)

```bash
# Gate A — LLVM build
./run_tests.sh && ./test_codegen.sh

# Gate B — parse-only build
BUILD_DIR=build-parse ./run_tests.sh

# Gate D — quiet compile (step 1)
out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"
```

All exit 0. Confirm the run shows ≥ 10 `fail/` tests with expected-message
declarations passing (grep the run output or count `.expected` files +
`// EXPECT-ERROR:` comments).

## 7. No accept/reject drift (SC-007)

The pass/fail counts printed by `./run_tests.sh` match the pre-U2 baseline
(162 parse tests in LLVM build, 154 in parse-only). Only messages and
success-silence changed.
