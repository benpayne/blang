# Specification Quality Checklist: Runtime unit tests + leak-check teeth

**Created**: 2026-07-14 · **Feature**: [spec.md](../spec.md)

## Content Quality
- [x] Behavioral contracts (ctest count, exit codes, teeth); files named are the SUT
- [x] Focused value: directly test the runtime; make the leak gate real
- [x] Mandatory sections complete

## Requirement Completeness
- [x] No [NEEDS CLARIFICATION] markers
- [x] Requirements testable (FR-001..009 → SC-001..007)
- [x] Success criteria measurable/runnable
- [x] Acceptance scenarios + edge cases (LSan-on-threads, bounds-under-ASan, both build dirs)
- [x] Scope bounded; ARC-leak fix escalated (FR-008) not attempted
- [x] Dependencies/assumptions identified

## REQ / design conformance
- [x] Covers REQ-005 (runtime units, ≥30, ASan) + REQ-004 leak-check teeth
- [x] No external test framework (design D7) — tiny assert harness
- [x] Reuse existing ASan/LSan machinery (design D5) — leak fatal fix, injected fixture
- [x] Bounds-removal teeth (FR-004/SC-004)
- [x] Real leaks surfaced → fix-or-track-with-justification (workplan U4); 4 tracked + Open Question

## Notes
- Leak-quarantine is the highest-risk element; audited for teeth below (must be
  minimal, justified, non-widenable, and never suppress new/injected leaks).
