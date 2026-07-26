# Spec Audit — U1 Golden-output harness (`009-golden-harness`)

**Phase**: 2 (spec audit, pre-implementation) · **Reviewer pass**: independent of
authoring · **Gate**: blocks implementation until PASS
**Rubric**: evaluation.md §Audit plan "spec audit" row + constitution Audit Pattern.

The reviewer re-derived REQ-001 (overview.md:88), the U1 done-condition
(workplan.md §U1), and design decisions D1/D2 + the normalization rule directly
from source before checking the spec — not from the author's summary.

## Rubric checks

| Rubric item | Verdict | Evidence |
|-------------|---------|----------|
| Covers the unit's REQ IDs (REQ-001) | PASS | FR-001 (compare stdout to `test_files/<name>.expected.out`, diff on mismatch), FR-004 (`--selfcheck` teeth). Maps 1:1 to REQ-001 text. |
| Machine-checkable acceptance criteria | PASS | SC-002 gives literal runnable checks (`--selfcheck; test $? -ne 0` + `grep -q 'SELFCHECK: OK'`); SC-003 the green→red flip; SC-001/006 the green-baseline. All executable, no prose-only criteria. |
| Consistent with the unit's done-when | PASS | Done-condition's three clauses — real paired selfcheck (temp-copy corrupt, red, `SELFCHECK: OK`, non-zero); default green with sample golden; scripted wrong-output flip; all suites green — appear as FR-004 + SC-002 / FR-009+SC-001 / SC-003 / SC-006 respectively. |
| Conforms to design.md D1 (extend script, no new runner) | PASS | FR-011 limits change set to `test_codegen.sh` + quarantine file + sample golden. |
| Conforms to design.md D2 (exact match + explicit quarantine, only-normalization=1 trailing newline) | PASS | FR-002 (exact match; single trailing newline the ONLY transform; loose/substring/regex/whitespace/locale all forbidden); FR-006/007 (quarantine file, skip-compare-still-run). Edge-cases pin the `foo\n\n` vs `foo` non-match. |
| Golden path contract | PASS | FR-001 uses `test_files/<name>.expected.out` verbatim from design §Interfaces. |
| `--update-goldens` present, never touches quarantined | PASS | FR-005 + US4 scenario 2. |
| No scope creep | PASS | FR-011 + Assumptions explicitly exclude U2 (all-63 goldens, approved quarantine contents), U4 (leak-check teeth), U7 (CI). FR-010 forbids changing leak semantics. |
| Required new tests/fixtures enumerated | PASS | FR-009 (≥1 sample golden), FR-006/007 (`codegen_quarantine.txt`), SC-003 (scripted wrong-output demo). These are the deliverable fixtures for U1. |
| Teeth guaranteed at the spec level (no trivially-passing gate) | PASS | US2 scenario 3 requires a passing vs broken comparator to be *distinguishable*; FR-004 forbids `SELFCHECK: OK` unless the corruption was actually detected — closes the "selfcheck is just `exit 1`" hole the done-condition warns against. |

## Findings

**F1 (observation, disposed — not a blocker).** FR-008 missing-golden policy.
At U1 only sample golden(s) exist but 63 tests run; if "no golden for a
non-quarantined test" were fatal, the suite would go red and violate "all suites
green." The spec resolves this by reporting a missing golden as a **visible,
non-fatal** status (`NO GOLDEN`) that is still exit-code-checked, and documents
it in Assumptions.
- *Why acceptable*: it does not create a teeth-hole — once a golden **exists**,
  wrong output fails (FR-001/US1). The non-fatal window is closed by U2, whose
  own done-condition (≥55 goldens; quarantine diff-equals the approved list)
  forces every non-quarantined deterministic test to be goldened. The status is
  *visible*, not silent, so the gap is auditable, not hidden.
- *Disposition*: accepted as the correct U1-scoped behavior. The reviewer will
  re-verify at U1 code-audit that missing-golden is `NO GOLDEN`-visible (not a
  silent PASS) and that a *present* golden with wrong output is fatal.

**F2 (nit, non-blocking).** SC-006 pins exact baseline counts (186/181). These
are correct as of the launch baseline just recorded, but are environment-
sensitive (parse-suite count is unrelated to U1's change). Acceptable as a
regression tripwire; the operative requirement is "stays green," which the
counts make concrete.

No blocking findings. No `[NEEDS CLARIFICATION]` markers remain.

## Verdict

**PASS** — spec is machine-checkable, consistent with the U1 done-condition,
conforms to design.md D1/D2 and the single-trailing-newline normalization rule,
scopes out U2/U4/U7 cleanly, and guards the comparator's teeth at the spec level.
Implementation (Phase 3) may proceed. The one observation (F1) is carried forward
as an explicit code-audit check.
