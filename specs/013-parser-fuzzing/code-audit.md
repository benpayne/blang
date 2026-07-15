# Code Audit — U5 Parser fuzzing (`013-parser-fuzzing`)

**Phase**: 4 (code audit, pre-merge) · independent reviewer pass; `build-fuzz`
re-created from scratch (`rm -rf`) and all gates re-run from a clean state.
**Gate**. Rubric: evaluation.md §Per-unit gates + §Audit plan + constitution.
PM directive honored: distinct implement vs audit passes, fresh rebuilds, reviewer
is the only merging actor.

## SC verification (re-run by reviewer, fresh clang build)

| SC | Result | Evidence |
|----|--------|----------|
| SC-001 fuzz_parse builds | **PASS** | clean `build-fuzz` (clang, BLANG_ENABLE_LLVM=OFF, BLANG_FUZZ=ON): configure+build exit 0, 0 errors; `build-fuzz/fuzz_parse` present. |
| SC-002 corpus ≥ 20 | **PASS** | `ls test_files/fuzz/corpus \| wc -l` = **31** (30 pass/fail seeds + 1 regression). |
| SC-003 corpus replay | **PASS** | `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` → exit 0 ("Done 63 runs"). |
| SC-004 poison teeth | **PASS** | see teeth section. |
| SC-005 bounded campaign | **PASS** | 60s campaign (temp corpus) ran **25,527** units, **no crashes**; target supports `-max_total_time=60` for U7. |
| SC-006 boundary green | **PASS** | run_tests 186, parse-only 181, test_codegen 63/63, `BUILD_DIR=build-asan ./run_tests.sh` 186. |

## Teeth verification

- **Poison reaches the parser (SC-004).** Injected a temporary abort in
  `FunctionDefinition::Parse` keyed on the poison's canary name
  (`__fuzz_reaches_parser__`), rebuilt fuzz_parse → `fuzz_parse
  test_files/fuzz/poison.b` **crashes** (libFuzzer "deadly signal", abort in
  `QFunctionDefinition.cpp`), proving the poison bytes flowed lexer→parser. A
  **non-poison** seed does **not** crash with the same break (exit 0) — the crash
  is specific to the poison's content, not an unconditional break (authenticity).
  Reverting the break → poison parses cleanly (exit 0).
- **The surfaced-bug fix has teeth.** Reverting the `CompilerHelpers.h` null-guard
  and rebuilding qcc makes the committed regression input
  (`regression_null_param_type.b`) **SEGV again (exit 139)**; with the fix it
  exits 255 (a clean located error, not a crash). So the fix is real and the
  regression case guards it.
- **Harness does not mask faults (F2).** The `catch(...)` swallows only C++
  `CompileError`/exceptions (controlled parse errors); the poison's abort and the
  null-deref SEGV are signals, which propagate to libFuzzer as crashes — not
  eaten. Confirmed by both crashes above being reported.
- **Leak-clean replay (F4).** `-runs=0` over 31 inputs exits 0 under ASan
  (LSan disabled via compiled-in `__asan_default_options` — leaks are U4's gate,
  not the parser fuzzer's; SmartPtr-managed AST keeps per-input memory bounded).

## Bug surfaced and fixed (the point of fuzzing)

The first replay surfaced a genuine `qcc` **heap-use-after-free** (harness bug:
raw-pointer `gScope` reused across inputs — fixed with a SmartPtr keepalive), and
the 60s campaign then surfaced a **real parser SEGV**: formatting a function with
a **null-typed parameter** (`fn f(BAD) -> X;`) via the `SmartPtr<T>` stream
operator dereferenced null. Fixed with a null-guard in `CompilerHelpers.h`
(prints `(null)` instead of crashing); qcc now reports an error instead of
crashing. Crashing input committed as a regression corpus case. Not large/risky —
a one-line defensive guard — so fixed in scope, no Open Question needed.

## Findings

- **F1 (path correction) RESOLVED** — `build-fuzz` (clang) is the fuzz build;
  `evaluation.md` fuzz row + acceptance aligned to `build-fuzz/fuzz_parse` with a
  documented rationale (a libFuzzer target cannot live in the gcc default
  `build/`, same as `build-asan`). Teeth (corpus replay + poison) unchanged.
- **F2/F3/F4 RESOLVED** — above.
- No blocking findings. Tree verified clean after all probes
  (`QFunctionDefinition.cpp`, `CompilerHelpers.h` == HEAD; only ignored build dirs
  and this spec dir untracked).

## Verdict

**PASS — approved for merge.** fuzz_parse builds; 31-input corpus replays
crash-free; the poison proves the harness reaches the parser; a real parser SEGV
was surfaced and fixed with a regression guard; a 60s campaign is clean; boundary
green. Proceed to Phase 5.
