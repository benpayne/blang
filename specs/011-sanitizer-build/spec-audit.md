# Spec Audit — U3 Sanitizer build (`011-sanitizer-build`)

**Phase**: 2 (spec audit) · independent reviewer pass · **Gate**.
Reviewer re-derived REQ-004 (overview.md), design.md D5 + the `BLANG_SANITIZE`
interface contract, and the U3 scope note before checking the spec.

## Rubric checks

| Item | Verdict | Evidence |
|------|---------|----------|
| Covers REQ-004 (build portion) | PASS | FR-001 (option), FR-003 (clean build), FR-004 (`run_tests` exit 0) = the "sanitizer build of the compiler that passes the parse suite clean" half of REQ-004. |
| Machine-checkable acceptance | PASS | SC-001 build exit 0; SC-002 `BUILD_DIR=build-asan ./run_tests.sh` exit 0; SC-005 grep `-fsanitize` on compile+link; SC-004 teeth. All runnable. |
| design.md conformance | PASS | `BLANG_SANITIZE` comma list address/undefined, OFF by default, normal build unchanged (FR-001/002, design §Interfaces "BLANG_SANITIZE CMake option ... off by default"). |
| Teeth (no trivially-passing gate) | PASS | FR-005/SC-004 require an injected fault to fail the run; UBSan made fatal (not the non-aborting default) — otherwise UB would print and pass. |
| Scope discipline | PASS | FR-007 explicitly excludes `--leak-check` teeth + injected-leak fixture (U4) and CI jobs (U7). |
| Non-determinism / bug handling | PASS | FR-008: real sanitizer-surfaced bug fixed in scope or Open Question. |

## Findings

**F1 (reviewed, permitted).** FR-006 sets default sanitizer options in
`run_tests.sh` with LeakSanitizer OFF. The reviewer accepts this as correct
scoping, not masking: `run_tests.sh` is a parse/sema correctness gate that runs
`qcc` (which links LLVM and does not free everything at exit — LSan would report
hundreds of non-bug "leaks"). Memory-leak verification is the dedicated
`test_codegen.sh --leak-check` gate on the deterministic codegen suite (U4). The
change must be a strict no-op for non-sanitizer builds (env vars ignored by
uninstrumented binaries) and must keep ASan memory-error + UBSan detection FATAL.
Reviewer will re-verify at code-audit that (a) leaks-off does not also suppress
real memory errors, and (b) `run_tests.sh` output for the normal build is
unchanged.

**F2 (carried to code-audit).** FR-005 teeth: reviewer will inject both a heap
error (ASan) and a UB (UBSan, e.g. signed overflow / OOB shift) to confirm BOTH
sanitizers are wired and fatal, then revert.

**F3 (carried).** Confirm the default build truly unchanged: grep the default
`build/` compile flags to ensure no `-fsanitize` leaked in (FR-002/SC-003).

No blocking findings.

## Verdict

**PASS** — implementation may proceed. Carried code-audit checks: F1 (leaks-off
is scoping not masking; normal-build no-op), F2 (both ASan+UBSan fatal, injected
faults caught), F3 (default build unchanged).
