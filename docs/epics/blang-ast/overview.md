# Epic: blang-ast — True Language Enforcement via Semantic Analysis

**Status**: planning
**Archetype**: evolve (restructuring an existing, working compiler)
**Docs**: this directory (`docs/epics/blang-ast/`)

## Problem

BLang today has no semantic-analysis pass. The pipeline is lexer → parser →
LLVM codegen, and every "check" is a side effect of codegen: gated behind
`BLANG_HAS_LLVM`, reported without source locations, and in most cases
implemented as a silent coercion rather than a rejection. Consequences,
verified empirically (2026-07-13 production-readiness audit):

- `string s = 42;` compiles cleanly and corrupts the heap at runtime.
- `int x = add(1, "hello");` is caught only by LLVM's IR verifier, whose raw
  output is shown to the user.
- Return-type mismatches fabricate values (`inttoptr`, zeroed structs);
  mismatched initializers are silently dropped, leaving variables
  uninitialized; unknown fields/methods and unhandled AST nodes vanish
  silently (`CGStruct.cpp:968,994`, `CodeGen.cpp:1118`).
- The AST carries no source locations, the lexer tracks no column or
  filename, and codegen-time errors (including "use of moved variable")
  have no location at all.
- The language's safety claims are unenforced: `shared` structs are mutable
  through fields, `sync` field access is unlocked, plain heap values cross
  `spawn` boundaries unguarded, and generic constraints (`T: Comparable`)
  are decorative.

Until wrong programs are reliably rejected with a diagnostic pointing at the
user's code, no feature work moves BLang toward production quality. This epic
fills that gap.

## Goal

Introduce a dedicated semantic-analysis pass over a location-bearing AST that
enforces the language — types, ownership, and concurrency rules — in all
build modes, with clean, located error messages, and migrate the existing
test suites, stdlib, and demos to be correct under the new checker.

## Requirements

| ID | Requirement |
|----|-------------|
| REQ-001 | Every AST node carries a source location (file, line, column). The lexer tracks column (currently dead — `charPos` never incremented) and retains the source filename. |
| REQ-002 | All user-facing compile errors are formatted `<file>:<line>:<col>: error: <message>`, include the offending token text where applicable, and never expose compiler-internal C++ `__FILE__:__LINE__` locations or raw LLVM verifier output. LLVM verifier failure is reported as an internal compiler error with a report request. |
| REQ-003 | Compiles are quiet by default: no per-token dumps, `[TRACE]` lines, or AST dumps on stdout/stderr unless a verbosity flag is passed. |
| REQ-004 | A semantic-analysis pass runs between parsing and codegen in ALL build modes, including `--parse-only` and non-LLVM builds. Semantic errors do not require LLVM to be detected. |
| REQ-005 | Type checking enforced: call arity and argument types, assignment and initializer compatibility, binary/unary operand validity, return-path correctness (type match, `return;` in non-void, missing return). Only documented implicit conversions (integer width promotion) are permitted; all other mismatches are errors — no value fabrication, no dropped initializers. |
| REQ-006 | Name and member resolution is enforced: undefined variables, functions, fields, and methods produce located errors; nothing resolves to a silent `nullptr`. |
| REQ-007 | `match` exhaustiveness is enforced (all variants covered or a `_` arm present), and `match` used as an expression produces a value of a checked, consistent type. |
| REQ-008 | Generic constraints are enforced at instantiation: using `T: Protocol` with a type argument that does not implement `Protocol` is a located error. |
| REQ-009 | Ownership/move analysis is correct and lives in the semantic pass: reassignment clears moved state, moves through fields/elements are tracked, branch merges are handled without spurious errors, and use-after-move is reported with the location of both the use and the move. |
| REQ-010 | Concurrency safety is enforced: field assignment through `shared` values is a compile error; `sync` field reads/writes acquire the lock in codegen; capturing a non-`own` heap value (string/Array/struct) in `spawn` requires it to be `shared` or `sync`, otherwise a compile error. |
| REQ-011 | Negative tests assert diagnostic content, not just exit codes: the test harness supports expected-error matching, and a `sema_*` negative suite encodes every soundness hole from the audit (minimum: the 10 documented miscompile/crash programs) plus each new diagnostic added by this epic. |
| REQ-012 | Codegen is hardened: an unhandled AST node type or ill-typed input reaching codegen is a loud internal compiler error, never a silent skip. |
| REQ-013 | Strict migration: `test_files/`, `stdlib/*.b`, and `demos/*.b` are made type-correct under the new checker; `./run_tests.sh` and `./test_codegen.sh` pass fully in both build modes. |

