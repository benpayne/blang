# Spec Audit — U2 Golden migration (`010-golden-migration`)

**Phase**: 2 (spec audit, pre-implementation) · independent reviewer pass · **Gate**.
**Rubric**: evaluation.md §Audit plan "spec audit" + constitution Audit Pattern.

Reviewer re-derived REQ-002 (overview.md), the U2 done-condition (workplan §U2),
design.md D2/D3 + the approved quarantine list before checking the spec.

## Rubric checks

| Item | Verdict | Evidence |
|------|---------|----------|
| Covers REQ-002 | PASS | FR-001 (golden per deterministic test, ≥55), FR-003 (quarantine = approved set). |
| Machine-checkable acceptance | PASS | SC-001 `wc -l ≥55`; SC-002 empty diff; SC-003 `No golden: 0`; SC-004 selfcheck; SC-005 flip; SC-006 186/181. All runnable. |
| Consistent with U2 done-when | PASS | workplan §U2 done: ≥55 goldens, quarantine diff-equals approved, `./test_codegen.sh` exit 0, reviewer ≥5-golden correctness spot-check → FR-001/003/004 + SC-001/002/003/007. |
| design.md D2 (exact match + explicit quarantine, no widening) | PASS | FR-003 freezes the 6; US2 scenario 2 gives the anti-widening teeth. |
| design.md D3 (all deterministic tests goldened) | PASS | FR-001/FR-002 (0 remaining NO GOLDEN among non-quarantined). |
| Known-answer verification, surface bugs not paper over | PASS | FR-004 + US3; explicit "never snapshotted as the golden". |
| Non-determinism handled honestly | PASS | FR-005: repeated-run determinism check; a 7th non-det test → Open Question, never silent quarantine (design.md D2 + user directive). |
| No scope creep | PASS | FR-006 limits change to data + freeze + one acceptance-command correction; no CI (U7). |

## Findings

**F1 (finding — reviewed and permitted, must be executed transparently).**
FR-006 authorizes correcting the quarantine-diff command in `evaluation.md`. The
reviewer independently re-ran the LITERAL command and confirms it is broken: it
strips comments only on the left operand, so `approved_quarantine.txt`'s 6 comment
lines leak into the right operand and the diff is non-empty **even for a correct
6-name list**. The symmetric form (strip comments/blanks on both sides — the
semantics BOTH files explicitly state: "comments/blank lines ignored") yields an
empty diff.
- *Disposition*: PERMITTED. This is a typo-level correction that makes the
  acceptance command match its own documented semantics; it is **not** a rubric
  weakening — widening the quarantine (a 7th name on the left) still produces a
  non-empty diff, so the anti-nullification teeth are fully preserved. evaluation.md
  line 122 explicitly assigns U2 responsibility for making this diff checkable.
- *Required transparency*: the correction MUST be (i) minimal (add the same
  `grep -vE '^\s*#|^\s*$'` to the right operand only), (ii) logged in the status
  log, and (iii) surfaced in the U2 report so the epic owner can object. It MUST
  NOT alter approved_quarantine.txt's contents or the 6-name set. The reviewer
  will re-verify at code-audit that the corrected command is symmetric and still
  catches widening.

**F2 (carried to code-audit).** FR-005 determinism: several non-quarantined tests
use threads/async (`codegen_async`, `codegen_async_multi`, `codegen_spawn`,
`codegen_sync_spawn`, `codegen_shared_spawn`, `codegen_shared_lambda`,
`codegen_sync_locking`, `codegen_wait`, `codegen_wait_all`). The reviewer will
require evidence (repeated-run stability) that their goldens are stable; any flaky
one must be an Open Question, not a silent quarantine addition.

No blocking findings.

## Verdict

**PASS** — implementation may proceed. Two carried-forward code-audit checks:
F1 (symmetric diff correction is minimal, teeth-preserving, transparently logged)
and F2 (thread/async goldens proven stable across repeated runs, else Open Question).
