# U2 baseline and post-unit record

- **Base**: U1 tip `5d9b7b6` (branch `041-shared-project-compile`, PR #158). `git merge-base --is-ancestor 5d9b7b6 HEAD` succeeds. Since `5d9b7b6`, the only files outside `specs/` that changed before implementation are under `.specify/`, so U1's final green run (recorded in `specs/041-shared-project-compile/checklists/baseline.md`) is the pre-unit baseline:
  - `run_tests.sh`: 251/0 (LLVM), 244/0 (parse-only), 251/0 (asan);
  - `test_codegen.sh`: 172/0, with Leaks: 0;
  - `ctest`: 85/85 in each of the three trees;
  - `test_lsp.sh`: 70/0.
- **D-1 neutrality (T003/T006).** I ran the pre-unit `bcc`/`qcc` against the post-D-1 `bcc`/`qcc`, from the same directory layout (`/tmp/u2-bin`, with the same stdlib path so digests compare equal), using `bcc -v -S` on the six overview files. stdout (including the IR), stderr and the exit code are identical for **12/12** outputs. I confirmed by `cmp` that the two binaries differ.

## Post-unit gates (final tree)

| Gate | Result |
|---|---|
| `run_tests.sh` build / build-nollvm / build-asan | 251/0 · 244/0 · 251/0 |
| `test_codegen.sh` · `--leak-check` · `--selfcheck` | 172/0 · Leaks: 0 · SELFCHECK: OK |
| `test_build/run_build_tests.sh` | all pass |
| `ctest` build / build-nollvm / build-asan | 86/86 each: `stdlib_locate` (new); `parser_no_leak` with the 50-reparse stdlib loop, LSan-clean; `project_compile`; `blangd_no_subprocess` + teeth |
| `./test_lsp.sh` (build) / `BUILD_DIR=build-nollvm` | **78/0** each: 12 pre-existing transcripts unchanged; 6 new `stdlib_*` goldens; 58 `fail/sema`; corpus leg (179/179); latency leg (todo_app median ~77 ms) |
| `./test_lsp.sh --selfcheck` | SELFCHECK: OK |
| reach-in gate, examples (6), demos | OK · 6/6 · 10/0 |
| Pre-existing golden diffs | **none**. No pre-existing LSP golden was regenerated or changed. |

## Extra leg, not a listed gate: `BUILD_DIR=build-asan ./test_lsp.sh`

The suite fails 8 entries: `diag_open`, `diag_change`, three `fail/sema` fixtures, `stdlib_missing`, `stdlib_missing_env`, and the corpus leg's session exit. **Each one is the pre-existing parse-error-recovery leak.** A raw `new` in `ReturnStatement::Parse`/`CallExpression::Parse` is lost when `COMPILE_ERROR` throws. It surfaces as an LSan report on blangd's stderr at exit, for any document with a parse error.

- U1 recorded the first five against the **pre-unit** ASan blangd.
- The new three are documents with parse errors: the single-file fallback goldens, and `test_build/{myapp,counterapp}/main.b` single-file.
- U2 changed no parser, Sema or `ProjectCompile` code since `5d9b7b6` (`git diff --stat 5d9b7b6 HEAD -- 'Q*.cpp' Expression.h Type.h Sema.cpp ProjectCompile.cpp` is empty).
- Plain `qcc --parse-only test_build/myapp/main.b` shows the same leak at `QExpression.cpp:1093`.
- With the oracle fixed (`detect_leaks=0` for the reference children only), **all 179 corpus files PASS under the ASan blangd as well**.

This was filed as a follow-up finding, not absorbed. A long-lived blangd leaks per reparse while a document has parse errors, so it bears on C3 (bounded memory) and belongs with U3's leak work or its own fix.
