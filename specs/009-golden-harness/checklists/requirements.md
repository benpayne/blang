# Specification Quality Checklist: Golden-output harness

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-14
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *harness contracts
  are behavioral; the one shell file named is the system under change, not an impl leak*
- [x] Focused on user value and business needs — *catch wrong output; prove teeth*
- [x] Written for non-technical stakeholders — *readable acceptance scenarios*
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (or expressed as exact runnable checks the epic requires)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified (empty stdout, trailing newline, selfcheck-with-zero-goldens, quarantined-and-goldened)
- [x] Scope is clearly bounded (U1 = harness + ≥1 sample golden + quarantine file; U2/U4/U7 explicitly excluded)
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria (FR-001..011 → SC-001..006 + scenarios)
- [x] User scenarios cover primary flows (wrong-output catch, selfcheck teeth, quarantine, update-goldens)
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## REQ / design.md conformance (epic-specific)

- [x] Covers REQ-001 (stdout comparison + `--selfcheck` teeth)
- [x] Golden path `test_files/<name>.expected.out`, exact match (design.md D1/D2)
- [x] Only permitted normalization = strip one trailing newline (design.md D2) — FR-002, edge cases
- [x] Quarantine mechanism `test_files/codegen_quarantine.txt`, skip-compare-still-run (design.md D2) — FR-006/007
- [x] `--selfcheck` is a real paired teeth-proof (corrupt real golden in temp copy, red, `SELFCHECK: OK`, non-zero) — FR-004
- [x] `--update-goldens` present, never touches quarantined tests — FR-005
- [x] No loose/substring/regex matching — FR-002
- [x] No scope creep: all 63 goldens (U2), leak-check teeth (U4), CI (U7) excluded — FR-011 + Assumptions

## Notes

- Items marked incomplete require spec updates before `/speckit-plan`. All pass.
