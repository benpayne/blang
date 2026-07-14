# Evaluation: test-validation

**Epic**: [overview.md](overview.md) · **Constitution**: `.specify/memory/constitution.md`

Instantiates the constitution's audit pattern for this epic: every unit is
audited twice — a **spec audit** (Phase 2, before implementation) and a **code
audit** (Phase 4, before merge) — by the secondary reviewer hire, gated by the
manager. Commands below are literal and runnable.

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema suite (LLVM) | `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh` | exit 0 | every unit |
| parse/sema suite (parse-only) | `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh` | exit 0 | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | exit 0 (stdout matches goldens) | U1+ |
| golden teeth self-check | `./test_codegen.sh --selfcheck` | exit **non-zero** (mismatch detected) | U1, U7 |
| behavioral test runner | `bcc test test_files/testblock/all_pass.b` / `... has_failure.b` | exit 0 / non-zero with counts + `file:line` | U3, U7 |
| sanitizer compiler build | `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && cmake --build build-asan -j"$(nproc)" && BUILD_DIR=build-asan ./run_tests.sh` | exit 0 | U4, U7 |
| leak check | `./test_codegen.sh --leak-check` | exit 0, 0 leaks (deterministic suite) | U4, U7 |
| runtime unit tests | `ctest --test-dir build` | exit 0, ≥ 30 tests | U5, U7 |
| fuzz corpus replay | `build/fuzz_parse test_files/fuzz/corpus/ -runs=0` | exit 0 (no crash) | U6, U7 |
| demos | `make -C demos run` | exit 0 | U7 |

## Per-unit gates (every PR)

The reviewer independently runs, on the PR branch:

1. Both parse/sema suites green (LLVM build and parse-only build).
2. `./test_codegen.sh` green (from U1 onward, goldens compared).
3. The unit's own harness rows from the table above (e.g. U3 runs the `bcc test`
   fixtures; U4 runs the sanitizer build + leak check; U5 runs `ctest`; U6
   replays the corpus).
4. For units touching `runtime/` or ARC/codegen memory paths (U3, U4, U5):
   `./test_codegen.sh --leak-check` exits 0.

## Audit plan (instantiating the constitution)

| Audit | When | Rubric | Gates? |
|-------|------|--------|--------|
| spec audit | after each unit's speckit spec | Spec covers the unit's REQ IDs with machine-checkable acceptance criteria consistent with the unit's done-when; conforms to `design.md` decisions (exact-match goldens, opt-in sanitizer/fuzz options, no external test deps); no scope creep; tasks include the required new tests/fixtures | gates |
| code review | before each unit merges | Constitution code-review standard + epic notes: the check has **teeth** (not a no-op comparator / trivially-passing gate); non-determinism handled by explicit quarantine, not loose matching; any wrong-output or memory bug surfaced is fixed (not masked); reviewer re-runs all gate commands rather than trusting CI | gates |
| memory-safety evidence | U3, U4, U5 | `--leak-check`/`ctest`-under-ASan output attached to the PR; 0 leaks | gates |
| functional review | epic completion (U7) | The full "Epic-level acceptance" block below passes | gates |

## Regression protection (evolve)

- **Baseline (record at launch in the status log):** `./run_tests.sh` = 162
  (LLVM) / 154 (parse-only); `./test_codegen.sh` = 63/63; demos 14 files,
  `make -C demos run` result recorded. Only *new* failures are gate failures;
  an inherited-red must be dispositioned explicitly.
- **Must not change:** default build artifacts; existing CI `parse-only` /
  `with-llvm` legs; normal `bcc build` codegen output.
- A golden that turns a test red must be triaged: correct-output bug fixed, or
  test quarantined with a documented reason — never a loosened match to force green.

## Evidence requirements

Each completed unit leaves behind, on its PR:
- U1: `--selfcheck` output (non-zero) proving teeth; a demo of a wrong-output
  patch failing.
- U2: golden count vs quarantine; the `codegen_quarantine.txt` with reasons;
  reviewer's ≥ 5-golden correctness spot-check note.
- U3: fixture run transcripts (pass fixture exit 0, fail fixture exit non-zero
  with `file:line`), `--filter` demo.
- U4: sanitizer build log + `--leak-check` output (0 leaks); list of any
  leaks/UB fixed.
- U5: `ctest` output (≥ 30 tests) under ASan.
- U6: corpus replay exit 0; list of crashes found and their fix commits or
  quarantine justifications.
- U7: CI config diff showing each new required job; `make -C demos run` output.

## Epic-level acceptance (run at U7 completion)

The `overview.md` done condition, executed literally:

```bash
# 1. goldens with teeth
q=$(grep -vc '^\s*#\|^\s*$' test_files/codegen_quarantine.txt); n=$(ls test_files/codegen_*.expected.out | wc -l)
test "$n" -ge $((63 - q))
./test_codegen.sh                      # exit 0, goldens matched
./test_codegen.sh --selfcheck          # exit non-zero, teeth proven

# 2. real bcc test
bcc test test_files/testblock/all_pass.b        # exit 0, prints pass count
bcc test test_files/testblock/has_failure.b     # exit non-zero, PASS/FAIL + file:line
bcc test --filter smoke test_files/testblock/    # runs only matching

# 3. sanitizers in CI
cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && cmake --build build-asan -j"$(nproc)"
BUILD_DIR=build-asan ./run_tests.sh    # exit 0
./test_codegen.sh --leak-check         # exit 0, 0 leaks

# 4. runtime unit tests
ctest --test-dir build                 # exit 0, >= 30 tests

# 5. bounded fuzzing
build/fuzz_parse test_files/fuzz/corpus/ -runs=0   # exit 0, crash-free replay

# 6. CI enforces it — grep the required jobs + demos leg
grep -Eq 'leak|asan|sanitize' .github/workflows/ci.yml
grep -Eq 'ctest|runtime.*test' .github/workflows/ci.yml
grep -Eq 'fuzz' .github/workflows/ci.yml
grep -Eq 'demos' .github/workflows/ci.yml
```

The manager additionally confirms `CLAUDE.md` known-issues no longer lists the
holes this epic closed (exit-code-only codegen tests; parse-only `bcc test`; no
sanitizer CI; no runtime unit tests).
