# Spec: CI integration, demos gate, and close-out (U7 / REQ-007)

**Epic**: test-validation · **Unit**: U7 · **Branch**: `epic/test-validation/u7-ci-integration`
**Covers**: REQ-007 · **Depends on**: U1–U6 + U8 (all merged to master)
**Design decisions honored**: D5 (wire the existing leak-check machinery, do not
reinvent it), design.md §"Architecture (target)" (each new check becomes a CI
required job), §Invariants (existing `parse-only`/`with-llvm` legs keep working;
new legs are additive; default build artifacts unchanged).

## Problem

Every correctness check this epic built (golden compare, real `bcc test`,
sanitizer build + fatal leak-check, `ctest` runtime units, bounded parser
fuzzing, and the `make -C demos run` target) exists and is green **locally**, but
CI does not run most of them. `.github/workflows/ci.yml` today has only two
legs: `parse-only` and `with-llvm`, both running `./run_tests.sh`, with
`with-llvm` additionally running `./test_codegen.sh`. There is **no** sanitizer
leg, **no** leak-check leg, **no** `ctest` runtime-unit leg, **no** fuzz leg, and
demos are never run. So a regression in any of the new checks would not fail CI —
the gates have no teeth at the CI level, which defeats the epic's purpose ("trust
the tests").

This unit is the capstone: wire **every** new check into `ci.yml` as a job that
*actually executes* it (never a grep of `ci.yml` text), each green on the epic's
final commit; document configuring them as GitHub branch-protection "required"
checks as a **manual manager step** (that setting lives in GitHub, not the repo);
and close out the docs (`CLAUDE.md` test counts / testing section / known-issues
the epic closed).

## Scope

**In scope**
- `.github/workflows/ci.yml`: preserve the existing `parse-suite` matrix
  (`parse-only` + `with-llvm`) and add jobs that execute, on every push/PR:
  1. **golden-codegen** — build with LLVM, run `./test_codegen.sh` (stdout
     goldens compared), and a `--selfcheck` teeth step that MUST exit non-zero
     and print `SELFCHECK: OK`.
  2. **bcc-test** — the three `bcc test` fixture assertions (all_pass, has_failure,
     `--filter add_two` strict subset).
  3. **sanitizers** — build `build-asan` (`-DBLANG_SANITIZE=address,undefined`),
     run `BUILD_DIR=build-asan ./run_tests.sh`, and the two leak-check legs
     (injected-leak fixture fatal; clean suite `Leaks: 0`).
  4. **runtime-units** — `ctest` count assertion (`≥ 30`) plus `ctest --test-dir
     build` and `ctest --test-dir build-asan` both green.
  5. **fuzz** — build `fuzz_parse` in a dedicated clang `build-fuzz` dir
     (`-DBLANG_ENABLE_LLVM=OFF -DBLANG_FUZZ=ON`, clang), assert corpus `≥ 20`,
     replay `-runs=0` crash-free, and run a bounded `-max_total_time=60` campaign.
  6. **demos** — `make -C demos run` (the 10 runnable demos).
- `docs/ci.md`: a new doc that lists the CI jobs and documents, as a **manual
  manager step**, configuring them as GitHub branch-protection "required status
  checks" (with the exact job/check names), noting this setting is not in the
  repo.
- `CLAUDE.md`: update the Testing section (current test counts, the CI legs now
  present) and remove from "Known Issues and Limitations" the holes this epic
  closed (exit-code-only codegen tests; parse-only `bcc test`; no sanitizer CI;
  no runtime unit tests).

**Out of scope (non-goals)**
- Changing the behavior/semantics of any underlying script or check
  (`test_codegen.sh`, `run_tests.sh`, `bcc test`, `ctest` targets, `fuzz_parse`,
  the sanitizer/leak machinery) — those were built and audited in U1–U6/U8; U7
  only *invokes* them. Minimal CI-plumbing-only touch is allowed if a job cannot
  otherwise run, but no test/gate logic changes.
- Coverage-percentage targets, new language features/diagnostics, open-ended bug
  hunting, Windows CI, and performance/perf-regression gating (epic Non-goals).
- Actually flipping the GitHub branch-protection toggle (a repo-settings action
  outside version control) — U7 only *documents* it as a manual manager step.

## Functional requirements

- **FR-001** `ci.yml` preserves the existing `parse-suite` legs: a matrix over
  `parse-only` (`-DBLANG_ENABLE_LLVM=OFF`) and `with-llvm`
  (`-DLLVM_DIR=…`), each building and running `./run_tests.sh`. These continue to
  pass unchanged.
