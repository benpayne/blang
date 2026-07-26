# Evaluation: blang-ast

Instantiates the constitution's audit pattern (`.specify/memory/constitution.md`)
for this epic. Every unit is audited **twice**: once at spec time (Phase 2,
before any implementation) and once at code time (Phase 4, before merge).
Both audits are performed by the secondary reviewer hire, independent of the
implementer, at the devbot manager's direction; the manager gates the phase
transitions on the audit outcomes. See `workplan.md` §Unit lifecycle.

## Spec audit rubric (Phase 2 — gate before implementation)

Applied to the unit's committed speckit artifacts
(`specs/<NNN>-<slug>/spec.md`, `plan.md`, `tasks.md`) on the draft PR:

1. **Traceability**: every REQ-ID the unit covers (workplan.md coverage
   table) appears in the spec with concrete acceptance criteria; the spec
   introduces no requirements outside the unit's scope.
2. **Testability**: acceptance criteria are machine-checkable and consistent
   with the unit's done-when in `workplan.md`; every new diagnostic the unit
   introduces has a planned `fail/sema/` test with an expected-message
   pattern in tasks.md.
3. **Design conformance**: the plan respects `design.md`'s fixed decisions
   (separate sema pass, single DiagnosticEngine, typed AST, codegen-trusts-
   loudly, closed conversion set) and touches only the expected seams.
4. **Constitution compliance**: nothing in the spec relaxes the quality
   gates, skips required tests, or reintroduces silent coercion.
5. **Completeness of tasks**: tasks.md ends with the per-unit gate commands;
   migration impact on existing tests/stdlib/demos is identified with tasks,
   not left implicit.

Findings are posted on the draft PR and must be resolved (spec revised, or
dispositioned with recorded rationale) before the manager authorizes
Phase 3. Implementation begun before spec approval is itself a blocking
finding.

## Per-unit gates (every PR, all must pass)

Run from the repo root on the PR branch. Both scripts honor a `BUILD_DIR`
environment override (added 2026-07-13); `test_codegen.sh` accepts multiple
test-file arguments. The default `build/` directory is the LLVM build;
`build-parse/` is the parse-only build.

```bash
# Gate A — LLVM build; full parse suite + full codegen E2E suite
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build -j"$(nproc)"
./run_tests.sh
./test_codegen.sh

# Gate B — parse-only build; parse+sema suite (cgfail auto-skips pre-U3)
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF
cmake --build build-parse -j"$(nproc)"
BUILD_DIR=build-parse ./run_tests.sh

# Gate C — leak check, for units touching runtime/, ARC, ownership, or
# concurrency codegen (U6, U7, and any diff touching those paths):
./test_codegen.sh --leak-check test_files/codegen_spawn*.b \
  test_files/codegen_sync*.b test_files/codegen_shared*.b
```

Plus, from U2 onward:

```bash
# Gate D — quiet-compile check: zero output, exit 0, no flags beyond
# --parse-only (avoids the .ll side effect of an LLVM-build compile)
out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1)
test -z "$out"

# Gate E — expected-error mode: negative tests must match their declared
# message (part of run_tests.sh from U2; Gates A and B re-run it)
```

## Unit-specific checks

- **U1** golden-location diff:
  `./build/qcc --dump-locations test_files/pass/func_simple.b | diff - test_files/golden/func_simple.locations`
  and the same for `match_basic`; both exit 0.
- **U4** coercion-site removal: `grep -c "CreateIntToPtr" CGStatements.cpp`
  returns 0.
- **U7** sync-RMW lock proof:
  `./test_codegen.sh test_files/codegen_sync_field_rmw.b` passes and
  `grep -c "__blang_sync_lock" test_files/codegen_sync_field_rmw.ll` ≥ 1.

A PR that requires relaxing or deleting an existing test to pass must say so
in its description with rationale; the reviewer treats an unexplained test
change as a blocking finding.

## Code review rubric (Phase 4 — gate before merge)

For every PR, the reviewer verifies and records:

1. **Gates**: all commands above pass, run by the reviewer, not taken on
   faith from CI or the implementer.
2. **Spec fidelity**: the diff matches the unit's speckit spec; scope creep
   or silent descoping is a finding.
3. **Requirement traceability**: the unit's REQ-IDs (workplan.md coverage
   table) are demonstrably satisfied, each new diagnostic has a
   `fail/sema/` test with an expected-message pattern.
4. **Reject-don't-coerce** (constitution III): no new silent fallback,
   `return nullptr`, or value fabrication paths; deleted coercion sites are
   actually deleted, not guarded.
5. **Locations**: every new diagnostic prints `file:line:col` pointing at
   the correct token (spot-check ≥ 3 by hand).
6. **Style**: house style per `CLAUDE.md` (tabs, Allman, `m`-prefix members).
7. **Docs**: `CLAUDE.md`/`docs/language_design.md` updated when behavior
   changed (constitution I).

All findings are resolved (fixed, or dispositioned with recorded rationale)
before the reviewer merges to `master`. Unresolved findings block the merge.

## Epic-level acceptance (run at U8 completion)

The done condition in `overview.md`, executed literally:

```bash
# (1) LLVM build, full suites
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build -j"$(nproc)"
./run_tests.sh && ./test_codegen.sh

# (2) parse-only build, parse+sema suite
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF
cmake --build build-parse -j"$(nproc)"
BUILD_DIR=build-parse ./run_tests.sh

# (3) sema negative suite: count, audit coverage, per-test format check
test "$(ls test_files/fail/sema/*.b | wc -l)" -ge 25
for n in 01 02 03 04 05 06 07 08 09 10; do
  test -f "test_files/fail/sema/audit_$n.b"
done
# per-test exit/format/message assertions are enforced by run_tests.sh (Gate E)

# (4) silence
out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"

# (5) concurrency subset leak-clean
./test_codegen.sh --leak-check test_files/codegen_spawn*.b \
  test_files/codegen_sync*.b test_files/codegen_shared*.b
```

Additionally the manager verifies that `CLAUDE.md` known-issues no longer
lists the holes this epic closed.

## Budget guardrails

Estimates (honest): 8 units, each a focused PR touching 5–25 files.
Manifest limits are set at ~2× these estimates (see `manifest.yaml`). If a
unit blows its iteration limit, the manager pauses it and surfaces the
blocker to the user via `/devbot-status` rather than burning budget.
