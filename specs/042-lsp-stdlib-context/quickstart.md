# Quickstart: validating U2 (blangd stdlib/prelude context)

## 0. Build (all three trees)

```bash
cmake --build build -j && cmake --build build-nollvm -j && cmake --build build-asan -j
```

## 1. The editor now agrees with the compiler

```bash
python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus stdlib --check-list
python3 tools/lsp_diag_corpus.py --build-dir build-nollvm --root "$PWD" --corpus stdlib --check-list   # parse-only (CI lsp job)
```

**Expected**: every line reads `PASS`, and the summary is `0 failed`. The four clean overview files show `(0 diagnostics)`. Before U2 they showed 4–16 false errors each.

## 2. Discovery routes

```bash
./test_lsp.sh test_files/lsp/stdlib_initopts.lsp.jsonl test_files/lsp/stdlib_env.lsp.jsonl \
              test_files/lsp/stdlib_missing_env.lsp.jsonl test_files/lsp/stdlib_relative.lsp.jsonl \
              test_files/lsp/stdlib_missing.lsp.jsonl
```

**Expected**: all 5 match their goldens.

**Manual check** of the not-found route: set `BLANG_STDLIB_DIR=/nonexistent`, run blangd from your editor, and open any `.b` file. You get one warning at 1:1, `stdlib not found: BLANG_STDLIB_DIR '/nonexistent' … (missing directory)`.

## 3. Full LSP suite

```bash
./test_lsp.sh            # transcripts + fail/sema leg + corpus leg + latency leg
./test_lsp.sh --selfcheck
BUILD_DIR=build-asan ./test_lsp.sh   # latency leg prints a SKIP notice
```

**Expected**:
- every pre-existing golden is unchanged;
- all 58 `fail/sema` fixtures pass, with `buffer_without_prelude.b` run via the not-found route;
- the latency leg prints `examples/todo_app/main.b: median … ms` at or under 500;
- `--selfcheck` prints `SELFCHECK: OK`.

## 4. Ownership / memory

```bash
ctest --test-dir build-asan -R "parser_no_leak|stdlib_locate|project_compile|blangd_no_subprocess" --output-on-failure
```

**Expected**: all pass, and `parser_no_leak`'s ≥ 50-reparse stdlib loop is LSan-clean.

## 5. `qcc`/`bcc` unchanged (the D-1 helper move)

```bash
./run_tests.sh && BUILD_DIR=build-nollvm ./run_tests.sh && BUILD_DIR=build-asan ./run_tests.sh
./test_codegen.sh && ./test_codegen.sh --leak-check && ./test_codegen.sh --selfcheck
test_build/run_build_tests.sh
ctest --test-dir build && ctest --test-dir build-asan && ctest --test-dir build-nollvm
```

Also compare `bcc -v -S <file>` before and after this unit on the six overview files with `cmp`, as in U1's quickstart §3.

**Expected**: everything green, no golden edits, and `cmp` identical.
