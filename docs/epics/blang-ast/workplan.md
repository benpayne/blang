# Workplan: blang-ast

Eight units. Each unit is one work assignment for an implementing hire and
moves through the five-phase lifecycle below. Every unit leaves
`./run_tests.sh` and `./test_codegen.sh` green — migration debt is not
allowed to accumulate between units.

## Unit lifecycle (applies to every unit; manager gates each transition)

Each unit UN passes through five phases. The devbot manager owns the
transitions: a phase does not begin until the previous phase's gate is met.

**Phase 1 — Create spec.** The implementing hire cuts a branch
`epic/blang-ast/uN-<slug>` from the latest `master`, then runs the speckit
ceremony for the unit: `/speckit-specify` fed the unit's entry in this file
plus `overview.md` and `design.md`; `/speckit-clarify` if ambiguities
surface (directed to the manager, not left open); `/speckit-plan`; then
`/speckit-tasks`. The resulting `specs/<NNN>-<slug>/` artifacts (spec.md,
plan.md, tasks.md) are committed to the branch and a **draft PR** is opened
titled `[blang-ast][UN] <unit title>`. No implementation happens in this
phase.

**Phase 2 — Audit spec (gate).** The secondary reviewer hire audits the
committed spec against the "Spec audit rubric" in `evaluation.md`:
requirement traceability to the unit's REQ-IDs, machine-checkable acceptance
criteria consistent with the unit's done-when, scope bounded to the unit,
no conflict with the constitution or `design.md`'s fixed decisions, and
tasks that include the required tests. Findings are posted on the draft PR;
the implementer revises the spec until the reviewer approves. **The manager
authorizes implementation only on spec approval.**

**Phase 3 — Implement.** The implementing hire runs `/speckit-implement` on
the same branch, keeping commits scoped to the unit. Before requesting
review it runs the per-unit gates from `evaluation.md` locally and fixes
what fails.

**Phase 4 — Audit code (gate).** The draft PR is marked ready for review.
The secondary reviewer independently runs the per-unit gate commands and
applies the "Code review rubric" in `evaluation.md`, including checking the
diff against the Phase-1 spec (unspecified work or silently dropped spec
items are findings). The implementer addresses findings; rounds are capped
by `manifest.yaml` limits, after which the manager escalates to the user.

**Phase 5 — Merge.** With all findings resolved and gates green, the
reviewer updates the branch onto the latest `master` (rebase; re-run gates
if the rebase picked up changes), **squash-merges** the PR into `master`,
and deletes the branch. The manager marks the unit done and unblocks
dependents.

**Parallel units (U5/U6/U7):** each cuts its branch from `master` *after*
U4 has merged. The manager serializes their merges; when one merges, the
remaining in-flight branches rebase onto the new `master` and re-run gates
before their own Phase 5. Merge order on conflict: U6 before U7 (both touch
ownership-adjacent codegen); U5 is expected to be disjoint.

**Git ground rules:** all work on `epic/blang-ast/uN-*` branches; no direct
commits to `master`; one PR per unit; squash merge (one commit per unit on
`master`, message `[blang-ast][UN] <title>`); the speckit `specs/` artifacts
merge with the code so the spec history lands in the repo.

## Dependency map

```
U1 (locations) ─→ U2 (diagnostics + error-test harness) ─→ U3 (sema skeleton)
                                                              │
                                            ┌─────────────────┤
                                            ▼                 ▼
                                     U4 (core types)   (U3 unblocks U4 only)
                                            │
                              ┌─────────────┼─────────────┐
                              ▼             ▼             ▼
                        U5 (match+       U6 (ownership  U7 (concurrency
                           generics)        /moves)        safety)
                              └─────────────┼─────────────┘
                                            ▼
                                     U8 (strict migration
                                        + negative suite)
```

U5, U6, U7 are independent of each other and may run in parallel after U4.
U8 requires all of U5–U7.

## Units

