# U4 qcc byte-for-byte equivalence vs the 043 qcc (T009/T010/T049)

Reference: `/tmp/blang-043`, a detached worktree of `7e6183c` (branch
`043-lsp-project-context`, not modified). It was built twice: `build/` (LLVM)
and `build-nollvm/` (`-DBLANG_ENABLE_LLVM=OFF`).

The harness is `specs/044-lsp-xmodule-navigation/qcc_equivalence.sh`. It takes
the `qcc` lines once from the LLVM build's `bcc` and replays each line against
both binaries of a pair, adding `--parse-only` for the non-LLVM pair. For every
input it compares the exit code, stdout, stderr and every output file with
`cmp`-style byte equality. It fails if category (a), (c) or (d) has 0 inputs.

## T009: after shared-compiler changes A (useCapabilityTypes lift) and B (type-reference recording)

| Pair | (a) codegen_*.b | (b) pass/fail/sema/cgfail | (c) lib `--emit-bmod` | (d) bin final line | Result |
|------|----------------:|--------------------------:|----------------------:|-------------------:|--------|
| LLVM `build/` vs 043 `build/` | 172 | 233 | 9 | 10 | `EQUIV: 424/424 identical (llvm)` |
| parse-only `build-nollvm/` vs 043 `build-nollvm/` | 172 | 233 | 9 | 10 | `EQUIV: 424/424 identical (parse-only)` |

Harness teeth: `qcc_equivalence.sh --selfcheck` printed `SELFCHECK: OK (corrupted reference detected at codegen_and_call.b: file codegen_and_call.ll)` and exited non-zero, as designed.

## T010/T050: regression gates (this branch, after all U4 edits)

| Gate | Result |
|------|--------|
| `./run_tests.sh` (LLVM) | 251 passed / 0 failed (043 baseline: 251) |
| `BUILD_DIR=build-nollvm ./run_tests.sh` | 244 / 0 (baseline 244) |
| `./test_codegen.sh` | 172 / 0 (baseline 172) |
| `./test_codegen.sh --leak-check` | `Leaks: 0` |
| `./test_codegen.sh --selfcheck` | `SELFCHECK: OK` |
| `test_build/run_build_tests.sh` | `All build-system checks passed` |
| `ctest --test-dir build` / `build-asan` | 87/87 / 87/87 (including `parser_no_leak` with the U4 navigation loop under LSan, `resolver_reuse`, `blangd_no_subprocess` + `_teeth`) |
| `tools/check_no_field_reachins.sh examples test_build` | `OK` |
| `./test_lsp.sh` | 114 / 0 (pre-U4 92; +22 `xmod_*`). Corpus stdlib `177 passed, 0 failed`; corpus project `29 passed, 0 failed` (`== bcc build`, llc present). `PASS git-shim: one 'not fetched' diagnostic, git never invoked`. Latency medians: todo_app 78.4 ms, usebox 75.9 ms (budget 500 ms) |
| `./test_lsp.sh --selfcheck` | `SELFCHECK: OK` |
| `.bmod`-URI scan `grep -l '\.bmod"' test_files/lsp/xmod_*.expected.out` | empty |
| Pre-existing goldens | no tracked `test_files/lsp/*.expected.out` changed; `lsp/AstLocator.*` unchanged vs `043-lsp-project-context` |

Both corpus legs are unchanged with type-reference recording enabled on every document compile. They are the F5 proof that recording changes no diagnostic.

### T048: `BUILD_DIR=build-asan ./test_lsp.sh`

All 22 `xmod_*` goldens pass under ASan. The run reports 105 passed and 8 failed. Every failure is a LeakSanitizer report from a **parser error path**, outside any U4 code:

- `ReturnStatement::Parse` and `StringInterpolation::Parse` leak on a parse error;
- the affected legs are `diag_change`, `diag_open`, `stdlib_missing_env`, `stdlib_missing`, the `fail/sema` re-verification of `interp_unknown_base`, `undefined_function` and `undefined_variable`, and the project corpus.

These failures are **pre-existing**. The same 8 fail identically on the 043 baseline: an ASan `blangd`/`qcc`/`bcc` built from the `7e6183c` worktree reproduces the 7 transcript and `fail/sema` legs and the project-corpus leg, each with `LeakSanitizer: detected memory leaks`. This is the "U2 known 8 parse-error leaks" that U3's final verification recorded. The latency leg is skipped under a sanitizer build, by design.

## T049: final re-run (after every U4 edit)

| Pair | (a) | (b) | (c) | (d) | Result |
|------|----:|----:|----:|----:|--------|
| LLVM | 172 | 233 | 9 | 10 | `EQUIV: 424/424 identical (llvm)` |
| parse-only | 172 | 233 | 9 | 10 | `EQUIV: 424/424 identical (parse-only)` |
