# Continuous Integration

BLang's CI is defined in [`.github/workflows/ci.yml`](../.github/workflows/ci.yml).
Every job **executes** a real check (it never merely greps `ci.yml` text), so a
regression in any check reddens CI on its own. Jobs run on every push to
`master`/`main`/`claude/**`/`epic/**` and on every pull request into
`master`/`main`.

## Jobs

| Job | What it executes | Teeth |
|-----|------------------|-------|
| `parse-suite` (matrix: `parse-only`, `with-llvm`) | Builds `qcc`/`bcc` in each mode and runs `./run_tests.sh` (parse/sema suite). | A parser/sema regression fails the suite. |
| `golden-codegen` | `./test_codegen.sh` (compiled-binary stdout compared to committed `test_files/codegen_*.expected.out` goldens); asserts `>= 55` goldens and that `test_files/codegen_quarantine.txt` exactly equals `docs/epics/test-validation/approved_quarantine.txt` (anti-widening); then `./test_codegen.sh --selfcheck`. | A wrong-output regression fails the compare. `--selfcheck` corrupts a real golden internally and MUST exit non-zero printing `SELFCHECK: OK` — a zero exit fails the job (proves the comparator has teeth). Quarantine widening fails the diff. |
| `bcc-test` | `bcc test` on `test_files/testblock/all_pass.b` (exit 0, `N passed`), `has_failure.b` (exit non-zero, `FAIL` + `<file>:<line>:`), and `--filter add_two` (strict non-empty subset). | A test-runner regression (miscount, lost isolation, broken filter) fails a step. |
| `sanitizers` | Builds `build-asan` (`-DBLANG_SANITIZE=address,undefined`), runs `BUILD_DIR=build-asan ./run_tests.sh`; then two leak-check legs against the LLVM `build`: `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b` (injected leak MUST be fatal) and `./test_codegen.sh --leak-check` (clean suite MUST report `Leaks: 0`). | ASan/UBSan errors fail the parse suite. A non-fatal injected leak fails the teeth step. Any real leak fails the clean leg. |
| `runtime-units` | Builds `build` and `build-asan`; asserts `ctest --test-dir build -N` lists `>= 30` tests; runs `ctest --test-dir build` and `ctest --test-dir build-asan`. | A runtime C-library regression (bounds/error path) fails a `ctest`; the ASan config proves the units run under the sanitizer. |
| `fuzz` | Builds `fuzz_parse` in a dedicated clang `build-fuzz` dir (`-DBLANG_ENABLE_LLVM=OFF -DBLANG_FUZZ=ON`); asserts corpus `>= 20`; replays `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` (crash-free); runs a bounded `-max_total_time=60` campaign. Job has `timeout-minutes` so the campaign cannot hang CI. | A parser crash on any committed corpus/regression input fails the replay; a newly-found crash fails the campaign. |
| `demos` | `make -C demos run` — the 10 runnable demos (the 4 network demos are build-only). | A demo that stops compiling or exits non-zero fails the leg. |

## Manual manager step — branch-protection "required" status checks

Making these jobs **required** for merging into `master` is a GitHub
repository-settings action. **It is not stored in the repository** and cannot be
configured from `ci.yml`; a manager with admin rights must set it once in the
GitHub UI (or via `gh`/the REST API).

**GitHub UI:** *Settings → Branches → Branch protection rules → `master` (add or
edit rule) → enable "Require status checks to pass before merging", then select
each check below.*

Required checks to select (these are the CI job names from `ci.yml`):

- `parse-suite (parse-only)`
- `parse-suite (with-llvm)`
- `golden-codegen`
- `bcc-test`
- `sanitizers`
- `runtime-units`
- `fuzz`
- `demos`

> Matrix jobs appear per-combination (e.g. `parse-suite (with-llvm)`); select
> each combination you want to require. Check names must be selected exactly as
> GitHub reports them after at least one run of the workflow on a branch.

Once configured, a PR cannot merge to `master` until all selected checks are
green — which is the enforcement half of the `test-validation` epic ("trust the
tests"). The repo-side half (the executing jobs) lives in `ci.yml`; this manual
step turns them into hard gates.
