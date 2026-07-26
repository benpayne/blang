# Code Audit — U3 Sanitizer build (`011-sanitizer-build`)

**Phase**: 4 (code audit, pre-merge) · independent reviewer pass; sanitizer build
re-created **from scratch** (`rm -rf build-asan`) and all gates re-run fresh.
**Gate**. Rubric: evaluation.md §Per-unit gates + §Audit plan + constitution.

## SC verification (re-run by reviewer)

| SC | Result | Evidence |
|----|--------|----------|
| SC-001 clean sanitizer build | **PASS** | `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && cmake --build build-asan -j"$(nproc)"` → exit 0, **0 build errors**; config logs `Sanitizers enabled: address,undefined`. |
| SC-002 `BUILD_DIR=build-asan ./run_tests.sh` | **PASS** | exit 0; **186/186**; zero ASan/UBSan diagnostics in the run. |
| SC-003 default boundary green | **PASS** | default `./run_tests.sh` 186/186; `./test_codegen.sh` 63/63 (57 golden-checked, 6 quarantined). |
| SC-004 teeth (injected fault caught) | **PASS** | see teeth section. |
| SC-005 flags on compile+link | **PASS** | `-fsanitize=address,undefined` on qcc `flags.make` AND `link.txt`, and on `blang_runtime` flags; `-fno-sanitize-recover=all` present. |

## Teeth verification (both sanitizers wired + FATAL)

- **ASan**: injected a heap-buffer-overflow (`p[7]` on a 1-byte malloc) at the top
  of `qcc.cpp:main`, rebuilt build-asan, ran `qcc` on a real test → **exit 134**
  with `AddressSanitizer: heap-buffer-overflow`. Reverted. ✅
- **UBSan**: injected a shift-exponent-too-large (`1 << 40`), rebuilt, ran → **exit
  1** with `runtime error: shift exponent 40 is too large for 32-bit type 'int'`.
  Exit 1 (not print-and-continue) confirms `-fno-sanitize-recover=all` makes UBSan
  **fatal**. Reverted. ✅
- Both faults were caught **with `ASAN_OPTIONS=detect_leaks=0`** in effect —
  proving F1: turning LeakSanitizer off does NOT suppress real memory errors or
  UB. Only leak *reporting* is disabled (U4's dedicated concern).

## Findings

- **F1 (spec audit) RESOLVED** — leaks-off is scoping, not masking: real ASan
  errors + UBSan still fatal (proven above); `run_tests.sh` output for the default
  build is unchanged (env vars are a no-op for uninstrumented binaries; default
  run still 186/186 with identical formatting).
- **F2 (spec audit) RESOLVED** — both ASan and UBSan proven wired and fatal.
- **F3 (spec audit) RESOLVED** — default `build/` has **no** `-fsanitize` in
  `flags.make`/`link.txt`; default build byte-for-byte unchanged.
- **Housekeeping** — added `build-asan/` to `.gitignore` so the sanitizer build
  dir is never committed (U4/U7 reuse it).
- No real memory/UB bug surfaced by the suite (clean run), so no in-scope fix or
  Open Question was needed. Tree verified clean after all probes (`qcc.cpp` ==
  HEAD; only the untracked, now-ignored `build-asan/` remains).

## Verdict

**PASS — approved for merge.** Opt-in `BLANG_SANITIZE` instruments compiler +
runtime on compile and link; `build-asan` builds clean and runs the parse suite
green (186/186) with both sanitizers proven to have teeth and UBSan fatal; the
default build and codegen suite are unchanged. Proceed to Phase 5.
