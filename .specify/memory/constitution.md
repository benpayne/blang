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
running binary. For reject-only checks (diagnostics whose purpose is that the
program never compiles), a negative test with an expected-message pattern in
`test_files/fail/` or `cgfail/` satisfies the end-to-end requirement in lieu
of a `codegen_*.b` test. Bug fixes add a regression test that fails without
the fix.

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

### VI. Principled Design (State of the Art)
BLang aims to be a production-ready, state-of-the-art language whose primary
audience is AI/LLM code generation: a regular grammar, a small keyword set, one
canonical way to express each concept, and diagnostics precise enough that a
model can self-correct from them. Design work is therefore a first-class
deliverable, not an afterthought. Every work unit that changes language
semantics, the type system, the runtime, or the compiler architecture MUST be
grounded in a written design artifact (a speckit `spec.md`/`plan.md`, or a
design note under the epic's docs) BEFORE implementation begins. Designs favor
robust, general solutions over point fixes and are chosen with an explicit view
to long-term maintainability — advanced, modern compiler-engineering practice
(clear phase separation, well-defined IR/AST contracts, semantic checks in all
build modes) is the expected default. Phased delivery is allowed; shipping a
narrow slice of a robust design is fine, shipping a fragile design is not. A
deliberate simplification MUST be recorded in Known Issues with its rationale.

## Quality Gates

A change is mergeable only when all of the following hold:
1. Builds clean in both configurations (parse-only and with LLVM).
2. `./run_tests.sh` fully green; `./test_codegen.sh` fully green.
3. Required new tests (Principle II) are present and pass.
4. Runtime/ARC changes pass `--leak-check` (Principle IV).
5. Documentation updated (Principle I).
6. The change traces to an approved design artifact (Principle VI); the
   reviewer confirms the implementation matches the design or records the
   divergence.
7. Security- and maintainability-relevant changes (untrusted input handling —
   lexer/parser, file/network/DB/FFI boundaries, subprocess invocation,
   `unsafe`/raw-pointer or memory-management code) clear the security and
   maintainability audit dimensions below with no unresolved finding.

## Audit Pattern

All work — code AND design — lands through review; nothing is committed
directly to `master`. Verification is performed by a secondary reviewer hire,
distinct from the implementing hire, acting at the devbot manager's direction.

**Design audit (before implementation).** For any unit covered by Principle VI,
the design artifact (`spec.md`/`plan.md` or design note) is reviewed before code
is written — for robustness (does the design handle the general case and its
failure modes?), security (does it widen an attack surface, mishandle untrusted
input, or weaken a safety guarantee?), and long-term maintainability (does it
fit the existing phase/architecture, or accrete complexity?). speckit
`/speckit-clarify` and `/speckit-analyze` are the tools for this pass.
Design findings are resolved before implementation starts.

**Code audit (before merge).**

1. The implementing hire opens a PR when its work unit is complete, with the
   Quality Gates above already green.
2. The secondary reviewer hire performs a full code review of the PR across
   these dimensions, each an explicit pass/finding:
   - **Correctness** — behavior matches the spec and design.
   - **Tests & diagnostics quality** — Principle II coverage; diagnostics point
     at user source (Principle III).
   - **Security** — untrusted-input handling (lexer/parser, file/net/DB/FFI,
     subprocess), memory/thread safety at trust boundaries, no injection or
     unchecked-buffer paths. When the change touches a security-relevant
     boundary, this pass is mandatory and recorded.
   - **Long-term maintainability & robustness** — general over point fixes, fits
     the architecture, no unjustified complexity or duplicated logic.
   - **Style & spec fidelity** — house style (Principle V); spec/docs updated
     (Principle I).
3. Every review finding (design or code) is resolved — fixed or explicitly
   dispositioned with rationale — before the PR advances. Unresolved findings
   block the merge.
4. Once all findings are resolved and gates are green, the reviewer hire
   merges the PR to `master`.

The devbot manager directs this cycle end-to-end and is responsible for
ensuring no work unit is marked done until its design was audited and its PR is
merged.

## Governance

This constitution supersedes other practices for devbot-directed work.
Amendments are made by PR to this file with the user's approval and take
effect on merge. Where this document is silent, `CLAUDE.md` governs.

**Version**: 1.2.0 | **Ratified**: 2026-07-13 | **Last Amended**: 2026-08-04
(1.2.0: added Principle VI (Principled Design — state-of-the-art design as a
first-class, audited deliverable); Audit Pattern now runs an explicit design
audit before implementation and names security and long-term
maintainability/robustness as required code-audit dimensions; Quality Gates 6–7
added. Approved by Ben Payne during devbot-init.
1.1.0: Principle II — negative expected-message tests satisfy the E2E
requirement for reject-only checks; approved by Ben Payne during blang-ast
review.)
