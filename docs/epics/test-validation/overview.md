# Epic: test-validation — Trust the Tests

**Archetype**: evolve (hardening existing test infrastructure) + a bounded discover slice (fuzzing)

**Status**: planning

**Owner**: Ben Payne

**Created**: 2026-07-14 · **Updated**: 2026-07-14

**Source documents**: production-readiness audit (conversation record, 2026-07-13)
and its four-phase roadmap; this epic is **Phase 2 — Trust the tests**. Phase 1
(the compiler-enforcement foundation) shipped as the `blang-ast` epic; Phase 3
(safety enforcement) was pulled forward into it. See `design.md` §Context.

## Why

BLang's test suites are solid *smoke tests* but validate almost nothing about
**correctness of results**. The 2026-07-13 audit found, and this session
re-confirmed against the current tree:

- **Codegen tests check exit code, not output.** `test_codegen.sh` captures a
  binary's stdout and then discards it — a program that prints the wrong answer
  passes as long as it exits 0. There are no golden-output files anywhere.
- **`bcc test` is parse-only.** It shells `qcc` and checks the exit code
  (`bcc.cpp:329` → `parseFile`); it never compiles or runs `test{}` blocks. The
  emitted `__blang_run_tests()` has no pass/fail counting and aborts the whole
  process on the first failed assert. The built-in test feature is vestigial.
- **The memory-safety tooling exists but is switched off in CI.**
  `test_codegen.sh --leak-check` (ASan/LSan) is implemented but CI never runs
  it; there is no sanitizer build of the compiler; the ARC runtime's memory
  correctness is unverified in CI.
- **No C-runtime unit tests** for `blang_array/string/buffer/net/fs/json`, and
  **no fuzzing** of the hand-written lexer/parser.

