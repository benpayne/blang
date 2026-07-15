# Specification Quality Checklist: CI integration, demos gate, and close-out (U7)

**Purpose**: Validate specification completeness and quality before proceeding to spec audit / planning
**Created**: 2026-07-15
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details beyond what the unit is (CI is inherently config; jobs name scripts/flags the unit must wire — appropriate for an infra unit)
- [x] Focused on the value: every new check is enforced by CI so regressions are caught
- [x] Written for the reviewer/manager audience (this is an infra/close-out unit)
- [x] All mandatory sections completed (Problem, Scope, FR, SC, Design notes, Tasks, Traceability)

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous (each FR maps to a runnable SC)
- [x] Success criteria are measurable (exit codes, greps, counts — instantiate evaluation.md)
- [x] Success criteria are verifiable without reading the implementation (commands, not internals)
- [x] All acceptance scenarios are defined (SC-001..SC-008)
- [x] Edge cases identified (selfcheck must go red; injected leak must be fatal; fuzz campaign time-bounded)
- [x] Scope is clearly bounded (in/out scope; no gate-semantics changes; branch-protection is manual-only)
- [x] Dependencies and assumptions identified (depends on U1–U6+U8 merged; opt-in build dirs)

## Feature Readiness

- [x] Every FR has clear acceptance criteria (traceability table FR→REQ→SC)
- [x] Scenarios cover the primary flow (all 6 CI jobs + docs close-out)
- [x] Feature meets measurable outcomes (SC-006 = actual CI green on merged commit, not a grep)
- [x] No gate-weakening: jobs execute real checks with CI-layer teeth

## Notes

- SC-006 is the decisive done-condition: `gh run list --branch master` conclusion
  == `success` on the epic's final commit, per overview.md #6 / evaluation.md
  clause 6. A grep of `ci.yml` text is explicitly insufficient.
- The quarantine-diff and golden-floor (SC-001) and leak/ctest/fuzz clauses
  (SC-003..SC-005) mirror `evaluation.md` §"Epic-level acceptance" verbatim so
  the spec audit can confirm consistency with the epic done-condition.
- Branch-protection "required checks" is documented (FR-009/SC-007) but not
  automated — it is a GitHub repo setting outside version control.
