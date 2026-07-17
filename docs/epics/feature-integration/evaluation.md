# Evaluation: feature-integration

**Epic**: [overview.md](overview.md) · **Constitution**: `.specify/memory/constitution.md`

Every unit is audited twice (spec audit before implementation, code audit before
merge) by an independent reviewer hire, gated by the manager. Commands are literal.

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema (LLVM) | `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh` | exit 0 | every unit |
| parse/sema (parse-only) | `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh` | exit 0 | every unit |
| codegen E2E + goldens | `./test_codegen.sh` (skips tests in `codegen_parked.txt`) | exit 0 (parked excluded until their owning unit lands) | every unit |
| sema enforcement intact | `ls test_files/fail/sema/*.b \| wc -l` ≥ 26 (rises as origin's cgfail non-exhaustive tests relocate); `BUILD_DIR=build-parse ./run_tests.sh` | fixtures still rejected | every unit |
| leak-check teeth | `./test_codegen.sh --leak-check` | exit 0, `Leaks: 0` | units touching codegen/runtime |
| runtime units | `ctest --test-dir build` | exit 0, ≥30 | U1, U5, U8 |
| fuzz replay | `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` | exit 0 | U8 |
| channels (origin) | `./test_codegen.sh test_files/codegen_channel.b test_files/codegen_channel_closed.b test_files/codegen_channel_spawn.b` | exit 0 | U2, U8 |
| event loop (origin) | `./test_codegen.sh test_files/codegen_timer_event.b test_files/codegen_timer_cancel.b test_files/codegen_timer_oneshot.b test_files/codegen_timer_helper.b` | exit 0 | U3, U8 |
| Option/Result + match (origin) | `./test_codegen.sh test_files/codegen_builtin_option.b test_files/codegen_builtin_result.b test_files/codegen_match_exhaustive.b test_files/codegen_match_wildcard_enum.b` | exit 0 | U4, U8 |
| match single-impl (dedup) | `test "$(grep -rl 'non-exhaustive match' Sema.cpp \| wc -l)" -eq 1 && ! grep -rE 'non-exhaustive' CodeGen.cpp CG*.cpp` | Sema only; none in codegen | U4, U8 |
| match rejected all-modes | relocated `fail/sema/*_non_exhaustive.b` → `BUILD_DIR=build-parse` rejects with `^[^:]+\.b:[0-9]+:[0-9]+: error: ` | rejected in parse-only | U4, U8 |
| database (origin) | `./test_codegen.sh test_files/codegen_db_query.b test_files/codegen_db_query_rows.b` (SQLite-guarded); `bcc migrate --preview` | exit 0 | U5, U8 |
| todo app E2E | `bash examples/todo_app/test_todo_app.sh` | exit 0 | U5, U8 |
| HTTP routing (origin) | `./test_codegen.sh test_files/codegen_http_routing.b test_files/codegen_http_json_response.b test_files/codegen_to_json_builtin.b` | exit 0 | U6, U8 |
| demos build + run | `make -C demos all` (builds NET_DEMOS incl. 13_http_server, 15_timer); `make -C demos run` (runs 01–10) | exit 0 | U7, U8 |

## Per-unit gates (every PR)
The reviewer independently runs, on the unit's branch (based on the integration
base): both parse/sema suites, `./test_codegen.sh` (which excludes
`codegen_parked.txt`), the sema-enforcement check, and the unit's own feature
harness row(s). A unit must not regress any harness that a prior unit made pass.

**U1 exception:** at U1 the parked-skip mechanism does not yet exist until U1
builds it, so U1's gate is: both `run_tests.sh` modes green, and
`./test_codegen.sh` green *once `codegen_parked.txt` is populated* (origin's
feature tests parked). Each of U2–U6 must remove its own tests from
`codegen_parked.txt` (burn-down); U8 requires the file empty.

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
# origin features — named tests (see harness table)
./test_codegen.sh test_files/codegen_channel.b test_files/codegen_channel_closed.b test_files/codegen_channel_spawn.b
./test_codegen.sh test_files/codegen_timer_event.b test_files/codegen_timer_cancel.b test_files/codegen_timer_oneshot.b test_files/codegen_timer_helper.b
./test_codegen.sh test_files/codegen_builtin_option.b test_files/codegen_builtin_result.b test_files/codegen_match_exhaustive.b test_files/codegen_match_wildcard_enum.b
./test_codegen.sh test_files/codegen_db_query.b test_files/codegen_db_query_rows.b       # sqlite-guarded
./test_codegen.sh test_files/codegen_http_routing.b test_files/codegen_http_json_response.b test_files/codegen_to_json_builtin.b
bash examples/todo_app/test_todo_app.sh                        # todo E2E (build->migrate->run)
make -C demos all                                             # NET_DEMOS build (13_http_server, 15_timer)
make -C demos run                                             # runs 01-10
# parked burn-down complete + single match-exhaustiveness implementation
test "$(grep -vcE '^\s*#|^\s*$' test_files/codegen_parked.txt)" -eq 0   # empty
test "$(grep -rl 'non-exhaustive match' Sema.cpp | wc -l)" -eq 1        # Sema only
! grep -rE 'non-exhaustive' CodeGen.cpp CG*.cpp                         # none in codegen
# history: pushed, clean
git status -sb | grep -q 'up to date' && git status --porcelain | wc -l | grep -qx 0
```
