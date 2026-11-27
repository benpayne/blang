# Quickstart: validating U3 (blangd project context)

## 0. Build

```bash
cmake --build build -j && cmake --build build-nollvm -j && cmake --build build-asan -j
```

## 1. `bcc` is unchanged by the shared-helper move (constraint 3)

```bash
test_build/run_build_tests.sh          # all legs, incl. git dep, diamond, .bmod goldens
```

Then compare `bcc build -v` before and after on all 10 binaries, using a scratch copy and a scratch `XDG_CACHE_HOME`, with `cmp`.

**Expected**: green and identical.

## 2. Projects show exactly `bcc build`'s diagnostics

```bash
python3 tools/lsp_diag_corpus.py --build-dir build --root "$PWD" --corpus project --check-list
```

**Expected**: every `project` and `xmodule` entry is `PASS`. `myapp` and `counterapp` show 0 diagnostics. Before U3 they showed 15 and 8.

## 3. Broken setups

```bash
./test_lsp.sh test_files/lsp/project_missing_path.lsp.jsonl test_files/lsp/project_git_unfetched.lsp.jsonl \
              test_files/lsp/project_malformed_toml.lsp.jsonl test_files/lsp/project_cycle.lsp.jsonl
```

**Expected**: each matches its golden, with one setup diagnostic and the unrelated real error still present. The full `./test_lsp.sh` also runs the git-shim leg, which must print no `git-invoked` marker.

## 4. Unsaved dependency edits

```bash
./test_lsp.sh test_files/lsp/project_unsaved_dep.lsp.jsonl
```

**Expected**: the consumer is clean, then reports an error after the unsaved lib edit, then is clean again after the restore.

## 5. Latency, leaks, isolation

```bash
./test_lsp.sh        # latency leg prints usebox + todo_app medians <= 500 ms
ctest --test-dir build-asan -R "parser_no_leak|project_layout|blangd_no_subprocess" --output-on-failure
```

## 6. Everything else

```bash
./run_tests.sh; BUILD_DIR=build-nollvm ./run_tests.sh; BUILD_DIR=build-asan ./run_tests.sh
./test_codegen.sh && ./test_codegen.sh --leak-check && ./test_codegen.sh --selfcheck
./test_lsp.sh --selfcheck; BUILD_DIR=build-nollvm ./test_lsp.sh
```

**Expected**: all green. The U2 stdlib corpus stays green, now with 177 entries, because the two `test_build` files moved to the project corpus.
