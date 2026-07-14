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
run from the repo root (concrete commands and thresholds in `evaluation.md`):

1. **Goldens with teeth.** Every codegen test not on the documented
   non-deterministic quarantine list has a committed stdout golden
   (`ls test_files/codegen_*.expected.out | wc -l` ≥ 63 − |quarantine|,
   quarantine listed in `test_files/codegen_quarantine.txt`); `./test_codegen.sh`
   exits 0 comparing captured stdout to each golden; and the harness
   self-check proves teeth — corrupting any one golden makes the suite exit
   non-zero (`./test_codegen.sh --selfcheck` exits non-zero).
2. **Real `bcc test`.** On the committed fixtures,
   `bcc test test_files/testblock/all_pass.b` exits 0 and prints a pass count;
   `bcc test test_files/testblock/has_failure.b` exits non-zero, prints per-test
   PASS/FAIL, a total, and a `<file>.b:<line>:` location for the failed assert;
   `bcc test --filter <name>` runs only matching tests.
3. **Sanitizers in CI.** A sanitizer build of the compiler
   (`cmake -B build-asan -DBLANG_SANITIZE=address,undefined`) builds clean and
   `BUILD_DIR=build-asan ./run_tests.sh` exits 0; `./test_codegen.sh --leak-check`
   exits 0 with 0 leaks over the deterministic suite; both are required CI jobs.
4. **Runtime unit tests.** `ctest --test-dir build` exits 0 with ≥ 30 runtime
   unit tests covering `blang_array/string/buffer/net/fs/json` including
   bounds/error paths, and the runtime-test target builds and runs under ASan.
5. **Bounded fuzzing.** The libFuzzer target `fuzz_parse` builds; replaying the
   committed corpus (`build/fuzz_parse test_files/fuzz/corpus/ -runs=0`) exits 0
   (no crash); CI runs a fixed-budget campaign leg.
6. **CI enforces it all.** `.github/workflows/ci.yml` defines required jobs for
   goldens, `bcc test`, the ASan/leak legs, `ctest` runtime units, the fuzz
   smoke/campaign, and `make -C demos run`; a regression in any one fails CI.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | Codegen E2E tests verify program **stdout**, not just exit code: `test_codegen.sh` compares captured stdout to a committed per-test golden and fails on mismatch; a `--selfcheck` mode proves the comparison has teeth. | P1 | Done-condition #1 (golden compare + selfcheck) |
| REQ-002 | Every codegen test that is deterministic has a committed stdout golden; non-deterministic tests (network/threading/timing) are explicitly listed in a quarantine file and documented, not silently exit-code-only. | P1 | Done-condition #1 (count ≥ 63 − quarantine; quarantine file exists) |
| REQ-003 | `bcc test` compiles and runs `test{}` blocks (not parse-only): per-test PASS/FAIL, a total count, a `--filter` by name, `<file>:<line>` on assertion failure, and exit non-zero iff any test fails. | P1 | Done-condition #2 (pass/fail fixtures + filter) |
| REQ-004 | CI runs an AddressSanitizer/LeakSanitizer leg over the codegen suite (`--leak-check`, 0 leaks) and a sanitizer (ASan+UBSan) build of the compiler that passes the parse suite clean. | P1 | Done-condition #3 (sanitizer build + leak leg green in CI) |
| REQ-005 | The runtime C libraries have unit tests runnable via one command (`ctest`), covering core operations and bounds/error paths, green under ASan. | P1 | Done-condition #4 (`ctest` ≥ 30 tests, ASan-clean) |
| REQ-006 | A libFuzzer target exercises the lexer+parser; CI runs a bounded campaign; a seed corpus plus any crash-derived regression inputs are committed and replay crash-free. | P2 | Done-condition #5 (corpus replay exit 0; CI campaign leg) |
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

- **Do not weaken existing green suites.** `./run_tests.sh` (162 LLVM / 154
  parse-only) and `./test_codegen.sh` (63/63) are green today; every unit
  boundary must keep them green. Adding golden comparison must not turn a
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
