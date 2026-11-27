# Contract: LSP test tools (python3 stdlib only)

## `tools/lsp_client.py` (extended)

- **Environment hygiene.** The server is always spawned **without** `BLANG_STDLIB_DIR`; the caller's value is removed. A developer's environment therefore cannot change a golden.
- **New script step:** `{"server_env": {"NAME": "value", ...}}`. The value may contain `${ROOT}`. The whole script is read before the server is spawned, and every `server_env` step is applied to the child environment. During replay the step is a no-op.
- **New option:** `--stdlib-path DIR`, valid only with `--sema-fixture`. It sends `initializationOptions: {"stdlibPath": DIR}` in that mode's `initialize`.
- Everything else is unchanged: strict framing, `${ROOT}` scrubbing, the transcript format and the exit codes.

## `tools/lsp_diag_corpus.py` (new)

```text
lsp_diag_corpus.py --build-dir DIR --root ROOT --corpus stdlib [--list FILE] [--check-list] [--print-qualifying] [--only PATH]
```

- **Inputs.**
  - `DIR/blangd`, `DIR/bcc` and `DIR/qcc` must exist; a missing one is exit 2 with a message.
  - `--list` defaults to `ROOT/test_files/lsp/corpus.txt`.
- **Per file**, for each entry of the `--corpus` kind:
  - **Reference.** Run `DIR/bcc -v -S ROOT/<path> -o <tmp>.ll` in a temp cwd. Take the first line containing `/qcc ` and run it with `--json` appended.
  - **Premise.** No reference entry may be located in another file.
  - **blangd side.** In one shared blangd session, the server environment has `BLANG_STDLIB_DIR` removed. For each file the tool sends `didOpen`, waits for that uri's `publishDiagnostics`, then sends `didClose`.
  - **Compare** sorted multisets of `(severity, line, col, message)`, with LSP 0-based positions mapped to 1-based.
- **Output.** One line per file, `PASS <path> (<n> diagnostics)` or `FAIL <path>: <first difference>`, then a summary `corpus stdlib: <p> passed, <f> failed`.
- **`--check-list`.** Also fail when a qualifying file is absent from the list, or the list has a duplicate. The qualifying files are the six overview files, `examples/*/main.b`, and every `test_files/codegen_*.b` whose reference compile exits 0.
- **`--print-qualifying`.** Print the qualifying list in list-file format and exit 0. It is used once, to create the committed list.
- **Exit codes.** 0 when everything passes; 1 on any FAIL; 2 on a usage or environment error.

## `tools/lsp_latency.py` (new)

```text
lsp_latency.py --server BLANGD --root ROOT [--edits 20] [--budget-ms 500] FILE...
```

- **Method.** Per file, one blangd session:
  - `didOpen`, then wait for the publish;
  - then `--edits` full-text `didChange` edits, edit *i* appending `\n// edit <i>\n`;
  - each edit is timed from send to that uri's next `publishDiagnostics`, with `time.monotonic`.
- **Output.** `<file>: median <ms> ms (min <ms>, max <ms>, n=<edits>)`.
- **Exit codes.** 1 if any median exceeds the budget; 0 otherwise.
- The tool never decides to skip. `test_lsp.sh` skips the leg, with a printed notice, when `BUILD_DIR/CMakeCache.txt` has a non-empty `BLANG_SANITIZE`.

## New goldens (`test_files/lsp/`, all opening `src/prelude_use.b`)

| Fixture | `server_env` | `initializationOptions` | Pinned result |
|---|---|---|---|
| `stdlib_initopts` | `BLANG_STDLIB_DIR=${ROOT}/test_files/lsp/src` | `stdlibPath=${ROOT}/stdlib` | `diagnostics: []` |
| `stdlib_env` | `BLANG_STDLIB_DIR=${ROOT}/stdlib` | — | `diagnostics: []` |
| `stdlib_missing_env` | `BLANG_STDLIB_DIR=${ROOT}/test_files/lsp/src` | — | the warning (route `BLANG_STDLIB_DIR`, missing `sys.b`), then the single-file errors |
| `stdlib_relative` | — | — | `diagnostics: []` |
| `stdlib_missing` | — | `stdlibPath=${ROOT}/test_files/lsp/src` | the warning (route `initializationOptions.stdlibPath`), then the single-file errors |
