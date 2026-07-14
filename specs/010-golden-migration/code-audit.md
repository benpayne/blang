# Code Audit — U2 Golden migration (`010-golden-migration`)

**Phase**: 4 (code audit, pre-merge) · independent reviewer pass; every check
re-run **fresh** after a clean rebuild from the committed tree. **Gate**.
**Rubric**: evaluation.md §Per-unit gates + §Audit plan "code review" +
constitution Quality Gates & Audit Pattern.

## SC / done-condition verification (re-run by reviewer)

| Check | Result | Evidence |
|-------|--------|----------|
| (a) SC-001 golden count ≥ 55 | **PASS** | `ls test_files/codegen_*.expected.out \| wc -l` = **57**. |
| (b) SC-002 quarantine diff empty | **PASS** | symmetric diff (comments/blanks stripped both sides) of `codegen_quarantine.txt` vs `approved_quarantine.txt` is empty — exactly the 6 approved names. |
| (c) SC-003 all compared, green | **PASS** | `./test_codegen.sh` exit 0; `Golden-checked: 57  No golden: 0  Quarantined: 6  Failed: 0`. |
| (d) SC-004 selfcheck teeth | **PASS** | `./test_codegen.sh --selfcheck` exit **1** + emits `SELFCHECK: OK`. |
| (e) SC-005 wrong-output flip | **PASS** | corrupting `codegen_enum_payload.expected.out` → suite exit 1; revert → exit 0. |
| (f) SC-006 parse suites green | **PASS** | LLVM **186/186**, parse-only **181/181** (no regression from the Sema fix). |
| SC-007 ≥5 known-answer spot-checks | **PASS** | 8 verified vs source intent: binexpr(z=80), result_type(division by zero…), forin, enum_payload, try_operator, comprehensive, pipeline, generic_fn(hello…). |

## Teeth verification (epic: no trivially-passing gate)

- **Anti-widening** (design.md D2 / US2-2): appending a 7th name
  (`codegen_array.b`) to `codegen_quarantine.txt` makes the corrected diff
  **non-empty** → widening is caught. The FR-006 symmetric-strip correction did
  NOT weaken this. ✅
- **Wrong-output fatal on the normal path**: (e) above. ✅
- **Bug-fix regression teeth**: reverting `Sema.cpp` to the pre-fix (`master`)
  version and rebuilding makes `codegen_result_type.b` **FAIL** (garbage ≠ correct
  golden); restoring the fix → PASS. The fix is genuine and the test guards it. ✅
- **Determinism** (FR-005 / F2): full suite stable across 3 runs; the
  thread/async subset (spawn, sync_spawn, shared_spawn, shared_lambda,
  sync_locking, async, async_multi, wait, wait_all) stable across **10/10** runs.
  No non-deterministic test exists outside the approved 6 → no Open Question. ✅

## Bug surfaced and fixed (the point of the epic)

`codegen_result_type.b` printed **nondeterministic garbage bytes** before its
final line — an uninitialized/mis-typed read, not flakiness. Root cause: a match
destructuring binding (`err(msg)` where `msg: string`) kept the parser's
placeholder `var` type (the subject enum is unknown at parse time), so
`isStringType(msg)` was false and `puts(msg)` received the `BlangString` object
pointer instead of its `.data` cstring — the struct header printed as a C string.
Fix (Sema, all build modes per constitution III): resolve each match-arm binding
to its variant's associated type before walking the arm body. Reviewer confirms:
- correct fix layer (typed AST is Sema's responsibility; parse-only build
  unaffected, 181/181);
- surgical (post-fix regeneration changed **only** `result_type`'s golden; all 56
  other goldens byte-identical → no collateral behavior change);
- golden is the verified known-answer `division by zero\nResult type test passed!`;
- regression guard present (constitution II) — the test fails without the fix.

## FR-006 correction (transparency)

The reviewer independently confirmed the pre-existing `evaluation.md`
quarantine-diff command stripped comments on only the left operand, so a correct
6-name list produced a non-empty diff. U2's minimal fix adds the same
`grep -vE '^\s*#|^\s*$'` to the right operand (the "comments/blanks ignored"
semantics both files already state). Not a rubric weakening — anti-widening teeth
verified intact above. `approved_quarantine.txt` contents and the 6-name set are
unchanged. **Surfaced explicitly in the U2 report + status log** for the epic
owner's visibility.

## Findings

- **F1 (spec audit) RESOLVED** — symmetric-diff correction is minimal,
  teeth-preserving (anti-widening caught), and logged.
- **F2 (spec audit) RESOLVED** — thread/async goldens proven stable (10/10); no
  Open Question needed.
- No blocking findings. Tree verified clean after all adversarial probes (git
  status empty, no stash residue, `Sema.cpp` == HEAD).

## Verdict

**PASS — approved for merge.** 57 verified goldens; quarantine frozen and
widening-proof; the migration surfaced and fixed a real garbage-output bug at the
correct layer with a regression guard; suites green (186/181/63). Proceed to
Phase 5 (squash-merge).
