# Specification Quality Checklist: Bounded parser fuzzing

**Created**: 2026-07-14 · **Feature**: [spec.md](../spec.md)

## Content Quality
- [x] Behavioral contracts (build, corpus replay, poison teeth); files named are the SUT
- [x] Focused value: fuzz the hand-written parser for crashes/UB
- [x] Mandatory sections complete

## Requirement Completeness
- [x] No [NEEDS CLARIFICATION] markers
- [x] Requirements testable (FR-001..009 → SC-001..006)
- [x] Success criteria measurable/runnable
- [x] Acceptance scenarios + edge cases (clang-only, LLVM-off, per-iter leaks, error≠crash, build-dir path)
- [x] Scope bounded (build + fixed campaign + fix-or-escalate; no open-ended hunt)
- [x] Dependencies/assumptions identified

## REQ / design conformance
- [x] Covers REQ-006 (libFuzzer over lexer+parser; ≥20 corpus; bounded campaign; poison reachability)
- [x] design: fuzz_parse built only when option ON and clang is the compiler; corpus at test_files/fuzz/corpus/
- [x] Teeth: poison-input crashes a broken parser (FR-006/SC-004) — no stub target passes
- [x] Opt-in, off by default; default/other builds unchanged (FR-008)
- [x] Bounded (Non-goal): fixed -max_total_time=60; crashes fixed or escalated

## Notes
- Build-dir path correction (build/ gcc cannot host libFuzzer) audited below —
  same pattern as build-asan; teeth (corpus replay + poison) unchanged.
