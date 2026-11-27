# U3 security review (plan D-9; Quality Gate 7)

## Input handling (with citations)

- [X] **Read-only access.** blangd's project context only does `stat`, `opendir`/`readdir`, `realpath` and reads:
  - every project, dependency and `blang.toml` file is read with `lsp::readThroughOverlay` (`lsp/ProjectContext.cpp:18-35`): the open buffer first, else `stat` + `S_ISREG` + `ifstream` (directories and devices are refused);
  - directory listing uses `BlangProject::discoverSourceFiles` (`ProjectLayout.cpp:26`, `opendir`);
  - project discovery uses `findProjectRoot` (`ProjectLayout.cpp:181`, `stat`);
  - the git warm check uses `locateGitCheckout` (`ProjectLayout.cpp:111`, `stat`).
  There is no write, create, rename or remove anywhere in `lsp/ProjectContext.cpp`, `lsp/Compile.cpp` or `ProjectLayout.cpp`. Interfaces live only in memory: blangd never writes a `.bmod`.
- [X] **`resolvePath` parity.** Path dependencies resolve only through the shared `BlangProject::resolvePath` (`ProjectLayout.cpp:49-63`), the function moved verbatim from `bcc` (T007: 28/28 `bcc` outputs `cmp`-identical), called from `resolveDependencyDir` (`ProjectLayout.cpp:123`). Absolute paths and `..` behave exactly as in `bcc`; per the parity rule there is no extra policy. A symlink loop fails `realpath`/`stat` with ELOOP and is reported as a missing dependency (T034).
- [X] **The git URL and pin are used only as hash input and text.**
  - `locateGitCheckout` (`ProjectLayout.cpp:99-113`) uses `dep.gitUrl + "@" + pin` only as `shortHashHex` input to name the cache directory, and the pin check yields `bcc`'s message.
  - `resolveDependencyDir` (`ProjectLayout.cpp:127-152`) only fetches when a `Fetcher` is passed. `bcc` passes its git fetch (`bcc.cpp:1015`); blangd always passes `nullptr` (`lsp/ProjectContext.cpp:321`), so a cold checkout is `kUnfetched`, and is reported, never cloned.
  - Neither the URL nor the pin reaches a shell or a process.
  - Residual risk (parity, accepted): as in `bcc`, the `[deps]` key is part of the cache directory name, so a hostile key can only make blangd **read** some other directory. It can never execute anything there, and the same reach already exists through an absolute `path =`.
- [X] **Issue sink, no stderr.**
  - `ProjectConfig::loadFromText( path, text, issues* )` sends every parse issue to the sink when one is given (`ProjectConfig.cpp:94-101`, `reportIssue`). Only the `bcc` path (`loadFromFile` with no sink) prints to `cerr`, with the identical text.
  - blangd always passes a sink: the root toml becomes Warnings (`lsp/ProjectContext.cpp:284-292`), and a dependency's toml issues are dropped (`lsp/ProjectContext.cpp:74`).
  - Strict framing in every `test_lsp.sh` session (any stderr byte fails) holds this for all 8 `project_*` goldens and the hostile leg.
- [X] **No process calls.**
  - `grep -nE "runCommand|popen|fork\(|exec[lv]p?\(|system\(|posix_spawn|socket\(" ProjectLayout.cpp lsp/*.cpp lsp/*.h` matches only the comment `ProjectLayout.cpp:3` ("Process-free: no fork/exec/system/popen, no network.").
  - `ctest -R blangd_no_subprocess` gives `blangd_no_subprocess` Passed and `blangd_no_subprocess_teeth` Passed.
  - `sh tools/check_no_subprocess.sh build/blangd` and `sh tools/check_no_subprocess.sh build-nollvm/blangd` both print `no-subprocess: OK`.
  - The git-shim leg below adds runtime proof.
- [X] **Dependency sources** are parsed like any source (located errors; a bounded `Run` per dependency per reparse, released with the `CompileResult`; LSan-clean, see `corpus-run.md` T027/T042). A dependency's compile failure is one setup diagnostic (`project_dep_compile_failed` golden).
- [X] **The T034 robustness results** are below.

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
