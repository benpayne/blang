# Evaluation: 001-toolchain-and-stdlib

**Epic**: [overview.md](overview.md) · **Constitution**: `.specify/memory/constitution.md`

Three-role audit: the **architect** gates Phase-2 (spec) for every unit; the
**independent code reviewer** gates Phase-4 (code) and merges; the manager gates
transitions. Commands are literal and resolvable (see Prerequisites).

## Prerequisites (declared env dependencies)

The gates below assume, on PATH: `gdb` (debugger smoke — **`lldb` is not
required and is not assumed**), `llvm-dwarfdump-18` (DWARF inspection), an
`llc-18` with the **aarch64 target compiled in** (cross-compile smoke — preflight
`llc-18 --version | grep -q aarch64` and skip that one gate if absent), plus
`file` and `python3`. The compiler binaries are at **`./build/qcc`** and
**`./build/bcc`** (there are no bare `qcc`/`bcc` on PATH — every command uses the
`./build/` prefix).

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema (LLVM) | `cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)" && ./run_tests.sh` | exit 0 | every unit |
| parse/sema (parse-only) | `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)" && BUILD_DIR=build-parse ./run_tests.sh` | exit 0 | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | exit 0 | every unit |
| runtime units | `ctest --test-dir build` | exit 0 | U4, U5, U6 |
| leak check | `./test_codegen.sh --leak-check` | exit 0, 0 leaks | units touching runtime/codegen |
| U0 no-op refactor | one `llc`-invocation helper (`grep -c` the old duplicated pattern → gone); suites byte-identical | refactor only | U0 |
| multi-error | `./build/qcc test_files/fail/multi/three_errors.b 2>&1 \| grep -Ec ':[0-9]+:[0-9]+: (error\|warning):'` ≥ 3 | ≥ 3 | U1, U6 |
| `--json` | `./build/qcc --json test_files/fail/multi/three_errors.b 2>&1 \| python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d)>=3 and all(k in d[0] for k in ("severity","file","line","col","message"))'` | exit 0 | U1, U6 |
| warning + `-Werror` | `./build/qcc <warn fixture>` exit 0 & prints `warning:`; `./build/qcc -Werror <warn fixture>` exit ≠ 0 | as stated | U1, U6 |
| opt correctness | build codegen suite with `./build/bcc -O2`; `./test_codegen.sh` (O2) | exit 0 | U2, U6 |
| opt delta | `opt-delta.md` exists with `-O2` vs `-O0` size (and/or perf) numbers | present | U2, U6 |
| `--release` | `./build/bcc --release demos/01_fibonacci.b -o /tmp/rel && /tmp/rel` | exit 0 | U2, U6 |
| cross-compile | `llc-18 --version \| grep -q aarch64 && ./build/bcc --target aarch64-unknown-linux-gnu -c test_files/pass/func_simple.b -o /tmp/x.o && file /tmp/x.o \| grep -qi aarch64` | exit 0 (or skip if no aarch64 target) | U2, U6 |
| debug info | `./build/bcc -g demos/01_fibonacci.b -o /tmp/fib && llvm-dwarfdump-18 /tmp/fib \| grep -q DW_TAG_subprogram && llvm-dwarfdump-18 --debug-line /tmp/fib \| grep -q 01_fibonacci.b` | exit 0 | U3, U6 |
| debug breakpoint smoke | `gdb -batch -ex 'break main' -ex run -ex bt /tmp/fib 2>&1 \| grep -q 'Breakpoint 1'` (function breakpoint = edit-stable vs a line number) | exit 0 | U3, U6 |
| `-g -O2` verifies | `./build/bcc -g -O2 demos/01_fibonacci.b -o /tmp/fibg` succeeds (module passes verifier) | exit 0 | U3, U6 |
| stdlib module | per module: `./build/bcc <import-<m> program>.b -o /tmp/m && /tmp/m` | exit 0 | U4, U5, U6 |
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

