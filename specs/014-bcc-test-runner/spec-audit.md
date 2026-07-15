# Spec Audit — U6 (014-bcc-test-runner)

**Auditor role**: secondary reviewer (independent pass; distinct from the
implement pass). **Rubric**: `evaluation.md` §Audit plan → spec audit row.
**Verdict**: **PASS with carried findings** (F3, F5 must be honored in
implementation; verified again at code audit).

## Rubric checks

1. **Covers REQ-003 with machine-checkable acceptance.** ✅ FR-001..007 map to
   every clause of REQ-003 (compile+run test blocks, per-test PASS/FAIL, total
   count, `--filter`, `file:line` on failure, exit non-zero iff any fail). SC-001..003
   are byte-for-byte the epic done-condition clause 2 commands; SC-004 adds an
   isolation teeth-proof beyond the minimum. Consistent with the unit done-when
   in `workplan.md` U3.
2. **Conforms to design.md.** ✅ D4 (reuse `genTestBlock` + AST `SourceLocation`)
   honored; the legacy `genTestRunner` is retained for the no-flag path and a new
   `genTestMain` is added only under the opt-in flag — acceptable because
   `genTestRunner` as-is has no counting/isolation and cannot satisfy the done
   condition. D7 (no external test framework) honored (libc-only driver).
   Invariant "normal build codegen unchanged" honored via opt-in flag + SC-006.
3. **No scope creep.** ✅ Non-goals fence off parallelism, timeouts, discovery
   redesign, and concurrency-under-fork.
4. **Tasks include required fixtures/tests.** ✅ Task 7 commits the two fixtures;
   SC-005 pins the four non-regression suites.

## Findings

- **F1 (accepted, note only).** FR-002 phrases the located line as
  `^[^:]*<file>:<line>:` while SC-002 uses the done-condition's unanchored
  `[^:]+\.b:[0-9]+:`. Both are satisfied by a line of the form
  `test_files/testblock/has_failure.b:<line>: assertion failed: …` because the
  path contains no `:` before `.b`. No change required; keep the emitted path
  colon-free (it is).

- **F3 (MUST address in implementation).** Per the U4 lesson (colored output
  broke `grep -E 'Leaks:[[:space:]]*0'`), the test-runner **summary and per-test
  lines must be plain ASCII with no ANSI escapes on non-TTY/piped output**, so
  `grep -oE '[0-9]+ passed'` and `grep -Eq 'FAIL'` match under `| grep`. The
  driver should not color the summary (or gate color on `isatty`). Verified at
  code audit against piped output.

- **F5 (MUST address in fixture design).** For SC-003 to hold as a *strict*
  subset, `all_pass.b` must contain **at least one passing test whose name does
  not contain the substring `add_two`** (so `filt < full`) and **at least one
  whose name contains `add_two`** (so `filt > 0`). State this in the fixture and
  re-check the counts at code audit (e.g. full ≥ 3, filt == 1).

- **F6 (note).** `bcc test` must forward `--filter <name>` to the compiled
  binary and still correctly identify the `.b` file argument regardless of
  argument order. Confirm arg parsing handles `bcc test --filter add_two
  <file>` and `bcc test <file> --filter add_two`.

## Disposition

Spec is sound and machine-checkable. Proceed to implementation; F3/F5 are
binding acceptance conditions re-verified in the Phase-4 code audit.