Phase 1 made the compiler *reject wrong programs*; this epic makes the test
harness *catch wrong behavior*, so future work (and the hires' own PRs) is
gated by evidence rather than "it compiled and exited 0."

## Done condition (epic level)

All of the following succeed on a clean checkout of the epic's final state,
run from the repo root. Every clause is a runnable teeth-proof, not just an
"exit 0"; exact commands in `evaluation.md` §Epic-level acceptance.

1. **Goldens with teeth.** At least 55 codegen tests have a committed stdout
   golden (`ls test_files/codegen_*.expected.out | wc -l` >= 55), and the
   quarantine file `test_files/codegen_quarantine.txt` diff-equals the approved
   list in `design.md` (so the quarantine cannot be widened to nullify goldens);
   `./test_codegen.sh` exits 0 comparing captured stdout to each golden; and the
   harness self-check proves teeth — `./test_codegen.sh --selfcheck` corrupts a
   real golden internally, asserts the suite goes red, prints
   `SELFCHECK: OK`, and exits non-zero, while a scripted wrong-output patch to
   one test flips `./test_codegen.sh` from green to red.
2. **Real `bcc test`.** `bcc test test_files/testblock/all_pass.b` exits 0 and
   its output matches `[0-9]+ passed`; `bcc test test_files/testblock/has_failure.b`
   exits non-zero and its output matches both `FAIL` and the location regex
   `[^:]+\.b:[0-9]+:`; and `bcc test --filter add_two test_files/testblock/all_pass.b`
   runs a strict, non-empty subset (its `passed` count is > 0 and < the
   unfiltered count).
3. **Sanitizers in CI, with teeth.** `cmake -S . -B build-asan
   -DBLANG_SANITIZE=address,undefined && cmake --build build-asan` builds clean
   and `BUILD_DIR=build-asan ./run_tests.sh` exits 0; `./test_codegen.sh
   --leak-check` **exits non-zero when a leak is present** (proven by an
   injected-leak fixture) and exits 0 reporting `Leaks: 0` on the deterministic
   suite; both are CI jobs (see #6).
4. **Runtime unit tests.** `test "$(ctest --test-dir build -N | grep -c 'Test #')"
   -ge 30` holds, `ctest --test-dir build` exits 0, `ctest --test-dir build-asan`
   exits 0 (tests run under ASan), and at least one runtime test is proven to
   fail when its library's bounds check is removed (demonstrated in the U5 PR).
5. **Bounded fuzzing with teeth.** `fuzz_parse` builds; `ls
   test_files/fuzz/corpus | wc -l` >= 20 (seeded from `pass`/`fail`); replaying
   the corpus `build/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0; the
   target provably reaches the parser (a committed "poison" input crashes a
   deliberately-broken parser, shown in the U6 PR); and CI runs a fixed-budget
   campaign (`-max_total_time=60`).
6. **CI enforces it all.** `.github/workflows/ci.yml` defines jobs that
   *execute* the golden compare, `bcc test` fixtures, the ASan build +
   leak-check legs, `ctest` runtime units, the fuzz campaign, and
   `make -C demos run`; each runs green on the epic's final commit (verified via
   the CI run, not by grepping ci.yml text). Configuring these as branch-
   protection "required" checks is a documented manual manager step.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | Codegen E2E tests verify program **stdout**, not just exit code: `test_codegen.sh` compares captured stdout to a committed per-test golden and fails on mismatch; a `--selfcheck` mode proves the comparison has teeth. | P1 | Done-condition #1 (golden compare + selfcheck) |
| REQ-002 | Every deterministic codegen test has a committed stdout golden; the non-deterministic set is a **fixed, approved** quarantine list (not hire-widenable), documented, and those tests still run for exit code. | P1 | Done-condition #1 (≥ 55 goldens; quarantine diff-equals the approved list) |
| REQ-003 | `bcc test` compiles and runs `test{}` blocks (not parse-only): per-test PASS/FAIL, a total count, a `--filter` by name, `<file>:<line>` on assertion failure, and exit non-zero iff any test fails. | P1 | Done-condition #2 (fixtures: exit codes + `passed`-count/`FAIL`/`file:line` greps + strict-subset filter) |
| REQ-004 | CI runs an AddressSanitizer/LeakSanitizer leg that **fails on any leak**, and a sanitizer (ASan+UBSan) build of the compiler that passes the parse suite clean. `test_codegen.sh --leak-check` is fixed so leaks are fatal to its exit code. | P1 | Done-condition #3 (injected-leak proof + sanitizer build green in CI) |
| REQ-005 | The runtime C libraries have unit tests runnable via `ctest`, covering core operations and bounds/error paths, green under ASan, with at least one test proven to fail when a bounds check is removed. | P1 | Done-condition #4 (asserted count ≥ 30; ctest on `build` and `build-asan`; removed-check proof) |
| REQ-006 | A libFuzzer target exercises the lexer+parser (provably reaches parser code); CI runs a bounded campaign (`-max_total_time=60`); a seed corpus (≥ 20) plus crash-derived regression inputs are committed and replay crash-free. | P2 | Done-condition #5 (corpus ≥ 20 + replay exit 0 + poison-input proof) |
| REQ-007 | CI enforces all new checks as required gates and additionally runs the demos (`make -C demos run`); a regression in any check fails CI. | P1 | Done-condition #6 (ci.yml job presence + green) |

## Non-goals

- **Code-coverage percentage targets.** This epic adds specific, checkable
  tests and gates, not a coverage-% threshold (a known rabbit hole). Coverage
  measurement may be a later epic.
- **Open-ended bug hunting.** Fuzzing is bounded to: build the target, run a
  fixed-budget campaign, fix crashes it surfaces, commit regression cases. No
  "find and fix all latent bugs" mandate.
- **New language features or diagnostics work** — multi-error recovery,
  warnings, `--json` diagnostics (Phase 4) are out of scope here.
- **Performance benchmarking / perf regression gating** — separate concern.
- **Windows CI** — the runtime is POSIX-only; sanitizers/fuzzing target Linux.

## Companion documents

| File | Purpose |
|------|---------|
| `workplan.md` | 7 units, dependency map, per-unit done conditions |
| `design.md` | design spec: current harness seams, target architecture, decisions |
| `evaluation.md` | harness commands, audit rubrics, regression protection |
| `manifest.yaml` | machine-readable run definition |

## Constraints & context for the manager

- **Do not weaken existing green suites.** `./run_tests.sh` and
  `./test_codegen.sh` (63/63) are green today; every unit boundary must keep
  them green. (Baseline counts are **measured at launch** and recorded in the
  status log — do not hardcode; the pre-blang-ast "162" figure is stale, the
  current LLVM count is 186.) Adding golden comparison must not turn a
  currently-passing test red except where it genuinely prints wrong output
  (that is a real bug to fix, not to mask).
- **Non-determinism is real.** `codegen_http*`, `codegen_tcp_echo`,
  `codegen_selector`, `codegen_spawn_threaded`, and some spawn tests have
  timing/order-dependent output and bind real sockets. Quarantine them from
  golden comparison explicitly; do not paper over flakiness with loose matches.
- **The `--leak-check`/`--valgrind` machinery already exists** in
  `test_codegen.sh` — wire it into CI, don't reinvent it.
- **`bcc test` work touches `CGRuntime.cpp` (`genTestRunner`) and `bcc.cpp`.**
  Keep the `test{}` codegen ABI changes isolated; don't perturb normal builds.
- Constitution applies: `.specify/memory/constitution.md` (quality gates +
  two-hire spec-audit/code-audit/merge lifecycle; reject-don't-coerce; verified
  memory safety). Per-unit branches `epic/test-validation/uN-<slug>`, squash
  merge, no direct commits to `master`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| — | (none currently) | | | |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-07-14 | — | epic created | Phase 2 of the production roadmap; scope confirmed: all five buckets, bounded fuzzing, all-63 goldens |
| 2026-07-14 | — | readiness review round 1 | 17 findings (fresh-context + self audit); core theme: vacuous/teeth-free gates a testing epic must not ship. Fixes applied: --leak-check confirmed exits 0 with leaks today (F1) → U4 now fixes it + injected-leak proof; quarantine floor bounded to a pinned 6-test list + >=55 goldens (F13); U1/U5/U6 done-conditions given runnable teeth-proofs; U3 acceptance greps output not just exit codes; 'smoke' filter → named add_two fixture; stale 162 baseline → measured-at-launch (186 observed); CI gate = jobs run green, not grep. |
