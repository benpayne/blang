# Specification Quality Checklist: Sanitizer build

**Created**: 2026-07-14 · **Feature**: [spec.md](../spec.md)

## Content Quality
- [x] Behavioral contracts (build option + green run); the named files are the system under change
- [x] Focused on value: memory-safety instrumentation of compiler+runtime
- [x] Mandatory sections complete

## Requirement Completeness
- [x] No [NEEDS CLARIFICATION] markers
- [x] Requirements testable (FR-001..008 → SC-001..005)
- [x] Success criteria measurable/runnable
- [x] Acceptance scenarios + edge cases (LSan noise, partial-instrumentation, UBSan non-fatal default)
- [x] Scope bounded: build option + green run; leak teeth (U4) + CI (U7) excluded
- [x] Dependencies/assumptions identified

## REQ / design.md conformance
- [x] Covers REQ-004 build portion (sanitizer build passes parse suite clean)
- [x] design.md: `BLANG_SANITIZE` comma list (address, undefined), OFF by default → normal builds unchanged (FR-001/002)
- [x] Teeth required (FR-005/SC-004): injected fault caught; UBSan made fatal
- [x] No external test deps; opt-in option
- [x] Explicit scope-out of leak teeth + CI

## Notes
- LSan-off scoping for run_tests.sh audited below (correctness gate vs leak gate separation).