### U1 — Source locations end to end
Covers: REQ-001.
Scope: store the filename in the lexer; make column tracking real
(`charPos` in `FileLexer.cpp` is initialized once and never incremented);
add a `SourceLocation {file, line, col}` to the AST base (`Statement` in
`Type.h`) captured at parse time in every `Parse` method; `CompileError`
carries a `SourceLocation` instead of leaning on the live lexer.
Done when: U1 delivers a `--dump-locations` flag on qcc (prints one
`<file>:<line>:<col> <NodeKind>` line per AST node, deterministic order) and
two committed golden files; the checks
`build/qcc --dump-locations test_files/pass/func_simple.b | diff - test_files/golden/func_simple.locations`
and
`build/qcc --dump-locations test_files/pass/match_basic.b | diff - test_files/golden/match_basic.locations`
both exit 0, no node in either dump has line 0 or column 0, and all
existing suites are green.

### U2 — Diagnostics engine and expected-error test harness
Covers: REQ-002, REQ-003, REQ-011 (harness half).
Scope: a single diagnostic reporting path used by parser and (later) sema:
`<file>:<line>:<col>: error: <message>`; parse errors name the offending
token (lexer's `getSymbolText()`); stop swallowing specific errors in the
`QStatement.cpp:73-86` backtracking (keep the deepest error); demote C++
`__FILE__:__LINE__` to an internal `--debug-compiler` flag; gate ALL
per-token/trace/AST-dump output behind `-v` (`FileLexer.cpp:321`,
`qcc.cpp:282,681`); LLVM verifier failure prints an ICE message, not raw IR.
Harness: `run_tests.sh` learns expected-error matching — each `fail/` and
`cgfail/` test may declare an expected-message pattern (companion
`.expected` file or `// EXPECT-ERROR:` comment); a test passes only if the
compiler exits non-zero AND the pattern matches stderr.
Done when: compiling a good file with no flags is byte-silent; a known-bad
fixture produces exactly one `file:line:col: error:` line; ≥ 10 existing
`fail/` tests carry expected-message patterns and pass in the new mode; all
suites green.

### U3 — Semantic pass skeleton: symbols, types, resolution
Covers: REQ-004, REQ-006.
Scope: new `Sema` module invoked between `Module::Parse` and `CodeGen`,
compiled in ALL build modes (no `BLANG_HAS_LLVM` guard); computes and stores
the type of every expression on the AST (typed AST); resolves every
variable, function, field, and method reference, reporting located errors
for unknowns; establishes the single type representation the checker and
codegen share. Codegen keeps working from the now-annotated AST.
Done when: `qcc --parse-only` (and a non-LLVM build) rejects undefined
variables/functions/fields/methods with located diagnostics; new `fail/sema/`
tests for each resolution error class; all suites green.

### U4 — Core type checking, silent coercions removed
Covers: REQ-005, REQ-012.
Scope: enforce call arity and argument types; assignment and initializer
compatibility (kill the dropped-initializer path, `CGStatements.cpp:285`);
binary/unary operand validity (including float unary minus); return-path
checking — type match (kill `inttoptr`/`getNullValue` fabrication,
`CGStatements.cpp:521-541`), `return;` in non-void, missing return on a
path. Codegen hardening: unhandled node types or ill-typed input reaching
codegen (`CodeGen.cpp:1118`) become loud ICEs.
Done when: audit programs 1–5 and 10 (numbered table in `design.md`) exist
as `fail/sema/audit_{01..05,10}.b` and are each rejected with a located
error matching their expected-message pattern; the specific coercion sites
are gone — `grep -c "CreateIntToPtr" CGStatements.cpp` returns 0 and the
dropped-initializer branch (`initVal = nullptr` fallback) no longer exists
in `CGStatements.cpp`; the "no other silent fallback path remains" judgment
is the reviewer's (code-review rubric item 4), not a machine check; all
suites green.

### U5 — Match and generics soundness
Covers: REQ-007, REQ-008.
Scope: match exhaustiveness against the enum's variant set (or `_` arm);
match-as-expression produces a checked value (today it always yields
`nullptr`, `CGEnum.cpp:331`); generic constraint verification at
instantiation for functions and structs (`CGTypes.cpp:397,212` never check),
moved into sema so it works in parse-only mode.
Done when: audit programs 8 (unconstrained generic) and 9 (non-exhaustive /
value-producing match) are rejected with located errors; a value-producing
match E2E codegen test passes; all suites green.

### U6 — Ownership and move analysis in sema
Covers: REQ-009.
Scope: lift move tracking out of codegen (`CGExpressions.cpp:69-75`,
`CGStatements.cpp:606-630`) into a flow-aware sema check: reassignment
clears moved state; moves via field access/array element/call results are
tracked; if/else merge does not produce spurious errors; loop handling
rejects only real repeated moves; diagnostics show both the use site and
the original move site (two located notes). Works in parse-only mode.
Done when: existing `cgfail/own_*.b` tests are enforced by sema (relocated
to `fail/sema/` with expected messages) and pass in a parse-only build; new
tests for reassign-after-move (accepted) and indirect moves (rejected);
all suites green.

### U7 — Concurrency safety enforcement
Covers: REQ-010.
Scope: reject field assignment through `shared` values (`CGStruct.cpp:1035`
currently allows it); emit lock/unlock around `sync` field reads and
read-modify-writes in codegen; sema rule that non-`own` heap values
(string/Array/struct) captured by `spawn` must be `shared` or `sync` —
otherwise a located error naming the variable and the fix.
Done when: `fail/sema/audit_06.b` (shared-field mutation) and
`fail/sema/audit_07.b` (unguarded spawn capture) are rejected with their
expected messages; a new `codegen_sync_field_rmw.b` E2E test passes AND its
generated IR contains the lock around the field RMW, checked mechanically:
`grep -c "__blang_sync_lock" test_files/codegen_sync_field_rmw.ll` ≥ 1 after
`./test_codegen.sh test_files/codegen_sync_field_rmw.b`;
`./test_codegen.sh --leak-check test_files/codegen_spawn*.b
test_files/codegen_sync*.b test_files/codegen_shared*.b` exits 0; all suites
green.

### U8 — Strict migration and negative-suite completeness
Covers: REQ-013, REQ-011 (suite half).
Scope: sweep `test_files/` (231 `.b` files), `stdlib/*.b` (6), and
`demos/*.b` (14) for code the new checker rejects and fix it to be
type-correct (per the strict decision); ensure every diagnostic introduced
in U3–U7 has at least one `fail/sema/` test with an expected-message
pattern; bring the suite to ≥ 25 sema negative tests including all 10 audit
programs (design.md table); update `CLAUDE.md` test counts and known-issues
(remove the ones this epic fixes) and `docs/language_design.md` where
enforcement semantics are now real.
Sizing: before starting, the hire runs the U7-complete compiler over the
full corpus and records the rejection count in the PR description. U8 is
pre-authorized to land as **multiple stacked PRs** (each through the normal
audit-code/merge phases) and is exempt from the per-PR file cap up to
`max_files_changed_per_pr_u8` in `manifest.yaml`.
Done when: the epic-level done condition in `overview.md` holds, checked by
running the "Epic-level acceptance" commands in `evaluation.md` literally.

## Requirement coverage

| REQ | Units |
|-----|-------|
| REQ-001 | U1 |
| REQ-002 | U2 |
| REQ-003 | U2 |
| REQ-004 | U3 |
| REQ-005 | U4 |
| REQ-006 | U3 |
| REQ-007 | U5 |
| REQ-008 | U5 |
| REQ-009 | U6 |
| REQ-010 | U7 |
| REQ-011 | U2 (harness), U8 (suite completeness) |
| REQ-012 | U4 |
| REQ-013 | U8 |
