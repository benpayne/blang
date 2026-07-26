# Specification Quality Checklist: Semantic Pass Skeleton (U3)

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-13
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
  - Note: file:line references appear only in Context to ground the current-state
    gaps (per the epic's audit-evidence convention, matching U1/U2 specs); the
    requirements and success criteria are outcome-based.
- [x] Focused on user value and business needs (correct rejection of unknown names; a shared type foundation)
- [x] Written for stakeholders (compiler maintainers / language users)
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable (SC-001..007, machine-checkable)
- [x] Success criteria are technology-agnostic where the outcome allows (build-mode gates named because they are the epic's fixed verification harness)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified (double-report, builtins, chained access, deferred-check expressions, combine/.bmod, generics)
- [x] Scope is clearly bounded (FR-016/Out of Scope: no U4–U7 checks; no audit_NN.b files)
- [x] Dependencies and assumptions identified (U1 locations, U2 engine+harness, scope model)

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows (resolution rejection; sema-runs-everywhere/typed-AST; fail/sema harness)
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into normative requirements

## Requirement → REQ-ID traceability (epic)

- REQ-004 (sema pass in all build modes) → FR-001, FR-002, FR-015; US2; SC-002
- REQ-006 (name/member resolution) → FR-004..009; US1; SC-001
- Design decision 3 (typed AST) → FR-010, FR-011, FR-012; US2; SC-006
- REQ-011 harness reuse / done-condition #3 groundwork → FR-013, FR-014; US3; SC-003, SC-004
- Scope/migration guardrails → FR-016, FR-017, FR-018; SC-005, SC-007

## Notes

- Items marked incomplete require spec updates before `/speckit-plan`. All items pass.
- Deliberately excludes the numbered audit_NN.b programs (assigned to U4/U5/U7 in design.md);
  U3 builds only the fail/sema/ category + canonical-format harness enforcement they use.
