# Spec audit — U8 (ARC leak fix / REQ-008)

**Auditor role**: secondary reviewer (independence enforced by discipline —
single-hire run per OQ-2; this is a distinct pass from implementation, gates
re-run from a clean state at code-audit).
**Verdict**: PASS (proceed to implement).

## Rubric checks

1. **Covers the unit's REQ IDs with machine-checkable acceptance.** ✔ REQ-008 is
   the target. SC-001..005 are all runnable commands with exact expected output
   (`grep -vc`, `Leaks: 0`, `Known-leaks: 0`, `CLEAN`, exit codes, suite
   counts). SC-002/SC-003 match the epic done-condition #3 leak clauses and the
   U8 workplan done-when verbatim.

2. **Consistent with the unit's done-when.** ✔ Workplan U8 requires: empty leak
   quarantine (`grep -vcE ... == 0`), `--leak-check` 0-leaks with the 4 tests
   leak-free, all suites green, teeth preserved. SC-001 (empty), SC-002 (0-leaks
   + 4 CLEAN), SC-003 (teeth), SC-005 (suites) map one-to-one.

3. **Conforms to design decisions / invariants.** ✔ Honors Principle IV
   (memory-safety evidence via `--leak-check`) and the `design.md` invariant
   that default build artifacts and normal codegen output are unchanged (only
   balancing retain/release calls are added). The fix is semantic ARC, not
   behind `BLANG_HAS_LLVM`-only guards beyond existing codegen.

4. **No scope creep.** ✔ Explicitly out-of-scope: default-build changes, general
   field-assignment refcount management for non-array types, new features. The
   `__blang_sys_get_args` retain is justified in-scope as the ownership fix that
   makes the array-return ABI uniform (without it, the codegen discipline would
   regress `codegen_sys_args`).

5. **Required new tests / evidence.** ✔ No new fixtures needed — the four
   affected tests plus `leak_probe.b` (teeth) already exist; the acceptance is
   that they flip from KNOWN-LEAK/quarantined to CLEAN. Evidence = `--leak-check`
   transcript with `Leaks: 0`, `Known-leaks: 0`, and the four `CLEAN` lines.

## Carried findings for code audit

- **F1 (teeth).** Confirm the empty quarantine is real teeth: a scripted
  re-introduced leak in any of the four tests must go RED (fatal), proving the
  gate is not vacuously green because nothing is checked.
- **F2 (over-release).** Because the struct-temp tracking already exposed one
  UAF (the lambda-return path), the code audit must re-run the **entire**
  `--leak-check` suite from a clean rebuild and confirm zero ASan SEGV /
  `AddressIsPoisoned` lines — not just the four target tests.
- **F3 (ABI consistency).** Verify no other runtime function returns a borrowed
  (non-owned) `BlangArray*`; if one exists it would leak or double-free under
  the new owned-return codegen contract.
