# Evaluation: feature-integration

**Epic**: [overview.md](overview.md) · **Constitution**: `.specify/memory/constitution.md`

Every unit is audited twice (spec audit before implementation, code audit before
merge) by an independent reviewer hire, gated by the manager. Commands are literal.

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema (LLVM) | `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh` | exit 0 | every unit |
| parse/sema (parse-only) | `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh` | exit 0 | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | exit 0 | every unit |
| sema enforcement intact | `ls test_files/fail/sema/*.b \| wc -l` ≥ 26; `BUILD_DIR=build-parse ./run_tests.sh` | fixtures still rejected | every unit |
| leak-check teeth | `./test_codegen.sh --leak-check` | exit 0, `Leaks: 0` | units touching codegen/runtime |
| runtime units | `ctest --test-dir build` | exit 0, ≥30 | U1, U5, U8 |
| fuzz replay | `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` | exit 0 | U8 |
| channels (origin) | origin's channel test via `./test_codegen.sh <test>` | exit 0 | U2, U8 |
| event loop (origin) | origin's event-loop test/demo | builds + exit 0 | U3, U8 |
| exhaustive match (origin+local) | non-exhaustive match → located `Sema` error; `grep -rc <exhaustive-check-fn> CG*.cpp CodeGen.cpp` == 1 site | rejected; single impl | U4, U8 |
| database (origin) | origin's DB codegen test (SQLite-guarded); `bcc migrate --preview` | exit 0 | U5, U8 |
| HTTP routing (origin) | HTTP-routing demo/test | builds + exit 0 | U6, U8 |
| demos | `make -C demos run` (incl. todo app build) | exit 0 | U7, U8 |

## Per-unit gates (every PR)
The reviewer independently runs, on the unit's branch (based on the integration
base): both parse/sema suites, `./test_codegen.sh`, the sema-enforcement check,
and the unit's own feature harness row(s). A unit must not regress any harness
that a prior unit made pass.

## Audit plan (instantiating the constitution)
| Audit | When | Rubric | Gates? |
|-------|------|--------|--------|
| spec audit | after each unit's speckit spec | covers the unit's REQ IDs with machine-checkable criteria; conforms to `design.md` decisions (local architecture is target; merge-commit; single match-exhaustiveness); scope = one subsystem | gates |
| code review | before each unit merges (to base) | no monolithic-`CodeGen.cpp` logic resurrected; origin feature routed through `Sema`/`CG*` correctly; no duplicate/dead code; prior harnesses still green; reviewer re-runs gates | gates |
| memory-safety | units touching codegen/runtime | `--leak-check` 0 leaks | gates |
| functional review | U8 | full epic done condition | gates |

## Regression protection (evolve)
- **Baselines to preserve (measured 2026-07-16 on local master):**
  `run_tests.sh` 186 (LLVM) / 181 (parse-only); `test_codegen.sh` 63/63 (57
  goldens); `ctest` 45; fuzz corpus 32; 26 `fail/sema` fixtures. Origin's DB
  codegen test + todo app (from origin/master) must also pass post-merge.
- **Must not change:** `CG*` module layout; `Sema`-in-all-modes; test-validation
  gate teeth; origin's public feature surface.

## Epic-level acceptance (run at U8)
```bash
set -e
# both build configs
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"
cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && cmake --build build-asan -j"$(nproc)"
# both suites + prior-epic gates
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh
./test_codegen.sh && ./test_codegen.sh --leak-check
test "$(ls test_files/fail/sema/*.b | wc -l)" -ge 26          # blang-ast enforcement intact
ctest --test-dir build
build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0
# origin features (each has a test/demo — see harness table)
make -C demos run                                             # incl. todo app build
# single match-exhaustiveness implementation (no dup)
# (reviewer confirms grep shows one site + origin's inline copy deleted)
# history: pushed, clean
git status -sb | grep -q 'up to date' && git status --porcelain | wc -l | grep -qx 0
```
