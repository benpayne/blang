# Contract: U3 test-tool additions

## `tools/lsp_diag_corpus.py --corpus project`

```text
lsp_diag_corpus.py --build-dir DIR --root ROOT --corpus project [--list FILE] [--check-list] [--only ENTRY]
```

- **Scratch tree.** Copy `ROOT/test_build`, `ROOT/examples` and `ROOT/test_files/{lsp/projects,fail/xmodule}` into `mkdtemp()`. Set `XDG_CACHE_HOME=<scratch>/cache` for **both** `bcc` and blangd. The repository is never written.
- **`project <dir>` entries.**
  - **Reference:** `DIR/bcc build -v <scratch>/<dir>`; the **last** line containing `/qcc ` is split with `shlex` and run with `--json`, with `cwd = <scratch>/<dir>`.
  - **blangd:** `didOpen <scratch>/<dir>/main.b` (disk text), in one session whose environment has `XDG_CACHE_HOME` set and `BLANG_STDLIB_DIR` removed.
  - **Compare:** sorted multisets of `(severity, line, col, message)` for entries located in `main.b`.
  - **Premise:** the reference exits 0 and reports no diagnostic located outside `main.b` (analyze L4).
- **`xmodule <fixture>` entries.**
  - **Layout:** synthesize `<scratch>/xm/<f>/lib/{blang.toml(name="lib", type="lib"), lib.b}` and `<scratch>/xm/<f>/app/{blang.toml(type="bin", [deps] lib={path="../lib"}), consumer.b}`.
  - **Reference:** `qcc --emit-bmod lib.bmod lib.b`, then `qcc --parse-only --json consumer.b lib.bmod`.
  - **Assertion:** every reference **error** located in `consumer.b` is in blangd's publish for `consumer.b`, and blangd publishes no **error** in `consumer.b` that the reference lacks (analyze M4). Extra warnings are tolerated and reported.
- **`--check-list`.**
  - **Project entries** must cover every `test_build/*` with `type = "bin"`, every `examples/*`, and `test_files/lsp/projects/multifile`.
  - **xmodule entries** must cover every `test_files/fail/xmodule/*`.
  - **Stdlib entries** must exclude `test_build/*` members.
- **Exit codes:** 0 when everything passes, 1 on any FAIL, 2 when `bcc`/`qcc`/`blangd` is missing or the build has no LLVM qcc or `llc`. The tool never skips; skipping is `test_lsp.sh`'s decision.

## `tools/lsp_client.py --server-env NAME=VALUE` (repeatable)

The option is applied after the script's `server_env` steps, with values `${ROOT}`-substituted. `BLANG_STDLIB_DIR` hygiene is unchanged.

## `test_lsp.sh` legs

| Leg | When | Pass condition |
|---|---|---|
| project goldens `project_*` (incl. `project_git_warm`, `project_git_unpinned`, `project_dep_compile_failed`) | always (both modes) | the transcripts match |
| hostile-input leg (5 MB garbage toml, toml-is-a-directory, symlink-loop dependency; built in a scratch dir) | always | each session exits 0 with strict framing and ≥ 1 published diagnostic (analyze M5) |
| git-shim | always | no `git-invoked` marker, and the transcript has exactly one `git dependency 'greet' is not fetched` diagnostic |
| project corpus | `$BUILD_DIR/qcc` has LLVM and `llc` is on PATH, and no `--no-project-corpus`; otherwise `SKIP` (a FAIL under `CI=true`) | `lsp_diag_corpus.py --corpus project --check-list` exits 0 |
| latency | non-sanitizer builds | `examples/todo_app/main.b` and `test_build/usebox/main.b` medians ≤ 500 ms |
