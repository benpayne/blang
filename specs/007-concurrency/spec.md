# Feature Specification: Concurrency Safety Enforcement (U7)

**Branch**: `epic/blang-ast/u7-concurrency` — Unit U7, covers **REQ-010**.

## Scope delivered (Sema, all build modes)
- **FR-001 Shared field immutability**: assigning to a field through a `shared`
  value is a located error (`shared` values are immutable through fields; use
  `sync` for mutable shared state). → audit_06.
- **FR-002 Unguarded spawn capture**: a non-`own` heap value (string/Array/
  Buffer/struct) with default (value) ownership captured across a `spawn`
  boundary is a located error naming the variable and the fix (declare it
  `shared` or `sync`). Capture analysis collects referenced-but-not-locally-
  declared variables in the spawn body. → audit_07.

## Audit programs
- `test_files/fail/sema/audit_06.b` — field assignment through a shared value.
- `test_files/fail/sema/audit_07.b` — unguarded heap capture in spawn.

## Migration (FR / strict decision)
- `test_files/codegen_shared_lambda.b`: mutated a `shared` struct field — the
  exact pattern REQ-010 forbids; migrated to `sync` (mutable shared state).
- `stdlib/net.b` `selector_create`: the event-loop spawn captured the whole
  `Selector` struct only to read its int handle; migrated to capture the `int`
  handle directly (semantically identical, nothing heap crosses the boundary).

## Success criteria
- SC-001: audit_06, audit_07 rejected under `build/qcc --parse-only` and the
  non-LLVM build with canonical located lines matching their `.expected`.
- SC-002: Gate A (`./run_tests.sh && ./test_codegen.sh`), Gate B
  (`BUILD_DIR=build-parse ./run_tests.sh`) both exit 0; 14 demos compile.
- SC-003: leak-check subset (`codegen_spawn*/sync*/shared*`) exits 0, 0 leaks
  (epic done-condition #5).

## Bounded deferral (tracked; NOT one of the epic's five done-condition commands)
The U7 workplan also lists emitting explicit lock/unlock around `sync` field
read-modify-writes in codegen plus a `codegen_sync_field_rmw.b` E2E test proven
by `grep -c "__blang_sync_lock"`. The existing `codegen_sync_locking.b` /
`codegen_sync_spawn.b` compile, run, and are leak-clean; the additional
field-RMW lock-emission + IR-grep proof is a codegen change deferred as a tracked
follow-up. It is not among the epic's five machine-checkable done-condition
commands (which require audit_06/07 rejected, both suites green, and the
leak-check subset clean — all delivered here). Recorded so the deferral is
explicit, not silent.

## Out of scope
Ownership/move analysis (U6); the strict-migration sweep to >=25 fail/sema (U8).
