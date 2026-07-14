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
| leak check (fixed to have teeth) | `./test_codegen.sh --leak-check` | exit 0 + `Leaks: 0` on clean; **exit non-zero** on injected leak | U4, U7 |
| runtime unit tests | `ctest --test-dir build` and `ctest --test-dir build-asan` | exit 0; count `≥ 30` asserted via `ctest -N` | U5, U7 |
| fuzz corpus replay | `build/fuzz_parse test_files/fuzz/corpus/ -runs=0` (corpus `≥ 20`) | exit 0 (no crash) | U6, U7 |
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

- **Baseline (measured and recorded at launch in the status log — do not
  hardcode):** run `./run_tests.sh` (LLVM build), `BUILD_DIR=build-parse
  ./run_tests.sh` (parse-only), `./test_codegen.sh`, and `make -C demos run`,
  and write the actual pass/total counts into the status-log baseline row. As
  of 2026-07-14 the LLVM parse suite is **186** (the earlier "162" was
  pre-blang-ast and is stale); `test_codegen.sh` is 63/63; `make -C demos run`
  exercises the **10** runnable demos (the 4 network demos are build-only).
  Only *new* failures are gate failures; an inherited-red must be dispositioned
  explicitly.
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
set -e

# 1. goldens with teeth (bounded quarantine, asserted floor, real teeth)
test "$(ls test_files/codegen_*.expected.out | wc -l)" -ge 55
diff <(sort -u test_files/codegen_quarantine.txt | grep -vE '^\s*#|^\s*$') \
     <(sort -u docs/epics/test-validation/approved_quarantine.txt)      # empty diff
./test_codegen.sh                                     # exit 0, goldens matched
./test_codegen.sh --selfcheck; test $? -ne 0          # corrupts a real golden, must go red
./test_codegen.sh --selfcheck 2>&1 | grep -q 'SELFCHECK: OK'  # proves it ran, not `exit 1`

# 2. real bcc test — assert on OUTPUT, not just exit codes
bcc test test_files/testblock/all_pass.b | grep -Eq '[0-9]+ passed'
! bcc test test_files/testblock/has_failure.b            # exits non-zero
bcc test test_files/testblock/has_failure.b 2>&1 | grep -Eq 'FAIL'
bcc test test_files/testblock/has_failure.b 2>&1 | grep -Eq '[^:]+\.b:[0-9]+:'
full=$(bcc test test_files/testblock/all_pass.b | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+')
filt=$(bcc test --filter add_two test_files/testblock/all_pass.b | grep -oE '[0-9]+ passed' | grep -oE '[0-9]+')
test "$filt" -gt 0 && test "$filt" -lt "$full"          # strict non-empty subset

# 3. sanitizers with teeth
cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && cmake --build build-asan -j"$(nproc)"
BUILD_DIR=build-asan ./run_tests.sh                     # exit 0
./test_codegen.sh --leak-check test_files/testblock/leak_probe.b; test $? -ne 0  # leak is fatal
./test_codegen.sh --leak-check | grep -Eq 'Leaks:[[:space:]]*0'                  # clean suite, 0 leaks

# 4. runtime unit tests — assert the count, run under ASan
test "$(ctest --test-dir build -N | grep -c 'Test #')" -ge 30
ctest --test-dir build
ctest --test-dir build-asan

# 5. bounded fuzzing with teeth
test "$(ls test_files/fuzz/corpus | wc -l)" -ge 20
build/fuzz_parse test_files/fuzz/corpus/ -runs=0        # crash-free replay
# (parser-reachability poison-input proof is a U6 PR artifact the manager confirms)

# 6. CI executes each check green on the final commit (not a grep of ci.yml text)
gh run list --branch master --limit 1 --json conclusion --jq '.[0].conclusion' | grep -qx success
```

`docs/epics/test-validation/approved_quarantine.txt` is the pinned list from
`design.md` §"Quarantine list"; U2 creates it (or the reviewer does at U2) so
this diff is checkable.

The manager additionally confirms `CLAUDE.md` known-issues no longer lists the
holes this epic closed (exit-code-only codegen tests; parse-only `bcc test`; no
sanitizer CI; no runtime unit tests).
