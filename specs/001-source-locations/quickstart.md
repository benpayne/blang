# Quickstart: Validating U1 Source Locations

**Feature**: specs/001-source-locations/spec.md

## Prerequisites

- Linux with cmake ≥ 3.16, a C++17 compiler, and `llvm-18-dev` (for the
  LLVM-build gate; the parse-only gate needs no LLVM).
- Repo root, branch `epic/blang-ast/u1-source-locations`.

## Build (both modes)

```bash
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
cmake --build build -j"$(nproc)"

cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF
cmake --build build-parse -j"$(nproc)"
```

## Validate the feature

```bash
# 1. Golden-location diffs (the unit's core acceptance; see contracts/)
build/qcc --dump-locations test_files/pass/func_simple.b \
  | diff - test_files/golden/func_simple.locations
build/qcc --dump-locations test_files/pass/match_basic.b \
  | diff - test_files/golden/match_basic.locations

# 2. No zero line/col anywhere in either dump
for f in func_simple match_basic; do
  build/qcc --dump-locations test_files/pass/$f.b \
    | grep -E ':0:[0-9]+ |:[0-9]+:0 ' && echo "FAIL: unset location" && exit 1
done; echo OK

# 3. Determinism: two runs, identical bytes
diff <(build/qcc --dump-locations test_files/pass/match_basic.b) \
     <(build/qcc --dump-locations test_files/pass/match_basic.b)

# 4. Parse-only build produces the identical dump (no LLVM dependency)
diff <(build/qcc --dump-locations test_files/pass/match_basic.b) \
     <(build-parse/qcc --dump-locations test_files/pass/match_basic.b)

# 4b. Corpus-wide FR-004 smoke check: no AST node in ANY pass file has a
#     zero line/col (goes beyond the two goldens).
fail=0
for f in test_files/pass/*.b; do
  if build/qcc --dump-locations "$f" 2>/dev/null \
       | grep -qE ':0:[0-9]+ |:[0-9]+:0 '; then
    echo "ZERO-LOC in $f"; fail=1
  fi
done; test "$fail" -eq 0 && echo "corpus: no zero locations"

# 5. Token-accurate error locations (behavioral spot check): a fail-suite
#    file still fails, and the reported line matches the offending token
build/qcc test_files/fail/missing_brace.b; echo "exit=$? (expect non-zero)"
```

## Regression gates (must all stay green — constitution Principle II)

```bash
# Gate A — LLVM build, full suites
./run_tests.sh && ./test_codegen.sh

# Gate B — parse-only build
BUILD_DIR=build-parse ./run_tests.sh
```

## Expected outcomes

- Both golden diffs and both determinism/parity diffs: empty output, exit 0.
- Zero-location grep: no matches.
- Suites: same pass counts as launch baseline (162 parse tests LLVM /
  154 parse-only; 63 codegen E2E).
- Hand spot-check (SC-005): open `test_files/pass/match_basic.b`, count by
  eye the line/column of ≥ 5 constructs of different node kinds, and
  confirm the dump agrees.
