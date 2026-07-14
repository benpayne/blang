# Epic: blang-ast — True Language Enforcement via Semantic Analysis

**Status**: launched
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

| ID | Requirement | Verified by |
|----|-------------|-------------|
| REQ-001 | Every AST node carries a source location (file, line, column). The lexer tracks column (currently dead — `charPos` never incremented) and retains the source filename. | U1 golden-location diff (evaluation.md §U1 check) |
| REQ-002 | All user-facing compile errors are formatted `<file>:<line>:<col>: error: <message>`, include the offending token text where applicable, and never expose compiler-internal C++ `__FILE__:__LINE__` locations or raw LLVM verifier output. LLVM verifier failure is reported as an internal compiler error with a report request. | Format regex on every `fail/` + `fail/sema/` test (done condition #3) |
| REQ-003 | Compiles are quiet by default: no per-token dumps, `[TRACE]` lines, or AST dumps on stdout/stderr unless a verbosity flag is passed. | Quiet-compile check (done condition #4) |
| REQ-004 | A semantic-analysis pass runs between parsing and codegen in ALL build modes, including `--parse-only` and non-LLVM builds. Semantic errors do not require LLVM to be detected. | Parse-only build runs `fail/sema/` suite green (done conditions #2, #3) |
| REQ-005 | Type checking enforced: call arity and argument types, assignment and initializer compatibility, binary/unary operand validity, return-path correctness (type match, `return;` in non-void, missing return). Only documented implicit conversions (integer width promotion) are permitted; all other mismatches are errors — no value fabrication, no dropped initializers. | `audit_01..05.b` rejected (design.md table); grep proxies in U4 done-when |
| REQ-006 | Name and member resolution is enforced: undefined variables, functions, fields, and methods produce located errors; nothing resolves to a silent `nullptr`. | `audit_10.b` + U3's `fail/sema/` resolution tests |
| REQ-007 | `match` exhaustiveness is enforced (all variants covered or a `_` arm present), and `match` used as an expression produces a value of a checked, consistent type. | `audit_09.b` rejected; value-producing match `codegen_*.b` E2E test |
| REQ-008 | Generic constraints are enforced at instantiation: using `T: Protocol` with a type argument that does not implement `Protocol` is a located error. | `audit_08.b` rejected in parse-only mode |
| REQ-009 | Ownership/move analysis is correct and lives in the semantic pass: reassignment clears moved state, moves through fields/elements are tracked, branch merges are handled without spurious errors, and use-after-move is reported with the location of both the use and the move. | Relocated `own_*.b` tests green in parse-only build + U6's accept/reject tests |
| REQ-010 | Concurrency safety is enforced: field assignment through `shared` values is a compile error; `sync` field reads/writes acquire the lock in codegen; capturing a non-`own` heap value (string/Array/struct) in `spawn` requires it to be `shared` or `sync`, otherwise a compile error. | `audit_06.b`, `audit_07.b` rejected; sync-RMW IR grep (U7 done-when); leak subset (done condition #5) |
| REQ-011 | Negative tests assert diagnostic content, not just exit codes: the test harness supports expected-error matching, and the `fail/sema/` suite encodes every soundness hole from the audit (minimum: the 10 audit programs) plus each new diagnostic added by this epic. | `run_tests.sh` expected-error mode (U2); count + coverage check (done condition #3) |
| REQ-012 | Codegen is hardened: an unhandled AST node type or ill-typed input reaching codegen is a loud internal compiler error, never a silent skip. | Grep proxies in U4 done-when + code-review rubric item 4 |
| REQ-013 | Strict migration: `test_files/`, `stdlib/*.b`, and `demos/*.b` are made type-correct under the new checker; `./run_tests.sh` and `./test_codegen.sh` pass fully in both build modes. | Done conditions #1 and #2 |

## Done condition (epic level, machine-checkable)

All of the following commands succeed on a clean checkout of the epic's
final state, run from the repo root:

1. LLVM build, full suites:
   `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh && ./test_codegen.sh`
   exits 0 (with expected-error assertion mode active for `fail/` and
   `cgfail/`, which is part of `run_tests.sh` from U2 onward).
2. Parse-only build, parse+sema suite:
   `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh`
   exits 0.
3. `ls test_files/fail/sema/*.b | wc -l` reports ≥ 25, and the set includes
   `audit_01.b` through `audit_10.b` per the numbered table in `design.md`
   §"The 10 audit programs". For every file in `fail/sema/`, `run_tests.sh`
   verifies: `build/qcc --parse-only <file>` exits non-zero AND stderr
   matches the regex `^[^:]+\.b:[0-9]+:[0-9]+: error: ` AND matches the
   test's own expected-message pattern.
4. `out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"`
   succeeds (quiet by default; exit 0).
5. `./test_codegen.sh --leak-check test_files/codegen_spawn*.b test_files/codegen_sync*.b test_files/codegen_shared*.b`
   exits 0 with 0 leaks reported.

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

**Pre-launch prerequisite (machine-checkable):** `git status --porcelain`
is empty and `git branch --show-current` is `master`. Record the launch SHA
in the status log below. Launch baseline (verified 2026-07-13): 162/162
parse tests (LLVM build), 154/154 (parse-only build; 8 cgfail skipped),
63/63 codegen E2E tests, leak-check concurrency subset 6/6 with 0 leaks.

## Non-goals (explicitly out of scope)

- Multi-error reporting / parser error recovery, caret/snippet rendering,
  `--json` diagnostics, and a warning system — deferred to a follow-up
  diagnostics epic. This epic delivers correct single-error reporting with
  locations.
- Channel send/recv syntax, stdlib growth, optimization passes, debug info
  (DWARF), and Windows support — separate epics.
- The legacy Bison/Flex path (`parser.yy`, `parse_helpers.cpp`) is untouched
  dead code and gets no semantic pass.

## Assumptions (recorded, not asked)

- Pre-1.0 language: breaking changes to accepted programs are acceptable and
  expected ("strict" migration chosen by the user, 2026-07-13).
- Integer width promotion (existing documented behavior) remains the only
  implicit conversion.

## Open questions

| # | Question | Raised by | Resolution |
|---|----------|-----------|------------|
| — | (none currently) | | |

## Status log

| Date | Event |
|------|-------|
| 2026-07-13 | Epic planned (overview, workplan U1–U8, design, evaluation, manifest). |
| 2026-07-13 | Readiness review round 1: 15 findings (script/gate mismatches, non-runnable done conditions, unnumbered audit programs, baseline red). Fixes applied; `codegen_http.b` hang root-caused and fixed in `runtime/blang_net.c` (`__blang_tcp_read_into_byte_array` looped to max_len instead of read-once); test scripts gained `BUILD_DIR` override + multi-file args; constitution amended to 1.1.0. Baseline recorded green. |
| 2026-07-13 | Readiness re-check passed 10/10; status → ready (user confirmed via launch). |
| 2026-07-13 | Launched on devbot: run `404b2b30-6c90-4218-be88-8f71143642ad` (endpoint http://localhost:8000, directory `a6b2f628-f546-4a5f-a22b-ff73e1dae54b`, fully_autonomous, max 160 turns / 14 days / 2M tokens per session). Status → launched. |
| 2026-07-13 | U1 (source locations) merged to master as b8c4703 after Phase-4 code audit: Gate A 162/162 + 63/63, Gate B 154/154, leak-check 6/6 0 leaks, both goldens (func_simple, match_basic) clean, corpus zero-location clean. REQ-001 satisfied. |
| 2026-07-13 | U2 (diagnostics engine + expected-error harness) merged to master as 47d4ee9 after Phase-4 code audit: Gate A 162/162 + 63/63, Gate B 154/154 (8 cgfail auto-skipped), Gate D byte-silent both build modes, SC-004 self-check green, 12 fail/ tests annotated with FR-014 canonical patterns, U1 goldens clean, zero accept/reject drift. REQ-002/REQ-003 + harness half of REQ-011 satisfied. |

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
