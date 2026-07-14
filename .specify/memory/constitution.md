# BLang Compiler Constitution

## Core Principles

### I. One Right Way (Spec Fidelity)
Every language concept has exactly one syntax. Any change to language syntax or
semantics MUST update `docs/language_design.md` and the feature lists in
`CLAUDE.md` in the same change. No feature ships in a state where the spec,
the parser, and codegen disagree without the divergence being recorded in
Known Issues.

### II. Test-Gated Changes (NON-NEGOTIABLE)
`./run_tests.sh` and `./test_codegen.sh` MUST pass before any merge. Every new
language feature adds, at minimum: positive parse tests (`test_files/pass/`),
negative tests for its failure modes (`test_files/fail/` or `cgfail/`), and an
end-to-end `codegen_*.b` test that exercises the feature in a compiled,
running binary. Bug fixes add a regression test that fails without the fix.

### III. Reject, Don't Coerce
The compiler MUST reject invalid programs with a diagnostic that points at the
user's source — never silently coerce, silently drop code, or emit IR that
fails LLVM verification. Raw LLVM verifier output, compiler-internal
`__FILE__:__LINE__` locations, and debug/trace spew are never part of normal
user-facing output. New checks are implemented as semantic analysis available
in all build modes, not only behind `BLANG_HAS_LLVM`.

### IV. Memory and Thread Safety Are Verified, Not Assumed
Changes to the runtime libraries (`runtime/*.c`) or to ARC/ownership codegen
MUST be validated with `./test_codegen.sh --leak-check` (ASan/LSan) before
merge. Safety claims made by the language (ownership, shared/sync, bounds
checks) require an enforcing check plus a test demonstrating the rejection or
the guarded behavior.

### V. House Style
C++17; tabs for indentation; spaces inside parentheses; Allman braces for
control flow; `m`-prefixed camelCase members; `Q`-prefixed parser files;
PascalCase headers. Follow the conventions in `CLAUDE.md` — new code must read
like the surrounding code.

## Quality Gates

A change is mergeable only when all of the following hold:
1. Builds clean in both configurations (parse-only and with LLVM).
2. `./run_tests.sh` fully green; `./test_codegen.sh` fully green.
3. Required new tests (Principle II) are present and pass.
4. Runtime/ARC changes pass `--leak-check` (Principle IV).
5. Documentation updated (Principle I).

## Audit Pattern

All work lands through pull requests; nothing is committed directly to
`master`. Verification is performed by a secondary reviewer hire, distinct
from the implementing hire, acting at the devbot manager's direction:

1. The implementing hire opens a PR when its work unit is complete, with the
   Quality Gates above already green.
2. The secondary reviewer hire performs a full code review of the PR
   (correctness, tests, diagnostics quality, style, spec fidelity).
3. Every review finding is resolved — fixed or explicitly dispositioned with
   rationale — before the PR advances. Unresolved findings block the merge.
4. Once all findings are resolved and gates are green, the reviewer hire
   merges the PR to `master`.

The devbot manager directs this cycle end-to-end and is responsible for
ensuring no work unit is marked done until its PR is merged.

## Governance

This constitution supersedes other practices for devbot-directed work.
Amendments are made by PR to this file with the user's approval and take
effect on merge. Where this document is silent, `CLAUDE.md` governs.

**Version**: 1.0.0 | **Ratified**: 2026-07-13 | **Last Amended**: 2026-07-13
