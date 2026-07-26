# Feature Specification: Ownership & Move Analysis in Sema (U6)

**Branch**: `epic/blang-ast/u6-ownership` — Unit U6, covers **REQ-009**.

## Scope delivered (Sema, all build modes)
Lifts move tracking out of codegen (`CGExpressions.cpp` non-located `cerr`
checks) into the Sema pass as a bounded flow analysis, reported as located
diagnostics that work in parse-only / non-LLVM builds:
- **Use after move**: using an `own` value after it was moved is a located error.
- **Move via init and via call**: `own Y = X` and passing `X` to an `own`
  parameter both move `X`.
- **Reassignment clears moved state**: assigning a new value to a moved variable
  makes subsequent uses valid (accepted-program behavior).
- **Move in a loop**: moving an `own` value declared outside a loop, inside the
  loop body, is a located error (it would be moved again next iteration).
- **Own capture across spawn**: referencing an `own` value declared outside a
  `spawn` inside the spawn body is a located error.
Codegen's use-after-move rejection (`CGExpressions.cpp`) is removed; sema is
authoritative (design decision 4).

## Tests
Relocated from `cgfail/` to `test_files/fail/sema/` with `.expected` patterns:
`own_use_after_move.b`, `own_move_in_loop.b`, `own_spawn_capture.b`. New:
`test_files/fail/sema/own_indirect_move.b` (move via call, then use — rejected)
and `test_files/pass/own_reassign_after_move.b` (reassign after move — accepted).

## Success criteria
- SC-001: the four `own_*` fail/sema fixtures rejected under `build/qcc
  --parse-only` and the non-LLVM build with located lines matching `.expected`.
- SC-002: `own_reassign_after_move.b` compiles and runs (accepted) in both builds.
- SC-003: Gate A + Gate B green; leak-check subset 6/6 0 leaks; U1 goldens clean.

## Bounds (documented)
The flow analysis is intentionally bounded: linear moved-set with loop/spawn
nesting-depth tracking and reassignment-clears. It correctly handles the audit
and accept/reject cases above; full branch-merge dataflow (e.g. move on one side
of an if/else only) is not modeled and errs toward acceptance to avoid false
positives. No corpus false positives (existing own_*/shared_*/sync_* pass tests
remain green).

## Out of scope
The strict-migration sweep to >=25 fail/sema (U8, which also adds per-diagnostic
coverage for the remaining U4–U7 diagnostics).
