# Spec Audit — U5 Parser fuzzing (`013-parser-fuzzing`)

**Phase**: 2 (spec audit) · independent reviewer pass · **Gate**.
Reviewer re-derived REQ-006 (overview.md), the workplan fuzzing done-condition,
and design.md's fuzz row/interface before checking the spec, and independently
confirmed the toolchain facts (clang 14 links `-fsanitize=fuzzer`; clang builds
qcc vs LLVM-18; `Module::Parse`/`main` co-located in qcc.cpp; `Scope`/`Module`
are RefCount).

## Rubric checks

| Item | Verdict | Evidence |
|------|---------|----------|
| Covers REQ-006 | PASS | FR-001/002 (libFuzzer over lexer+parser), FR-004 (≥20 corpus), FR-005 (replay), FR-006 (poison reachability), FR-007 (bounded campaign). |
| Machine-checkable | PASS | SC-001..006 runnable (build, `wc -l`, `-runs=0` exit, poison crash, boundary). |
| design conformance | PASS | fuzz_parse built only when option ON + clang (design "built only when its CMake option is on and clang is the compiler"); corpus at `test_files/fuzz/corpus/`. |
| Teeth (no vacuous gate) | PASS | FR-006/SC-004 poison crashes a broken parser; US2 makes "provably reaches the parser" a paired break→crash→revert→clean proof — a stub can't pass. |
| Bounded (Non-goal) | PASS | FR-007 fixed `-max_total_time=60`; crashes fixed or escalated, not an open hunt. |
| Scope discipline | PASS | FR-008 opt-in, other builds unchanged; Sema out of the fuzz path. |

## Findings

**F1 (build-dir path correction — reviewed, permitted).** A libFuzzer target
requires clang; the default `build/` is gcc, so `build/fuzz_parse` cannot exist
there. The spec puts `fuzz_parse` in a dedicated clang build `build-fuzz`
(BLANG_ENABLE_LLVM=OFF, BLANG_FUZZ=ON), mirroring the accepted `build-asan`
pattern, and aligns the runnable acceptance path to `build-fuzz/fuzz_parse`.
Disposition: PERMITTED — this is a toolchain reality, not a weakening; the teeth
(corpus replay + poison proof) are unchanged. Constraint for code-audit: the path
correction must be documented in `evaluation.md` (and a note in overview/manifest
clause 5) and must NOT reduce the corpus/replay/poison requirements. Reviewer
will re-run `build-fuzz/fuzz_parse … -runs=0` itself.

**F2 (harness must not mask faults — carried).** FR-002 swallows only
`CompileError` (controlled parse errors). Code-audit verifies a real fault
(the poison against a broken parser) is NOT swallowed by `catch(...)` — i.e. the
break causes a signal/abort/ASan fault, which propagates to libFuzzer, not a C++
exception the harness eats.

**F3 (reachability authenticity — carried).** The poison must crash *because the
input bytes drove the parser* (canary keyed on the poison's content), not because
the break crashes unconditionally on any input. Code-audit confirms the poison
specifically triggers it and an unrelated input does not (with the break in).

**F4 (leak-clean replay — carried).** `-runs=0` runs each corpus input under
libFuzzer's leak detection; code-audit confirms the SmartPtr-managed AST keeps
replay exit 0 (no per-iteration leak accumulation).

No blocking findings.

## Verdict

**PASS** — implementation may proceed. Carried code-audit checks: F1 (path
correction documented + teeth intact), F2 (faults not masked), F3 (poison
authenticity), F4 (leak-clean replay).
