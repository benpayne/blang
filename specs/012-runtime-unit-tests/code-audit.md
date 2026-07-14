# Code Audit — U4 Runtime unit tests + leak-check teeth (`012-runtime-unit-tests`)

**Phase**: 4 (code audit, pre-merge) · independent reviewer pass; **both build
dirs re-created from scratch** (`rm -rf build build-asan`) and all gates re-run.
**Gate**. Rubric: evaluation.md §Per-unit gates + §Audit plan + constitution.

## SC verification (re-run by reviewer, fresh clean builds)

| SC | Result | Evidence |
|----|--------|----------|
| SC-001 ctest count ≥30 | **PASS** | `ctest --test-dir build -N \| grep -c 'Test #'` = **45** (54 real tests; the grep under-counts 9 single-digit `Test  #N` lines — still well over 30). |
| SC-002 `ctest --test-dir build` | **PASS** | exit 0; 54/54. |
| SC-003 `ctest --test-dir build-asan` | **PASS** | exit 0; 54/54 under ASan/UBSan (no sanitizer error/leak in the tests). |
| SC-004 bounds-removal teeth | **PASS** | see F2. |
| SC-005 injected leak fatal | **PASS** | `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b` → exit **1**, `Leaks: 1` (64B). |
| SC-006 clean suite `Leaks: 0` | **PASS** | `./test_codegen.sh --leak-check` → exit 0, `Leaks: 0`, `Known-leaks: 4`; **zero** un-quarantined LEAK lines. |
| SC-007 boundary green | **PASS** | run_tests 186/181, test_codegen 63/63, `BUILD_DIR=build-asan ./run_tests.sh` 186. |

## Teeth verification

- **F2 bounds removal (SC-004).** Neutered ONLY `__blang_array_get`'s bounds
  condition (`if ( index<0 || index>=length )` → `if ( 0 )`, `array_set` guard
  left intact), rebuilt `test_array` (normal build) → `array_get_oob` **RED**
  (ctest exit 8); reverted → **GREEN**. The guard, not something incidental, is
  what the test asserts. (Demonstrated on the normal build — under ASan the
  removed guard is caught by ASan, masking the point, per spec edge case.)
- **F1 leak-quarantine is minimal + load-bearing.** The list has **exactly 4**
  entries. Removing `codegen_method_chain` from it made `--leak-check` **fatal**
  (exit 1, `LEAK codegen_method_chain`, `Leaks: 1`); restoring → exit 0. So every
  entry is a real leaker and any *unlisted* leaker is fatal — teeth against new
  leaks. The injected `leak_probe.b` (not listed) is likewise fatal (SC-005).
  Known leakers are still **run** and shown as `KNOWN-LEAK` (visible, tracked).
- **F3 known-answers, not stubs.** Corrupting one assertion's expected value
  (`json_object_roundtrip` 7→8) turned that test **RED**; reverting → **GREEN**.
  The 54 cases are real value comparisons. (Cross-check: F2 already shows the
  bounds probes are non-trivial.)
- **F4 both build dirs.** `ctest --test-dir build` and `--test-dir build-asan`
  both exit 0 with 54 tests — the sanitizer config actually runs them.

## Findings

- **F1 (spec audit) RESOLVED** — leak quarantine satisfies all six constraints:
  exactly the 4 identified tests, each justified (root cause in the file),
  injected fixture fatal, unlisted leaker fatal, known leakers visible
  (`KNOWN-LEAK`), and **Open Question OQ-1 filed** (overview.md) for a dedicated
  ARC-leak unit. It is a tracked interim, not a silent mask.
- **F2/F3/F4 RESOLVED** — above.
- **Count-command quirk (noted, not blocking).** The done-condition's
  `grep -c 'Test #'` under-counts single-digit tests (ctest right-aligns as
  `Test  #N`). Real count 54; grep 45; both ≥30. No gaming — 54 genuine tests.
- **Leak-check leak semantics change is scoped.** `--leak-check` now exits
  non-zero on an unexpected leak; non-leak-check runs and the default suite are
  unchanged (test_codegen 63/63; leaks only affect `--leak-check` exit).
- Tree verified clean after all probes (`blang_array.c`, `test_json.c`,
  `codegen_leak_quarantine.txt` == HEAD; only ignored build dirs untracked).

## Verdict

**PASS — approved for merge.** 54 CTest known-answer runtime tests (grep-count 45
≥ 30) green in `build` and `build-asan`; bounds enforcement proven with teeth;
`--leak-check` now fatal on unexpected/injected leaks and clean (`Leaks: 0`) on
the suite via a minimal, load-bearing, justified leak quarantine + OQ-1. Boundary
green. Proceed to Phase 5.
