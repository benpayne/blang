# Code Audit — U6 (014-bcc-test-runner)

**Auditor role**: secondary reviewer (independent pass; distinct discipline from
implement — fresh clean rebuild, re-ran every gate command, did not trust prior
output). **Verdict**: **PASS — merged.**

## Method

- `rm -rf build` then reconfigured and rebuilt `build/` from scratch (LLVM),
  confirming the new `blang_testrunner` static lib and the `BCC_TESTRUNNER_LIB`
  wiring build cleanly with no missing-dependency errors.
- Re-ran all clause-2 sub-checks against the freshly built `bcc`, plus the four
  boundary suites, `--leak-check`, and additional teeth probes.

## Clause-2 evidence (fresh binaries)

| Check | Command | Result |
|-------|---------|--------|
| all_pass exit 0 | `bcc test …/all_pass.b` | exit 0 |
| passed regex | `… \| grep -Eq '[0-9]+ passed'` | match (`4 passed`) |
| has_failure non-zero | `bcc test …/has_failure.b` | exit 1 |
| FAIL grep | `… 2>&1 \| grep -Eq 'FAIL'` | match |
| location regex | `… 2>&1 \| grep -Eq '[^:]+\.b:[0-9]+:'` | match (`has_failure.b:17:2:`) |
| strict subset | full=4, filt=1 | `filt>0 && filt<full` ✓ |

## Teeth (beyond the minimum)

- **Isolation (SC-004)**: `has_failure.b` runs `passes_before` (PASS), `fails_here`
  (FAIL, located), `passes_after` (PASS) — the failure did not abort siblings.
- **Not trivially green**: a scripted wrong-output patch
  (`assert add(2,3) == 99`) flips the run to `exit 1`, `FAIL  add_two`,
  `3 passed, 1 failed` — the runner validates actual assertion results, not just
  compile/exit success.

## Spec-audit findings — disposition

- **F1** (located line format): matched by the emitted
  `test_files/testblock/has_failure.b:17:2: assertion failed: …` (no `:` before
  `.b`). Resolved.
- **F3** (plain, greppable output): driver uses plain `printf` with no ANSI;
  `cat -v` shows no escape sequences; all `| grep` acceptance commands match on
  piped output. Resolved.
- **F5** (fixture composition): `all_pass.b` = 4 tests, exactly one named
  `add_two`, the other three names free of that substring → strict subset holds.
  Resolved.
- **F6** (filter arg order): verified `--filter x <file>`, `<file> --filter x`,
  and `--filter=x` all select the subset. Resolved.

## Audit-round fix applied

- The child ran `_exit(0)` on a passing test, which (unlike `exit`) does not
  flush stdio — a passing test that printed buffered output would lose it. Added
  `fflush(NULL)` before `_exit(0)` in `runtime/blang_testrunner.c`. Re-verified
  all gates green after the fix.

## Boundary + memory gates (fresh)

| Gate | Result |
|------|--------|
| `./run_tests.sh` (LLVM) | exit 0 — Total 186 |
| `BUILD_DIR=build-parse ./run_tests.sh` | exit 0 — Total 181 |
| `./test_codegen.sh` | exit 0 — 63/63 (57 golden, 6 quarantine) |
| `BUILD_DIR=build-asan ./run_tests.sh` | exit 0 — Total 186 |
| `./test_codegen.sh --leak-check` | exit 0 — `Leaks: 0` |

## Invariants

- Normal codegen unchanged: `test_codegen.sh` (no `--emit-test-main`) is 63/63,
  `No golden: 0` — the test-mode `main`/assert changes are inert without the
  opt-in flag. The legacy `__blang_run_tests` path is retained for no-flag
  builds; `genTestMain` is emitted only for the module carrying `test{}` blocks.

No open findings. No Open Question required (test-block semantics needed no
language decision). Cleared to squash-merge.