**Constitution Principle II carve-out (U0/U2/U3).** Principle II requires a
`codegen_*.b` E2E test per new *language feature*. U0 is a pure refactor (no
feature); U1's diagnostics are reject-only (negative fixtures under
`test_files/fail/multi/`, per the amended carve-out); U4/U5 add `codegen_*.b`.
**U2 (`-O`/`--release`/`--target`) and U3 (`-g`) are toolchain *flags*, not
language features** — their Principle-II obligation is discharged by running the
**existing suite built under the flag** (`-O2`/`-g`) plus the dwarfdump/gdb
smoke checks, not by a new `codegen_*.b`. An architect must not raise a
missing-`codegen_*.b` finding against U2/U3 on that basis.

**Note on the `specs/*` existence check** (done-condition #6): `ls specs/*…`
proves the artifacts *exist*, which is **necessary but not sufficient**. That the
architect *audited* them is process-enforced (manager-verified at Phase 2), and
each merged spec must carry a `Reviewed-by: architect` line the check greps for
(see acceptance block) so audit-passed is machine-evidenced, not just presence.

## Regression protection (evolve)
- **Baseline (measured at launch, recorded in status log):** `run_tests` LLVM +
  parse-only, `test_codegen` count (**107 @ launch**), `ctest` count. Only new
  failures are gate failures.
- **Must not change:** U0 preserves existing `.ll`/link behavior; the always-on
  stdlib set; existing goldens/leak/fuzz gates.

## Epic-level acceptance (run at U6)
```bash
set -e
# tool shims (see Prerequisites): binaries at ./build/, dwarfdump is versioned
QCC=./build/qcc; BCC=./build/bcc; DWARFDUMP="${DWARFDUMP:-llvm-dwarfdump-18}"
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"
cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"
# 1. correctness everywhere
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh && ./test_codegen.sh && ctest --test-dir build
./test_codegen.sh --leak-check
# (opt/-g suite builds are their own CI legs; U6 confirms each is green)
# 2. diagnostics: multi-error, JSON (WITH schema keys), warning + -Werror
$QCC test_files/fail/multi/three_errors.b 2>&1 | grep -Ec ':[0-9]+:[0-9]+: (error|warning):' | { read n; test "$n" -ge 3; }
$QCC --json test_files/fail/multi/three_errors.b 2>&1 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d)>=3 and all(k in d[0] for k in ("severity","file","line","col","message"))'
$QCC test_files/fail/warn/unused.b 2>&1 | grep -q 'warning:'          # warning emitted
if $QCC -Werror test_files/fail/warn/unused.b 2>/dev/null; then echo "FAIL: -Werror did not promote"; exit 1; fi
# 3. optimization: correctness at -O2 is its own CI leg; delta recorded; --release; cross-compile
test -f docs/epics/001-toolchain-and-stdlib/opt-delta.md
$BCC --release demos/01_fibonacci.b -o /tmp/rel && /tmp/rel
llc-18 --version | grep -q aarch64 && $BCC --target aarch64-unknown-linux-gnu -c test_files/pass/func_simple.b -o /tmp/x.o && file /tmp/x.o | grep -qi aarch64
# 4. debug info: DWARF present + line table names the .b + gdb breakpoint hits + -g -O2 verifies
$BCC -g demos/01_fibonacci.b -o /tmp/fib
$DWARFDUMP /tmp/fib | grep -q DW_TAG_subprogram
$DWARFDUMP --debug-line /tmp/fib | grep -q 01_fibonacci.b
gdb -batch -ex 'break main' -ex run -ex bt /tmp/fib 2>&1 | grep -q 'Breakpoint 1'
$BCC -g -O2 demos/01_fibonacci.b -o /tmp/fibg   # verifies clean under -O
# 5. stdlib (each module has a codegen test; run the module tests)
./test_codegen.sh test_files/codegen_math_*.b test_files/codegen_time_*.b test_files/codegen_random_*.b test_files/codegen_env_*.b test_files/codegen_sort_*.b test_files/codegen_hashmap_*.b test_files/codegen_flags_*.b
# 6. process + count: >=132 tests; each area's spec exists AND was architect-reviewed
test "$(ls test_files/codegen_*.b | wc -l)" -ge 132        # 107 + 25 (baseline pinned)
for a in diagnostics optimization debug stdlib; do ls specs/*"$a"* >/dev/null; grep -rq 'Reviewed-by: architect' specs/*"$a"*; done
# CI green on the final commit
gh run list --branch master --limit 1 --json conclusion --jq '.[0].conclusion' | grep -qx success
```