- **FR-002** A **golden-codegen** job builds with LLVM and runs
  `./test_codegen.sh` (which compares captured stdout to committed goldens and
  fails on mismatch), then runs `./test_codegen.sh --selfcheck` as a teeth step:
  the step MUST treat a zero exit as a failure and MUST require the output to
  contain `SELFCHECK: OK` (proving the comparator has teeth, not a bare `exit 1`).
- **FR-003** A **bcc-test** job builds with LLVM and asserts, on the committed
  fixtures: `bcc test test_files/testblock/all_pass.b` exits `0` and matches
  `[0-9]+ passed`; `bcc test test_files/testblock/has_failure.b` exits non-zero
  and its output matches **both** `FAIL` and `[^:]+\.b:[0-9]+:`; and
  `--filter add_two` yields a `passed` count that is `> 0` and `<` the unfiltered
  count.
- **FR-004** A **sanitizers** job configures and builds `build-asan`
  (`cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined` + build),
  runs `BUILD_DIR=build-asan ./run_tests.sh` (exit 0), and runs two leak-check
  legs against a normal LLVM `build`: `./test_codegen.sh --leak-check
  test_files/testblock/leak_probe.b` MUST be fatal (job fails if it exits 0), and
  `./test_codegen.sh --leak-check` on the clean suite MUST report a line matching
  `Leaks:[[:space:]]*0`.
- **FR-005** A **runtime-units** job builds both `build` (LLVM) and `build-asan`,
  asserts `ctest --test-dir build -N | grep -c 'Test #'` is `≥ 30`, and runs
  `ctest --test-dir build` and `ctest --test-dir build-asan` (both exit 0; ASan
  config proves the units run under the sanitizer).
- **FR-006** A **fuzz** job installs clang, configures `build-fuzz`
  (`-DBLANG_ENABLE_LLVM=OFF -DBLANG_FUZZ=ON -DCMAKE_C_COMPILER=clang
  -DCMAKE_CXX_COMPILER=clang++`), builds the `fuzz_parse` target, asserts
  `ls test_files/fuzz/corpus | wc -l` is `≥ 20`, replays
  `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` crash-free, and runs a
  bounded `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -max_total_time=60`
  campaign (job has a wall-clock timeout so the campaign cannot hang CI).
- **FR-007** A **demos** job builds with LLVM and runs `make -C demos run`
  (exit 0 across the 10 runnable demos; the 4 network demos remain build-only per
  the demos Makefile).
- **FR-008** Each of FR-002…FR-007 is a **separate GitHub Actions job** so a
  regression in any single check fails CI independently, and every job *executes*
  its check (no job merely inspects `ci.yml` text).
- **FR-009** `docs/ci.md` exists and documents, as a manual manager step,
  configuring the CI jobs as branch-protection "required status checks",
  enumerating the exact job names, and stating explicitly that this GitHub
  setting is not stored in the repo.
- **FR-010** `CLAUDE.md` is updated: the Testing section reflects the current
  measured test counts and the new CI legs, and the "Known Issues and
  Limitations" section no longer lists the four holes this epic closed
  (exit-code-only codegen tests; parse-only `bcc test`; no sanitizer CI; no
  runtime unit tests).
- **FR-011** The change does not alter default (non-sanitizer, non-fuzz) build
  artifacts or the semantics of any existing green suite; `./run_tests.sh` (both
  modes) and `./test_codegen.sh` stay green at the unit boundary.

## Success criteria (machine-checkable)

These instantiate `evaluation.md` §"Epic-level acceptance". Run from the repo
root.

- **SC-001** (goldens with teeth)
  `test "$(ls test_files/codegen_*.expected.out | wc -l)" -ge 55`; the
  symmetric-strip quarantine diff
  `diff <(sort -u test_files/codegen_quarantine.txt | grep -vE '^\s*#|^\s*$') <(sort -u docs/epics/test-validation/approved_quarantine.txt | grep -vE '^\s*#|^\s*$')`
  is empty; `./test_codegen.sh` exits 0; `./test_codegen.sh --selfcheck` exits
  non-zero and its output contains `SELFCHECK: OK`.
- **SC-002** (real `bcc test`)
  `bcc test test_files/testblock/all_pass.b` matches `[0-9]+ passed`;
  `! bcc test test_files/testblock/has_failure.b` (exits non-zero); its output
  matches `FAIL` and `[^:]+\.b:[0-9]+:`; and `filt=$(--filter add_two … passed)`,
  `full=$(all_pass … passed)` satisfy `filt > 0` and `filt < full`.
- **SC-003** (sanitizers with teeth)
  `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined && cmake --build
  build-asan` succeeds; `BUILD_DIR=build-asan ./run_tests.sh` exits 0;
  `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b` exits
  non-zero; `./test_codegen.sh --leak-check` output matches `Leaks:[[:space:]]*0`.
