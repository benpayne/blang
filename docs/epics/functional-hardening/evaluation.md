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
| leak check (ARC matrix) | `./test_codegen.sh --leak-check <arc tests>` | exit 0, `Leaks: 0` | U1, U5 |
| runtime units | `ctest --test-dir build` | exit 0 | U5 |
| seeded S1 (struct-field reassign) | `./test_codegen.sh test_files/codegen_struct_field_reassign.b` | exit 0 (+ golden) | U1, U5 |
| seeded S2 (`Map` via `bcc`) | `bcc <map-program-using-import-collections>.b -o /tmp/m && /tmp/m` | exit 0 | U4, U5 |
| operator short-circuit | operator test proving RHS side effect skipped | golden shows skip | U2, U5 |
| new-test count | `ls test_files/codegen_*.b \| wc -l` grew by ≥ 20 vs launch baseline | ≥ +20 | U5 |

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

# 2. matrices exist: >= 20 new codegen tests vs launch baseline (record baseline at launch)
#    test "$(ls test_files/codegen_*.b | wc -l)" -ge $((BASELINE + 20))

# 3. seeded bugs fixed
./test_codegen.sh test_files/codegen_struct_field_reassign.b           # S1 passes + golden
printf 'import collections;\nfn main()->int{ Map<string,int> m = Map<string,int>{}; m.set("a",1); assert m.get("a")==1,"map"; return 0; }\n' > /tmp/mapchk.b
bcc /tmp/mapchk.b -o /tmp/mapchk && /tmp/mapchk                        # S2: Map via bcc

# 4. ARC matrix leak-clean (list the arc tests, or a tagged glob)
./test_codegen.sh --leak-check test_files/codegen_*arc*.b test_files/codegen_struct_field_reassign.b

# 5. fix-or-file: known-issues.md exists; every deferral has a repro (reviewer-verified);
#    no committed matrix test is failing (implied by #1 green)
test -f docs/epics/functional-hardening/known-issues.md

# 6. CI green on the final commit
gh run list --branch master --limit 1 --json conclusion --jq '.[0].conclusion' | grep -qx success
```
The manager additionally confirms `CLAUDE.md` counts/known-issues are updated
and that the seeded bugs no longer appear as open issues.