## Done condition (epic level, machine-checkable)

All of the following, on a clean checkout of the epic's final state:

1. `./run_tests.sh` exits 0 in both a parse-only build and an LLVM build,
   with expected-error assertion mode active for `fail/` and `cgfail/`.
2. `./test_codegen.sh` exits 0 on the LLVM build.
3. `test_files/fail/sema/` contains ≥ 25 negative tests (including the 10
   audit miscompile programs); for each, `qcc --parse-only` exits non-zero
   AND its stderr matches the regex `^[^:]+\.b:[0-9]+:[0-9]+: error: ` AND
   matches the per-test expected-message pattern.
4. `qcc test_files/pass/func_simple.b` (no flags) produces zero bytes on
   stdout and zero bytes on stderr other than nothing (exit 0, silent).
5. `grep -rn "inttoptr" <generated .ll for the audit return-mismatch
   programs>` is impossible because those programs no longer compile; and
   `./test_codegen.sh --leak-check` exits 0 for the concurrency test subset
   (`codegen_spawn*.b`, `codegen_sync*.b`, `codegen_shared*.b`).

## Execution model

Every unit (U1–U8, see `workplan.md`) moves through a five-phase lifecycle,
with the devbot manager gating each transition:

1. **Create spec** — implementing hire, on a fresh `epic/blang-ast/uN-<slug>`
   branch, runs the speckit ceremony (`/speckit-specify` → `/speckit-clarify`
   as needed → `/speckit-plan` → `/speckit-tasks`), commits the
   `specs/<NNN>-<slug>/` artifacts, opens a draft PR. No code yet.
2. **Audit spec** — secondary reviewer hire audits the spec against the
   rubric in `evaluation.md`; findings resolved before implementation is
   authorized.
3. **Implement** — implementing hire runs `/speckit-implement` on the same
   branch and passes the per-unit gates locally.
4. **Audit code** — reviewer independently re-runs the gates and reviews the
   PR against the spec; all findings resolved.
5. **Merge** — reviewer rebases onto `master`, squash-merges, deletes the
   branch. No direct commits to `master`, ever.

Git mechanics, parallel-unit rules (U5–U7), and merge serialization are
specified in `workplan.md` §Unit lifecycle and `manifest.yaml` §execution.

**Pre-launch prerequisite:** the epic assumes unit branches cut from a clean,
committed `master`. The repo currently carries substantial uncommitted work
(new CG*.cpp split, fs/io/buffer stdlib, etc.) — that must be committed (or
stashed deliberately) before `/devbot-launch`, or U1's baseline is undefined.

## Assumptions (recorded, not asked)

- Pre-1.0 language: breaking changes to accepted programs are acceptable and
  expected ("strict" migration chosen by the user, 2026-07-13).
- Multi-error reporting, caret snippets, `--json` diagnostics, and a warning
  system are explicitly OUT of scope — deferred to a follow-up diagnostics
  epic. This epic delivers correct single-error reporting with locations.
- Integer width promotion (existing documented behavior) remains the only
  implicit conversion.
- The legacy Bison/Flex path (`parser.yy`, `parse_helpers.cpp`) is untouched
  dead code and gets no semantic pass.
- Channel send/recv syntax, stdlib growth, optimization, and debug info are
  separate epics.

## Source documents

- Production-readiness audit — conversation record, 2026-07-13, summarized
  in `design.md` §Context (with file:line evidence).
- `docs/language_design.md` — the language rules being enforced.
- `CLAUDE.md` — conventions, test-suite layout, known issues.
- `.specify/memory/constitution.md` — quality gates and audit pattern.

## Companion documents

| File | Purpose |
|------|---------|
| `manifest.yaml` | Machine-readable epic manifest for devbot (done condition, limits). |
| `workplan.md` | 8 units, dependency map, per-unit done conditions. |
| `design.md` | Design spec: target architecture, seams, what must not break. |
| `evaluation.md` | Audit pattern instantiated: exact commands, gates, review rubric. |
