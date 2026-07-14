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

Run from the repo root on the PR branch:

```bash
# 1. Both build configurations compile clean
rm -rf build && mkdir build && cd build && cmake .. && make -j"$(nproc)" && cd ..
rm -rf build-llvm && mkdir build-llvm && cd build-llvm && \
  cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && make -j"$(nproc)" && cd ..

# 2. Full parse/sema suite green (LLVM build AND parse-only build)
./run_tests.sh

# 3. Codegen E2E suite green
./test_codegen.sh

# 4. Units touching runtime, ARC, ownership, or concurrency codegen
#    (U6, U7, and any unit whose diff touches runtime/ or CG*.cpp ARC paths):
./test_codegen.sh --leak-check
```

Plus, from U2 onward:

```bash
# 5. Quiet-compile check — zero output without flags on a passing file
out=$(./build-llvm/qcc test_files/pass/func_simple.b 2>&1); test -z "$out"

# 6. Expected-error mode — negative tests must match their declared message
./run_tests.sh   # (expected-error matching is part of the suite after U2)
```

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
# (1)+(2) suites, both builds
./run_tests.sh && ./test_codegen.sh

# (3) sema negative suite: count and per-test format check
ls test_files/fail/sema/*.b | wc -l          # ≥ 25
# each test: exit non-zero, stderr matches '^[^:]+\.b:[0-9]+:[0-9]+: error: '
# and the test's own expected pattern (enforced by run_tests.sh)

# (4) silence
out=$(./build-llvm/qcc test_files/pass/func_simple.b 2>&1); test -z "$out"

# (5) concurrency subset leak-clean
./test_codegen.sh --leak-check test_files/codegen_spawn*.b \
  test_files/codegen_sync*.b test_files/codegen_shared*.b
```

Additionally the manager verifies the 10 audit programs (catalogued in
`design.md` §Context and encoded as `fail/sema/audit_01..10.b`) are all
rejected, and that `CLAUDE.md` known-issues no longer lists the holes this
epic closed.

## Budget guardrails

Estimates (honest): 8 units, each a focused PR touching 5–25 files.
Manifest limits are set at ~2× these estimates (see `manifest.yaml`). If a
unit blows its iteration limit, the manager pauses it and surfaces the
blocker to the user via `/devbot-status` rather than burning budget.
