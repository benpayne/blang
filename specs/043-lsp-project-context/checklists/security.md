# U3 security review (plan D-9; Quality Gate 7)

## T034: hostile-input leg (`test_lsp.sh`, committed; both build modes)

Each project is built in a `mktemp -d` scratch tree, and its `main.b` is opened through `lsp_client.py --sema-fixture`. Strict framing is on, so any stray stdout or stderr byte fails the session.

| Project `blang.toml` | Result (`build` and `build-nollvm`) |
|---|---|
| 5 MB of seeded pseudo-random bytes | PASS: exit 0, strict framing. 12939 diagnostics: one `parse error` Warning per unparseable line (the same body `bcc` prints) plus the document's own errors. Takes about 1 s. |
| a **directory** | PASS: exit 0. Pins N4 exactly: `<toml>: cannot read blang.toml` Warning at 1:1, then the single-file context (the document's own errors plus `unknown module 'loop'`). |
| a dependency that is a **symlink loop** (`loop -> loop`) | PASS: exit 0. One `dependency 'loop' cannot be resolved: '<dir>/../loop' has no blang.toml (declared at <toml>:6)` Error at the import, plus the document's own error. `realpath`/`stat` fail with ELOOP and go down the missing-path route. |

The `project_dep_compile_failed` golden pins the dependency-compile-failure path (T032).

## T033: git-shim leg

A tripwire `git` (`touch git-invoked; exit 1`) is put first on blangd's `PATH` with `--server-env`, and `project_git_unfetched` is opened.

- Result in both modes: **PASS**. Exactly one `git dependency 'greet' is not fetched` diagnostic, and `git-invoked` is never created.
- `sh tools/check_no_subprocess.sh build/blangd` → `no-subprocess: OK`.
- `sh tools/check_no_subprocess.sh build-nollvm/blangd` → `no-subprocess: OK`.
