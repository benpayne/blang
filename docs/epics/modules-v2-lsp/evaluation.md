# Evaluation: modules-v2-lsp

**Epic**: [overview.md](overview.md) · **Constitution**:
`.specify/memory/constitution.md` (v1.2.0)

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema suite (LLVM) | `./run_tests.sh` | fully green | every unit |
| parse/sema suite (parse-only) | `cmake -S . -B build-nollvm -DBLANG_ENABLE_LLVM=OFF && cmake --build build-nollvm && BUILD_DIR=build-nollvm ./run_tests.sh` | fully green | every unit |
| parse/sema under ASan qcc | `BUILD_DIR=build-asan ./run_tests.sh` | fully green, no ASan report | U1, U3 |
| codegen E2E + goldens | `./test_codegen.sh` | fully green, golden-checked | every unit (regression) |
| leak check | `./test_codegen.sh --leak-check` | `Leaks: 0` | U1 (driver refactor) |
| golden teeth | `./test_codegen.sh --selfcheck` and `./test_lsp.sh --selfcheck` | each prints `SELFCHECK: OK` | U1, epic completion |
| build-system fixtures | `test_build/run_build_tests.sh` | all checks pass | every unit (regression); U3 uses its bins as the project corpus |
| LSP transcripts | `./test_lsp.sh` | all transcripts match goldens, incl. this epic's new ones; the `fail/sema` re-verification leg green | every unit |
| LSP diagnostic corpus (new, U2) | `tools/lsp_diag_corpus.py --corpus stdlib` / `--corpus project` (wired into `test_lsp.sh`) | for every corpus file, blangd's published diagnostics == `qcc`'s for the same compile (empty for clean files) | U2 (stdlib), U3 (project) |
| LSP latency (new, U2) | `tools/lsp_latency.py examples/todo_app/main.b test_build/usebox/main.b` (wired into `test_lsp.sh`) | median `didChange`→`publishDiagnostics` ≤ 500 ms per file over 20 edits | U2, U3 |
| no-subprocess guard (new, U3) | test inside `test_lsp.sh`: `PATH=<shim-dir>:$PATH` where `<shim-dir>/git` writes a marker; open a project with an unfetched git dep | marker file absent; one located diagnostic published | U3 |
| unit/ctests | `ctest --test-dir build` and `ctest --test-dir build-asan` | all pass; `project_compile` (new, U1); `parser_no_leak` extended with stdlib-context (U2) and project-context (U3) reparse loops, LSan-clean | every unit |
| shared-entry call sites | `grep -n '<entry point>' qcc.cpp lsp/Compile.cpp` and `grep -n 'isUserFile\|isPreludeModule' qcc.cpp` | entry point present in both files; the routing branch is absent from `qcc.cpp` | U1 |
| real-client smoke | `./test_lsp_nvim.sh` | PASS with nvim (project fixture: zero errors + cross-module definition); yellow SKIP without | U5 |
| reach-in gate | `tools/check_no_field_reachins.sh examples test_build` | `OK` | every unit (regression) |
| examples + demos | `for t in examples/*/test_*.sh; do (cd $(dirname $t) && ./$(basename $t)); done` and `make -C demos run` | all pass | U1, epic completion |
| CI (authority) | GitHub Actions on the epic branch PRs (`gh pr checks <n> --watch`) | all jobs green | every unit's PR |

## Audit plan (instantiating the constitution)

| Audit | When | Rubric | Gates? |
|-------|------|--------|--------|
| design audit | before implementation of **U1** (driver restructuring) and **U3** (interface-equivalence mechanism D2, project locator); via `/speckit-clarify` + `/speckit-analyze` | U1: purely behavior-neutral; the entry point is in frontend sources with no LLVM; ownership follows KG-7. U3: dep interface provably identical to the `.bmod` `bcc` would emit (D2); identities/origins match `bcc` (D6); no subprocess/network (C4); bounded memory (C3); handles missing/malformed/cyclic inputs | gates |
| spec audit | after each dev unit's speckit spec | covers the unit's REQ IDs; done conditions testable; no scope creep into non-goals (completion/references/rename, dependents re-diagnosis, fetching, `.bmod` reading, module search path); U4's spec states the D3 display rule (design.md D4 or a justified amendment) | gates |
| code review | before each unit's PR merges | constitution's five dimensions (correctness; tests/diagnostics; security; maintainability/robustness; style/spec-fidelity), each an explicit pass/finding by the distinct reviewer hire; every golden diff justified | gates |
| security review | **U2** (stdlib path from `initializationOptions`/env), **U3** (untrusted `blang.toml`, dep paths, git checkout paths; no-subprocess guarantee) | Quality Gate 7 untrusted-input rubric: path handling, no execution, no crash on malformed input, no unbounded memory; findings recorded in the PR | gates |
| functional review | epic completion (U5) | each numbered done-condition bullet in overview.md demonstrated by a named, committed, CI-run test; each REQ's "Verified by" checked; owner verification on a clean rebuild in both build modes before the epic branch PR to `master` | gates |

## Regression protection (evolve)

- **Baseline**: the epic branch base (`master` with PR #157 merged). Reference
  numbers from PR #157's verification (2026-11-25): `run_tests.sh` 251/0 (LLVM),
  244/0 (parse-only), 251/0 (ASan qcc); `test_codegen.sh` 172/0, `--leak-check`
  `Leaks: 0`; `test_lsp.sh` 70/0; `ctest` 82/82 in `build` and `build-asan`;
  `run_build_tests.sh` all pass; all 6 example scripts; demos; GitHub CI 15/15
  green. Re-measure on the actual base before U1 and report any pre-existing
  red before starting. Do not absorb it.
- **Must not change**: `qcc`/`bcc` exit codes, diagnostic text, IR and `.bmod`
  output; codegen goldens; same-file LSP goldens (except U2's individually
  justified prelude-resolution diffs); the quarantine list
  (`docs/epics/test-validation/approved_quarantine.txt`).
- **Test-count floor**: the `run_tests`/`test_codegen`/`test_lsp`/`ctest`
  totals may only grow.

## Evidence requirements

- Each unit's PR body: gate-command outputs (or CI links); the reviewer hire's
  per-dimension verdicts; disposition of every finding; a justification line per
  changed golden.
- **U1**: the design-audit record under `specs/041-shared-project-compile/`; the
  `project_compile` ctest; the grep evidence for both call sites.
- **U2**: the committed corpus list and harness output (per-file pass); latency
  output; the extended `parser_no_leak` LSan run.
- **U3**: the D2 design-audit record (how interface equivalence is guaranteed);
  corpus output over `test_build/*` bins and `fail/xmodule/*`; the git-shim
  test; latency output on `usebox`; the security-review record.
- **U4**: per-case definition/hover goldens; the D3 rule as written in the spec.
- **Epic completion (U5)**: done-condition → test mapping table; `CLAUDE.md` +
  `editors/README.md` diffs; CI green on the epic branch; overview status log via
  fold-back.
