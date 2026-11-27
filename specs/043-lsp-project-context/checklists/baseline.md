# U3 baseline and post-unit record

- **Branch**: `043-lsp-project-context`.
- **Base**: `git merge-base --is-ancestor dc39603 HEAD` succeeds. The base is U2's converged tip.
- **Pre-unit HEAD**: `9bd4858`. Its source tree is identical to `dc39603`; only `specs/`, `docs/` and `.specify/` changed.
- **Pre-unit gate counts**: U2's final converge record, `specs/042-lsp-stdlib-context/checklists/converge.md`:
  - ctest 86/86 ×3;
  - `run_tests` 251/244/251;
  - `test_lsp` 78/0, both modes;
  - `test_codegen` 172/0, Leaks: 0;
  - `run_build_tests` all pass;
  - stdlib corpus 179/179;
  - reach-in, examples and demos green.
- **`bcc` reference (T002)**: the pre-unit `bcc`, `qcc` and `stdlib` are in `/tmp/u3-ref/bin`, and `test_build` is copied to `/tmp/u3-ref/tree`. `/tmp/u3-ref/capture.sh` records `bcc build -v` for the 10 binaries, plus negative legs: a malformed toml with an unpinned git dependency, the cycle a↔b, a missing path dependency, and diamond under `-v`. Outputs go to `/tmp/u3-ref/before/`, with the cache dir and bin dir normalized. These are scratch files and are not committed.

## T007: R3 neutrality (`bcc` after the `ProjectLayout` / `walkDependencies` / `scanImportsAll` / `loadFromText` moves)

- **`bcc` outputs**: all **28/28** are `cmp`-identical between the pre-unit and post-move `bcc`. This covers stdout, stderr and exit code, with the cache dir and bin dir normalized. The set is:
  - `bcc build -v` on all 10 binaries;
  - the negative legs: malformed toml plus an unpinned git dependency (`<toml>:4: parse error: expected key = value` and `error: git dependency 'greet' must pin a version…`); the cycle a↔b (`error: circular dependency detected: …/cyc_a`); and a missing path dependency (`error: no blang.toml found in …/../does_not_exist`, the N1 2a path);
  - the diamond leg under `-v`.
- **`test_build/run_build_tests.sh`**: all build-system checks pass (107 PASS lines), including the git-dependency leg, the diamond leg and the `.bmod` goldens.
- **Grep gates**:
  - `grep -n 'buildProject(' bcc.cpp` → only `1071:` (the definition) and `1402:` (the `runBuild` call);
  - `grep -nE 'building\.(insert|count|erase)' bcc.cpp` → nothing;
  - `grep -nE "^static .*(discoverSourceFiles|resolvePath|gitCacheRoot|shortHashHex|mergeDep)" bcc.cpp` → nothing.
- **`blangd_no_subprocess`**: OK.
- **ctest**: 86/86 in `build`. `project_layout` was added separately (`ProjectLayoutTest: OK`).
