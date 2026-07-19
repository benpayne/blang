# Evaluation: 001-toolchain-and-stdlib

**Epic**: [overview.md](overview.md) · **Constitution**: `.specify/memory/constitution.md`

Three-role audit: the **architect** gates Phase-2 (spec) for every unit; the
**independent code reviewer** gates Phase-4 (code) and merges; the manager gates
transitions. Commands are literal.

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema (LLVM) | `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh` | exit 0 | every unit |
| parse/sema (parse-only) | `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh` | exit 0 | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | exit 0 | every unit |
| runtime units | `ctest --test-dir build` | exit 0 | U4, U5, U6 |
| leak check | `./test_codegen.sh --leak-check` | exit 0, 0 leaks | units touching runtime/codegen |
| U0 no-op refactor | one `llc`-invocation helper (`grep -c` the old duplicated pattern → gone); suites byte-identical | refactor only | U0 |
| multi-error | `qcc test_files/fail/multi/three_errors.b 2>&1 \| grep -Ec ':[0-9]+:[0-9]+: (error\|warning):'` ≥ 3 | ≥ 3 | U1, U6 |
| `--json` | `qcc --json test_files/fail/multi/three_errors.b 2>&1 \| python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d)>=3 and all(k in d[0] for k in ("severity","file","line","col","message"))'` | exit 0 | U1, U6 |
| warning + `-Werror` | `qcc <warn fixture>` exit 0 & prints `warning:`; `qcc -Werror <warn fixture>` exit ≠ 0 | as stated | U1, U6 |
| opt correctness | build codegen suite with `bcc -O2`; `./test_codegen.sh` (O2) | exit 0 | U2, U6 |
| opt delta | `opt-delta.md` exists with `-O2` vs `-O0` size (and/or perf) numbers | present | U2, U6 |
| cross-compile | `bcc --target aarch64-unknown-linux-gnu -c test_files/pass/func_simple.b -o /tmp/x.o && file /tmp/x.o \| grep -qi aarch64` | exit 0 | U2, U6 |
| debug info | `bcc -g demos/01_fibonacci.b -o /tmp/fib && llvm-dwarfdump /tmp/fib \| grep -q DW_TAG_subprogram && llvm-dwarfdump --debug-line /tmp/fib \| grep -q 01_fibonacci.b` | exit 0 | U3, U6 |
| debug breakpoint smoke | scripted `gdb -batch -ex 'break 01_fibonacci.b:<line>' -ex run -ex bt /tmp/fib` hits the breakpoint | breakpoint hit | U3, U6 |
| `-g -O2` verifies | `bcc -g -O2 demos/01_fibonacci.b -o /tmp/fibg` succeeds (module passes verifier) | exit 0 | U3, U6 |
| stdlib module | per module: `bcc <import-<m> program>.b -o /tmp/m && /tmp/m` | exit 0 | U4, U5, U6 |
| new-test count | `test "$(ls test_files/codegen_*.b \| wc -l)" -ge 132` (baseline 107 + 25) | exit 0 | U6 |

## Per-unit gates (every PR)
The code reviewer independently runs: both parse/sema suites, `./test_codegen.sh`,
the unit's own harness rows, and `--leak-check` for units touching runtime/codegen
ARC. No test committed failing.

## Audit plan (instantiating the constitution — 3 roles)
| Audit | When | Who | Rubric | Gates? |
|-------|------|-----|--------|--------|
| **spec audit** | after each unit's speckit spec | **architect** | covers the unit's REQ IDs with machine-checkable criteria; conforms to this epic's design docs; **threads through U0's single flag/link path** (no re-duplication); cross-area coherence (e.g. `-g`×`-O` stance honored, `--json` schema consistent, stdlib import-gated); scope not silently cut | gates |
| **code review** | before each unit merges | **code reviewer** (independent) | constitution code standard; the check has teeth; correctness under `-O2`/`-g` where relevant; ARC survives; reviewer re-runs gates | gates |
| memory-safety | U2/U3/U4/U5 | code reviewer | `--leak-check`/ctest-ASan output attached, 0 leaks | gates |
| functional review | U6 | architect + reviewer | full epic done condition | gates |

## Regression protection (evolve)
- **Baseline (measured at launch, recorded in status log):** `run_tests` LLVM +
  parse-only, `test_codegen` count (**107 @ launch**), `ctest` count. Only new
  failures are gate failures.
- **Must not change:** U0 preserves existing `.ll`/link behavior; the always-on
  stdlib set; existing goldens/leak/fuzz gates.

## Epic-level acceptance (run at U6)
```bash
set -e
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"
# 1. correctness everywhere
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh && ./test_codegen.sh && ctest --test-dir build
./test_codegen.sh --leak-check
# (opt/-g suite builds are their own CI legs; U6 confirms each is green)
# 2. diagnostics
qcc test_files/fail/multi/three_errors.b 2>&1 | grep -Ec ':[0-9]+:[0-9]+: (error|warning):' | { read n; test "$n" -ge 3; }
qcc --json test_files/fail/multi/three_errors.b 2>&1 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d)>=3'
# 3. optimization
test -f docs/epics/001-toolchain-and-stdlib/opt-delta.md
bcc --target aarch64-unknown-linux-gnu -c test_files/pass/func_simple.b -o /tmp/x.o && file /tmp/x.o | grep -qi aarch64
# 4. debug info
bcc -g demos/01_fibonacci.b -o /tmp/fib && llvm-dwarfdump /tmp/fib | grep -q DW_TAG_subprogram
bcc -g -O2 demos/01_fibonacci.b -o /tmp/fibg   # verifies clean
# 5. stdlib (each module has a codegen test; run the module tests)
./test_codegen.sh test_files/codegen_math_*.b test_files/codegen_time_*.b test_files/codegen_random_*.b test_files/codegen_env_*.b test_files/codegen_sort_*.b test_files/codegen_hashmap_*.b test_files/codegen_flags_*.b
# 6. process + count
test "$(ls test_files/codegen_*.b | wc -l)" -ge 132        # 107 + 25
ls specs/*diagnostics* specs/*optimization* specs/*debug* specs/*stdlib* >/dev/null   # speckit artifacts committed
# CI green on the final commit
gh run list --branch master --limit 1 --json conclusion --jq '.[0].conclusion' | grep -qx success
```
