# Evaluation: functional-hardening

**Epic**: [overview.md](overview.md) · **Constitution**: `.specify/memory/constitution.md`

Every unit is audited twice (spec audit before implementation, code audit
before merge) by an independent reviewer hire, gated by the manager. Commands
are literal and runnable.

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema (LLVM) | `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh` | exit 0 | every unit |
| parse/sema (parse-only) | `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh` | exit 0 | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | exit 0 | every unit |
| leak check (ARC matrix) | `./test_codegen.sh --leak-check test_files/codegen_arc_*.b` (U1 names ARC tests `codegen_arc_*.b`) | exit 0, `Leaks: 0`; glob non-empty | U1, U5 |
| runtime units | `ctest --test-dir build` | exit 0 | U5 |
| seeded S1 (struct-field reassign) | `./test_codegen.sh test_files/codegen_struct_field_reassign.b` | exit 0 (+ golden) | U1, U5 |
| seeded S2 (`Map` via `bcc`) | `bcc <map-program-using-import-collections>.b -o /tmp/m && /tmp/m` | exit 0 | U4, U5 |
| operator short-circuit | operator test proving RHS side effect skipped | golden shows skip | U2, U5 |
| new-test count | `test "$(ls test_files/codegen_*.b \| wc -l)" -ge 105` (baseline 85 + 20) | exit 0 | U5 |

## Per-unit gates (every PR)
The reviewer independently runs, on the unit's branch: both parse/sema suites,
`./test_codegen.sh`, and the unit's own harness rows. A matrix unit must show
its new tests passing with goldens; the ARC unit must show `--leak-check` clean.
No new test may be committed failing; any deferral must appear in
`known-issues.md` with a repro (reviewer confirms the repro reproduces).

## Audit plan (instantiating the constitution)
| Audit | When | Rubric | Gates? |
|-------|------|--------|--------|
| spec audit | after each unit's speckit spec | covers the unit's REQ IDs with concrete, named test cases (a real matrix, not one token test); goldens planned; ARC unit plans `--leak-check`; fix-or-file understood | gates |
| code review | before each unit merges | tests have teeth (goldens, not exit-code-only where output exists); bugs fixed not masked; deferrals in known-issues.md have real repros; reviewer re-runs gates | gates |
| memory-safety | U1 | `--leak-check` output attached, 0 leaks over the ARC matrix | gates |
| functional review | U5 | full epic done condition | gates |

## Regression protection (evolve)
- **Baseline (measured at launch, recorded in status log):** `run_tests.sh`
  count (LLVM + parse-only), `test_codegen.sh` count, `ctest` count, and the
  new-test target (`codegen_*.b` count at launch, so "+20" is measured, not
  assumed). As of branch `fix/nested-field-access`: `run_tests` 195, codegen 85.
- **Must not change:** existing green suites; default build output. Fixes are
  correctness-only.

## Epic-level acceptance (run at U5)
```bash
set -e
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"

# 1. both suites, both modes + runtime units
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh
./test_codegen.sh
ctest --test-dir build

# 2. matrices exist: >= 20 new codegen tests vs the launch baseline.
#    BASELINE is the codegen_*.b count measured at launch (85 @ fix/nested-field-access;
#    also recorded in manifest.yaml run.baseline.codegen_tests).
BASELINE=85
test "$(ls test_files/codegen_*.b | wc -l)" -ge $((BASELINE + 20))

# 3. seeded bugs fixed. Syntax is known-good (matches codegen_map.b:56), so
#    these test ONLY the bug. Both are POST-FIX gates: the S2 program
#    legitimately fails today (that IS the bug — Map from the collections module
#    is unresolved via both bcc-import and qcc --combine); it must pass at U5.
./test_codegen.sh test_files/codegen_struct_field_reassign.b           # S1 passes + golden
printf 'import collections;\nfn main() -> int { Map<string,int> m = Map<string,int> { keys: [], values: [] }; m.set("a", 1); assert m.get("a") == 1, "map"; return 0; }\n' > /tmp/mapchk.b
bcc /tmp/mapchk.b -o /tmp/mapchk && /tmp/mapchk                        # S2: Map via bcc

# 4. ARC matrix leak-clean. U1 MUST name its ARC tests test_files/codegen_arc_*.b
#    (this glob is the gate; a different name makes it vacuous — see C2/design invariants).
test -n "$(ls test_files/codegen_arc_*.b 2>/dev/null)"                 # the matrix exists
./test_codegen.sh --leak-check test_files/codegen_arc_*.b test_files/codegen_struct_field_reassign.b

# 5. fix-or-file, bounded and non-trivial: known-issues.md exists, is structured
#    (### KI-N entries with a Repro block + Justification), and holds AT MOST 3
#    unfixed matrix bugs. Beyond 3, the hire must have raised an Open Question,
#    not bulk-deferred — acceptance FAILS.
test -f docs/epics/functional-hardening/known-issues.md
ki=$(grep -c '^### KI-' docs/epics/functional-hardening/known-issues.md || echo 0)
test "$ki" -le 3
# (reviewer additionally confirms each ### KI-N repro reproduces and was raised
#  as an Open Question before filing; seeded S1/S2 may never appear here.)

# 6. CI green on the final commit
gh run list --branch master --limit 1 --json conclusion --jq '.[0].conclusion' | grep -qx success
```
The manager additionally confirms `CLAUDE.md` counts/known-issues are updated
and that the seeded bugs no longer appear as open issues.
