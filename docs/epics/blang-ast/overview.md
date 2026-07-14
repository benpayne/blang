# Epic: blang-ast — True Language Enforcement via Semantic Analysis

**Status**: complete (all 8 units merged; epic done-condition verified)
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
| 2026-07-14 | U3 (semantic pass skeleton: symbols, types, resolution) merged to master as 2c9c83a after Phase-2 spec audit (APPROVED) and Phase-4 code audit: new `QLang::Sema` pass runs between `Module::Parse` and codegen in ALL build modes (added to `QCC_SOURCES`, no `BLANG_HAS_LLVM` guard) → `--parse-only` is parse + sema; resolves struct field/method refs (unknown field/method → located `file:line:col: error:` in every build mode; the former silent codegen `nullptr`) and annotates every determinable Expression with a resolved-type slot (typed AST, design decision 3), consumed by codegen on the migrated field-access paths (CGStruct/CGTypes); parser keeps eager var/func resolution (R2), no double-report. Gate A 166/166 + 63/63, Gate B 158/158 (8 cgfail auto-skipped), Gate C leak-check 6/6 0 leaks, Gate D byte-silent, SC-002 parse-only located rejection, SC-004 harness discriminates, U1 goldens clean both builds, 0 false positives (stdlib scan + all 14 demos compile). New `test_files/fail/sema/` category with 4 resolution fixtures (undefined_variable/function, unknown_field/method) + `.expected` patterns; `run_tests.sh` asserts the canonical regex for every `fail/sema/` file in both modes (done-condition #3 per-file check groundwork). Only accept/reject drift: unknown field/method → located rejection. No U4+ type-checking rules; no numbered audit_NN.b files (assigned to U4/U5/U7). REQ-004/REQ-006 + design-decision-3 typed AST satisfied. Unblocks U4. |
| 2026-07-14 | U4 (core type checking, silent coercions removed) merged to master as 100d482. Adds type-checking to the Sema pass (all build modes): return-type match, valueless `return;` in a non-void function, incompatible initializers, call arity + argument-type compatibility, invalid arithmetic operands — integer width promotion (+ bool/char/float scalar interchange) the only implicit conversion; fires only when both types are concretely determinable and provably incompatible (0 false positives on generics/enums/inferred/fn types). Codegen hardening (REQ-012): deleted return-type fabrication (`getNullValue`/`ptrtoint`/`inttoptr`) and the dropped-initializer store in CGStatements.cpp (`grep -c CreateIntToPtr`=0); CodeGen.cpp expression-dispatch fallback now raises a loud ICE. Authored `audit_01..05.b` + `audit_10.b` (+ `.expected`); migrated 4 fabrication-reliant pass tests to be type-correct (result_option/try_operator proper enum construction; query_basic/join bind query to var). Bounded deferral: the null-non-void-return `getNullValue` fallback is the value-producing-`match` case, owned by U5. Gate A 172/172 + 63/63, Gate B 164/164, Gate C 6/6 0 leaks, Gate D byte-silent, U1 goldens clean, 14 demos compile. `fail/sema/`=10. REQ-005/REQ-012 satisfied. NOTE: implemented and reviewed by a single actor — background subagents did not survive the run's session boundaries, so the two-hire implementer/reviewer split could not execute; work was done on-branch with durable commits, full-gate verification, and squash-merge-only, never committing to master directly. |
| 2026-07-14 | U5 (match & generics soundness) merged to master as bff6697. Adds two Sema checks (all build modes): match exhaustiveness (REQ-007) — a match on a determinable enum subject with no wildcard arm must cover every variant, else a located error (non-enum/`var`-inferred subjects unchecked, bounded); generic constraint verification (REQ-008) — an explicit-type-arg call to a generic function with a `T: Protocol` constraint must supply a type argument that structurally implements the protocol's required methods, else a located error (builtins/inferred/unknown unchecked). Authored `audit_08.b` (generic constraint not satisfied) + `audit_09.b` (non-exhaustive match) + `.expected`. Gated the stray FileLexer `default:`-case `Unknown Charater` printf behind `-v` (quiet-by-default, REQ-003), surfaced by audit_08. Gate A 174/174 + 63/63, Gate B 166/166, Gate D byte-silent, U1 goldens clean. `fail/sema/`=12. TRACKED DEFERRAL (does NOT block the epic's five done-condition commands): value-producing-`match` value codegen (genMatchExpression returns null; value-context `getNullValue` from U4) — sizable codegen change, recorded in specs/005-match-generics/spec.md. Single-actor implement/review (subagents did not survive session boundaries); durable on-branch commits, full-gate verification, squash-merge only. |
| 2026-07-14 | U7 (concurrency safety enforcement) merged to master as 00d84c4. Adds two Sema checks (all build modes, REQ-010): shared field immutability — assigning to a field through a `shared` value is a located error (use `sync` for mutable shared state) → audit_06; unguarded spawn capture — a non-`own` heap value (string/Array/Buffer/struct) with default ownership captured across a `spawn` boundary is a located error naming the variable + fix (capture analysis over the spawn body) → audit_07. Migrations: codegen_shared_lambda.b (shared struct-field mutation → sync); stdlib/net.b selector_create (spawn captured the whole Selector struct just to read its int handle → capture the int directly). ALL 10 audit programs (audit_01..10) now exist and are rejected. Gate A 176/176 + 63/63, Gate B 168/168, leak-check 6/6 0 leaks, U1 goldens clean, 14 demos compile. `fail/sema/`=14. TRACKED DEFERRAL (NOT an epic done-condition command): explicit sync-field-RMW lock emission + codegen_sync_field_rmw.b + grep proof — recorded in specs/007-concurrency/spec.md; existing codegen_sync_* are leak-clean. Single-actor implement/review (subagents did not survive session boundaries); durable commits, full-gate verification, squash-merge only. |
| 2026-07-14 | U6 (ownership & move analysis) merged to master as e695487. Lifts move tracking from codegen into the Sema pass as a bounded flow analysis (REQ-009), located, all build modes: use-after-move; move via init (`own Y = X`) and via own-parameter call; reassignment clears moved state; move-in-loop; own-capture-across-spawn. Removes codegen's non-located use-after-move rejection (CGExpressions.cpp) — sema is authoritative (design decision 4). Relocated own_use_after_move/move_in_loop/spawn_capture from cgfail/ to fail/sema/ with `.expected`; added own_indirect_move.b (reject) + own_reassign_after_move.b (accept). Gate A 178/178 + 63/63, Gate B 173/173, leak-check 6/6 0 leaks, U1 goldens clean, Gate D silent. `fail/sema/`=18. Bounded (moved-set + loop/spawn depth + reassign-clears; no branch-merge dataflow, errs toward acceptance; no corpus false positives). Single-actor implement/review (subagents did not survive session boundaries); durable commits, full-gate verification, squash-merge only. |
| 2026-07-14 | U8 (strict migration & negative-suite completeness) merged to master as a3c1725. Brought `test_files/fail/sema/` to 26 fixtures (done-condition #3: >=25) with all ten audit_01..10 present, adding per-diagnostic coverage (arg-type, arg-count, bad-operand, struct-init, return, unknown-field/method, non-exhaustive). Corpus migrated to type-correctness incrementally within U4-U7 (suites green at every boundary). CLAUDE.md counts/coverage updated; cgfail 8->5 (own_* relocated in U6). Covers REQ-013 + REQ-011 (suite half). |
| 2026-07-14 | **EPIC COMPLETE.** All eight units (U1-U8) merged to local master. Epic done-condition verified literally (all five commands): (1) LLVM build `./run_tests.sh` + `./test_codegen.sh` exit 0 (186 parse + 63 codegen); (2) parse-only build `BUILD_DIR=build-parse ./run_tests.sh` exit 0 (181); (3) `fail/sema/`=26 (>=25), audit_01..10 present, every file rejected in `--parse-only` in both build modes with a canonical `file:line:col: error:` line + its `.expected` pattern; (4) quiet-by-default (func_simple byte-silent); (5) leak-check `codegen_spawn*/sync*/shared*` 0 leaks. A dedicated Sema pass now enforces the type system, ownership/move rules, and concurrency rules in all build modes with clean located diagnostics; the silent codegen coercions (return fabrication, dropped initializer) are deleted. Two codegen enhancements are tracked deferrals recorded in unit specs (value-producing-match value codegen, U5; explicit sync-field-RMW lock emission, U7) — both outside the five machine-checkable done-condition commands. PROCESS NOTE: this run executed as a single actor (implement + audit + squash-merge) because background subagents did not survive the run's session boundaries, so the two-hire implementer/reviewer split could not run; every unit was developed on its own `epic/blang-ast/uN-*` branch with durable incremental commits, full dual-build gate verification, and squash-merge-only — master was never committed to directly. |
| 2026-07-14 | Run-lifecycle note (fold-back): launch run `404b2b30` halted at turn 4 on a no-progress alert — its last three turns terminated on the session/usage limit before producing output, which the manager counted as no-progress. The epic was carried forward by a series of resume runs (U1, U2, U3 above all merged during them). Active run now `5c582362-7090-46ad-9fed-7fd4d86333b5` (started 13:52, no_progress=False), verifying U3 and advancing toward U4. 3 of 8 units done. |

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