- **SC-004** (runtime units)
  `test "$(ctest --test-dir build -N | grep -c 'Test #')" -ge 30`;
  `ctest --test-dir build` exits 0; `ctest --test-dir build-asan` exits 0.
- **SC-005** (bounded fuzzing)
  `test "$(ls test_files/fuzz/corpus | wc -l)" -ge 20`;
  `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0.
- **SC-006** (CI executes each check green on the final commit — the decisive
  teeth-proof, not a grep of ci.yml)
  after the branch merges to master,
  `gh run list --branch master --limit 1 --json conclusion --jq '.[0].conclusion'`
  is `success`; and the CI run for that commit shows the jobs `golden-codegen`,
  `bcc-test`, `sanitizers`, `runtime-units`, `fuzz`, `demos`, and the
  `parse-suite` matrix all having run and passed.
- **SC-007** (close-out docs)
  `docs/ci.md` exists and names the branch-protection manual step and the exact
  required-check job names; `CLAUDE.md` "Known Issues" no longer lists
  exit-code-only codegen tests, parse-only `bcc test`, absent sanitizer CI, or
  absent runtime unit tests.
- **SC-008** (non-regression) `./run_tests.sh` (LLVM) green,
  `BUILD_DIR=build-parse ./run_tests.sh` green, `./test_codegen.sh` green,
  `BUILD_DIR=build-asan ./run_tests.sh` green, `ctest --test-dir build` /
  `--test-dir build-asan` green — at the unit boundary.

## Design notes (contracts the implementation must honor)

- **Wire, don't reinvent (D5).** Each job invokes an already-built, already-
  audited script/target with its established flags. U7 adds no new test logic and
  changes no gate semantics; if a CI-plumbing-only tweak is unavoidable to make a
  job runnable, it must not alter what the check asserts.
- **Additive, isolated jobs.** The existing `parse-only`/`with-llvm` legs are
  preserved verbatim in behavior. Every new check is its own job so failures are
  independently attributable and any one regression reddens CI (FR-008).
- **Opt-in build dirs mirror the units that introduced them.** `build-asan`
  (`BLANG_SANITIZE`, U3) and `build-fuzz` (`BLANG_FUZZ`, clang, U5) are configured
  exactly as their evaluation.md harness rows specify; the default gcc `build/`
  is unchanged, so default artifacts are untouched (invariant).
- **Teeth at the CI layer.** The golden `--selfcheck` step and the injected-leak
  `leak_probe.b` step must treat a *passing* underlying command as a job
  **failure** (they prove the gate can go red). The fuzz job carries a wall-clock
  `timeout-minutes` so the bounded campaign cannot hang the pipeline.
- **The decisive done-condition is the CI run, not the file.** SC-006 verifies
  green via `gh run` on the merged commit — grepping `ci.yml` text is explicitly
  insufficient (overview.md done-condition #6, evaluation.md clause 6).
- **Branch-protection is manual.** Marking these jobs "required" is a GitHub
  repo-settings action outside version control; U7 documents it in `docs/ci.md`
  for the manager to perform (evaluation.md; workplan.md §U7).

## Tasks

1. `.github/workflows/ci.yml`: keep the `parse-suite` matrix; add jobs
   `golden-codegen`, `bcc-test`, `sanitizers`, `runtime-units`, `fuzz`, `demos`,
   each installing only the deps it needs and each *executing* its check with the
   teeth steps described in FR-002…FR-007.
2. `docs/ci.md`: enumerate the jobs and document the manual branch-protection
   "required checks" step with the exact job names and the note that the setting
   lives in GitHub.
3. `CLAUDE.md`: update the Testing section (measured counts + CI legs) and remove
   the four now-closed known-issues bullets.
4. Verify locally that every job's commands pass (run each harness row from
   evaluation.md), keeping `./run_tests.sh` (both modes) and `./test_codegen.sh`
   green.
5. Push the branch and confirm the CI run is green for the head commit (the
   reviewer/manager confirms the merged-commit `gh run` is `success` for SC-006).

## Traceability

| FR | REQ | SC |
|----|-----|----|
| FR-001 | REQ-007 | SC-008 |
| FR-002 | REQ-007 | SC-001, SC-006 |
| FR-003 | REQ-007 | SC-002, SC-006 |
| FR-004 | REQ-007 | SC-003, SC-006 |
| FR-005 | REQ-007 | SC-004, SC-006 |
| FR-006 | REQ-007 | SC-005, SC-006 |
| FR-007 | REQ-007 | SC-006 |
| FR-008 | REQ-007 | SC-006 |
| FR-009 | REQ-007 | SC-007 |
| FR-010 | REQ-007 | SC-007 |
| FR-011 | REQ-007 | SC-008 |
