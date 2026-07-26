# Spec Audit — U4 Runtime unit tests + leak-check teeth (`012-runtime-unit-tests`)

**Phase**: 2 (spec audit) · independent reviewer pass · **Gate**.
Reviewer re-derived REQ-005 + REQ-004 (overview.md), the U4/U5 workplan
done-conditions, and design D5/D7 before checking the spec, and independently
re-ran `--leak-check` to confirm the 4 surfaced leakers.

## Rubric checks

| Item | Verdict | Evidence |
|------|---------|----------|
| Covers REQ-005 | PASS | FR-001/002/003 → ctest ≥30 known-answer tests, build + build-asan. |
| Covers REQ-004 leak leg | PASS | FR-005/006/007 → leak fatal, injected fixture, clean `Leaks: 0`. |
| Machine-checkable | PASS | SC-001..007 all runnable (`ctest -N` count, exit codes, greps). |
| design D7 (no external framework) | PASS | FR-001 tiny assert harness. |
| design D5 (reuse ASan/LSan; make leak fatal) | PASS | FR-005 targeted exit fix; FR-006 injected fixture proves detection. |
| Bounds teeth | PASS | FR-004/SC-004 removal turns a test red, reverted. |
| Known-answer, not stubs | PASS | US1-3 requires value comparisons; a wrong value fails. |

## Findings

**F1 (the critical one — leak quarantine).** The spec introduces a leak
quarantine (`codegen_leak_quarantine.txt`) so `--leak-check` reports `Leaks: 0`
despite 4 genuinely-leaking tests. Risk: this is structurally identical to a gate
being nullified by quarantine. Reviewer independently reproduced the 4 leakers
(method_chain 128B pure-codegen, file_io 232B, fs_convenience 335B, http 272B),
all from `__blang_rc_alloc` — real codegen ARC gaps, large/risky to fix inside
U4. Disposition: **PERMITTED with hard constraints**, because the workplan
explicitly sanctions "quarantine with a tracked justification if out of scope"
and the alternative (risky ARC codegen surgery mid-unit) is exactly what the
guidance says to escalate rather than guess. Constraints the code-audit MUST
verify:
  1. The leak quarantine contains **exactly** the 4 identified tests — no more.
  2. Each entry has a written justification (root cause).
  3. The injected `leak_probe.b` (not quarantined) makes `--leak-check` **fatal**.
  4. A *new* leak in a non-quarantined test is **fatal** (teeth against
     regressions) — verified by a temporary injected leak in a normal test.
  5. Quarantined leakers are still **run** and shown with a distinct visible
     `KNOWN-LEAK` status, never silently dropped.
  6. An **Open Question** is filed (FR-008) recording the ARC leaks for a
     dedicated fix — this is not a permanent mask.

**F2 (bounds teeth authenticity).** The removal-teeth demo must be on the
**normal** build (under ASan the removed guard is caught by ASan, masking the
point). Spec's edge case already states this. Code-audit re-verifies.

**F3 (ctest count integrity).** SC-001 asserts the count via `ctest -N | grep -c`,
not a comment. Reviewer will confirm the count is ≥30 real tests and that they
are known-answer (spot-check several assertions), not padded exit-0 stubs.

**F4 (both build dirs).** `ctest --test-dir build-asan` must actually run the
tests under ASan (not skip). Code-audit re-runs both.

No blocking findings.

## Verdict

**PASS** — implementation may proceed. The leak quarantine is permitted strictly
under F1's six constraints + an Open Question; all other requirements are clean.
Carried code-audit checks: F1 (leak-quarantine teeth + minimality + Open
Question), F2 (bounds removal on normal build), F3 (≥30 real known-answer tests),
F4 (both build dirs run under ctest).
