# Specification Quality Checklist: Golden migration

**Created**: 2026-07-14 · **Feature**: [spec.md](../spec.md)

## Content Quality
- [x] No implementation-detail leakage (golden data + list freeze are behavioral)
- [x] Focused on value: no deterministic test passes with wrong output; quarantine can't be widened
- [x] Mandatory sections complete

## Requirement Completeness
- [x] No [NEEDS CLARIFICATION] markers
- [x] Requirements testable/unambiguous (FR-001..007 → SC-001..007)
- [x] Success criteria measurable and runnable
- [x] Acceptance scenarios defined; edge cases (non-det-not-on-list, empty output, env-dependent) identified
- [x] Scope bounded (data + freeze + one acceptance-command correction; no harness logic change; no CI)
- [x] Dependencies/assumptions identified

## REQ / design.md conformance (epic-specific)
- [x] Covers REQ-002 (goldens for all deterministic tests; quarantine = fixed approved list)
- [x] ≥ 55 goldens (SC-001); quarantine diff-equals approved set (SC-002, symmetric)
- [x] Verified known-answers, not blind snapshots (FR-004/SC-007); wrong output surfaced not papered (US3)
- [x] Non-determinism → Open Question, never silent quarantine (FR-005; design.md D2)
- [x] U1 harness teeth preserved (FR-006/SC-004)

## Notes
- FR-006 permits a documented, teeth-preserving correction of the asymmetric
  quarantine-diff command in evaluation.md (empirically confirmed broken). Audited below.
