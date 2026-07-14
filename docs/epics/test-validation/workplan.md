# Workplan: test-validation

**Epic**: [overview.md](overview.md) · **Archetype**: evolve (+ bounded discover)

Each unit is one implementing hire's assignment and moves through the
constitution's five-phase lifecycle (create spec → spec audit → implement →
code audit → merge) on a branch `epic/test-validation/uN-<slug>`, landing as one
squash-merged PR. Every unit boundary leaves `./run_tests.sh` and
`./test_codegen.sh` green.

## Unit map

```text
U1 (golden harness) ─▶ U2 (golden migration, all 63) ─┐
U3 (bcc test runner) ─────────────────────────────────┤
U4 (sanitizer builds + leak CI) ──────────────────────┼─▶ U7 (CI integration + demos + close-out)
U5 (runtime C unit tests) ────────────────────────────┤
U6 (parser fuzzing, bounded) ─────────────────────────┘
```

U3, U4, U5, U6 are independent of each other and of the U1→U2 chain; they may
run in parallel after the launch baseline. U2 depends only on U1. U7 is the
capstone and depends on U1–U6 (it wires each new check into CI as a required
gate). The manager serializes merges and rebases in-flight branches after each.

## Units

### U1 — Golden-output harness

- **Covers**: REQ-001
- **Preconditions**: none (baseline suites green)
- **Work**: extend `test_codegen.sh` so each test's captured stdout is compared
  against a committed golden `test_files/<name>.expected.out`; a mismatch fails
  the test with a diff. Add `--update-goldens` (regenerate goldens for
  deterministic tests) and `--selfcheck` (corrupt a golden in a temp copy and
  assert the suite detects it — proves the comparator has teeth). Add a
  quarantine mechanism (`test_files/codegen_quarantine.txt`) so listed
  non-deterministic tests skip golden comparison but still run for exit code.
  The **only** permitted output normalization is stripping a single trailing
  newline — no other transform, no loose/substring matching. Commit at least
  one sample golden so `--selfcheck` has something real to corrupt at U1 merge.
- **Done condition**: `--selfcheck` is a real paired proof, not `exit 1` — it
  corrupts an actual committed golden in a temp copy, runs the suite, asserts
  the suite went red, prints `SELFCHECK: OK`, and exits non-zero; separately,
  `./test_codegen.sh` exits 0 with the sample golden matching, and a scripted
  one-line wrong-output patch to a test flips the suite red. All suites green.
- **Audit**: per constitution.
- **Budget hint**: small–medium (one script + a few fixtures).
- **Team hint**: single session; unblocks U2.
- **Speckit**: `NNN-golden-harness`

### U2 — Golden migration (all deterministic codegen tests)

- **Covers**: REQ-002
- **Depends on**: U1
- **Work**: generate and commit stdout goldens for every deterministic
  `codegen_*.b`. For each, verify the golden reflects *correct* output (not just
  whatever the binary currently prints — check against the test's intent; a
  wrong current output is a bug to fix, logged). The quarantine set is **fixed**:
  `codegen_quarantine.txt` must equal the approved list in `design.md`
  §"Quarantine list" (the ~5 network/threading tests); it cannot be widened to
  dodge goldens. Additionally hand-author a few **known-answer** goldens whose
  expected value is stated in the test's own comment, so at least some goldens
  are independently verifiable rather than self-snapshotted.
- **Done condition**: `ls test_files/codegen_*.expected.out | wc -l` ≥ **55**;
  `diff <(sort test_files/codegen_quarantine.txt) <approved-list>` is empty;
  `./test_codegen.sh` exits 0; all suites green.
- **Audit**: per constitution; reviewer spot-checks ≥ 5 goldens for correctness
  (not just presence) and records the note as a named PR artifact the manager
  can confirm exists.
- **Budget hint**: medium (migration sweep; may span stacked PRs if large).
- **Team hint**: single session; after U1.
- **Speckit**: `NNN-golden-migration`

### U3 — Real `bcc test` runner

- **Covers**: REQ-003
- **Preconditions**: none
- **Work**: make `bcc test` compile and run `test{}` blocks instead of parsing
  (`bcc.cpp` + `CGRuntime.cpp` `genTestRunner`): per-test PASS/FAIL, a total
  count, `--filter <substring>`, `<file>:<line>` on assertion failure (uses the
  SourceLocation the AST now carries from blang-ast U1), and process exit code
  non-zero iff any test failed. Tests must be isolated so one failure does not
  abort the rest of the run.
- **Done condition** (machine-asserted on output, not just exit codes):
  `bcc test test_files/testblock/all_pass.b` exits 0 and output matches
  `[0-9]+ passed`; `bcc test test_files/testblock/has_failure.b` exits non-zero
  and output matches both `FAIL` and `[^:]+\.b:[0-9]+:`;
  `bcc test --filter add_two test_files/testblock/all_pass.b` runs a strict
  non-empty subset (`passed` count > 0 and < the unfiltered count). Fixtures
  (`all_pass.b` with a test named `add_two`, `has_failure.b`) committed. All
  suites green.
- **Audit**: per constitution.
- **Budget hint**: medium (codegen + driver).
- **Team hint**: parallelizable with U4/U5/U6.
- **Speckit**: `NNN-bcc-test-runner`

### U4 — Sanitizer builds + leak CI

- **Covers**: REQ-004
- **Preconditions**: none
- **Work**: **first fix `test_codegen.sh --leak-check` so leaks are fatal** —
  today a leak (LSan exit 23) is counted as PASS and never affects the script's
  exit code (`LEAK_TOTAL` is cosmetic), so the current gate has no teeth. Make
  `LEAK_TOTAL > 0` force a non-zero exit. Then add a `BLANG_SANITIZE` CMake
  option (address, undefined) building the compiler+runtime with sanitizers; a
  CI job for the sanitizer compiler build running `./run_tests.sh`; and a CI job
  running `./test_codegen.sh --leak-check`. Fix any real leaks/UB the legs
  surface, or quarantine with a tracked justification if out of scope.
- **Done condition**: `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined
  && cmake --build build-asan` succeeds; `BUILD_DIR=build-asan ./run_tests.sh`
  exits 0; `./test_codegen.sh --leak-check` **exits non-zero on a deliberately
  injected leak** (teeth proof, shown in PR) and exits 0 reporting `Leaks: 0`
  on the deterministic suite; both wired as CI jobs. All suites green.
- **Audit**: per constitution; **memory-safety evidence required** (leak-check
  output attached).
- **Budget hint**: medium (may surface real leaks to fix).
- **Team hint**: parallelizable.
- **Speckit**: `NNN-sanitizer-ci`

### U5 — Runtime C unit tests

- **Covers**: REQ-005
- **Preconditions**: none
- **Work**: add a lightweight C unit-test harness (a tiny assert-based runner,
  no external framework) with per-library test files for
  `blang_array/string/buffer/net/fs/json`, covering core operations and the
  error/bounds paths (out-of-bounds, null, OOM-ish, empty). Register them with
  CTest (`add_test` + `enable_testing`) in both the normal `build` and the
  `build-asan` config. Include at least one test that fails when a library's
  bounds check is removed (teeth).
- **Done condition**: `test "$(ctest --test-dir build -N | grep -c 'Test #')"
  -ge 30` holds (count asserted, not commented); `ctest --test-dir build` exits
  0; `ctest --test-dir build-asan` exits 0 (tests actually run under ASan); the
  removed-bounds-check test is shown failing in the PR; all suites green.
- **Audit**: per constitution.
- **Budget hint**: medium.
- **Team hint**: parallelizable.
- **Speckit**: `NNN-runtime-unit-tests`

### U6 — Parser fuzzing (bounded)

- **Covers**: REQ-006
- **Preconditions**: none (best sequenced late so it fuzzes the settled parser,
  but no hard dependency)
- **Work**: add a libFuzzer target `fuzz_parse` driving the lexer+parser on
  arbitrary bytes (guarded build behind a CMake option, clang required). The
  harness **must actually invoke the parser** on the input — provable by a
  committed "poison" input that crashes a deliberately-broken parser (shown in
  the PR), so a stub target can't pass. Commit a seed corpus (≥ 20 inputs)
  derived from `test_files/pass` + `fail`; run a fixed-budget campaign
  (`-max_total_time=60` in CI); triage crashes, fix them (or file + quarantine
  with justification), and commit each crashing input as a regression case under
  `test_files/fuzz/corpus/`. Bounded per the Non-goal — no open-ended hunt.
- **Done condition**: `fuzz_parse` builds; `ls test_files/fuzz/corpus | wc -l`
  ≥ 20; `build/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0 (corpus
  replays crash-free); the poison-input parser-reachability proof is in the PR;
  a CI leg runs `-max_total_time=60`; any crash found is fixed (committed
  regression input) or logged as a quarantined known-issue.
- **Audit**: per constitution; reviewer confirms crashes were fixed, not just
  removed from the corpus.
- **Budget hint**: medium; **bounded** — cap the campaign per Non-goals.
- **Team hint**: parallelizable; prefer late.
- **Speckit**: `NNN-parser-fuzzing`

### U7 — CI integration, demos gate, and close-out

- **Covers**: REQ-007
- **Depends on**: U1, U2, U3, U4, U5, U6
- **Work**: wire every new check into `.github/workflows/ci.yml` as a job that
  *executes* it — golden compare (already in `test_codegen.sh`), `bcc test` on
  the fixtures, the sanitizer build + leak legs (from U4), `ctest` runtime units
  (U5), the fuzz campaign (U6) — and add `make -C demos run` as a CI leg (note:
  `run` covers the 10 runnable demos; the 4 network demos are build-only).
  Document, as a manual manager step, configuring these as branch-protection
  "required" checks (that setting lives in GitHub, not in the repo). Update
  `CLAUDE.md` (test counts, testing section, known-issues this epic closes) and
  testing docs as needed.
- **Done condition**: the epic-level done condition in `overview.md` holds,
  checked by the "Epic-level acceptance" block in `evaluation.md`; each new CI
  job runs **green on the epic's final commit** (verified via the CI run, not by
  grepping ci.yml text); `make -C demos run` is a CI leg.
- **Audit**: per constitution; **functional review** = the full epic done
  condition.
- **Budget hint**: medium.
- **Team hint**: capstone, after all others.
- **Speckit**: `NNN-ci-integration`

## Sequencing notes for the manager

- U1 must merge before U2 (the migration needs the harness).
- U3/U4/U5/U6 are mutually independent; staff up to `max_open_prs` of them in
  parallel after baseline. Rebase in-flight branches after each merge and
  re-run gates.
- U6 (fuzzing) may surface parser bugs. If a crash requires a **language/parser
  semantics** decision to fix (not a clear crash bug), raise an Open Question
  rather than guessing — one thread blocks, the rest continues.
- U7 is strictly last; it turns the new checks into required gates, so it must
  see all prior units merged or it will gate on absent artifacts.
- If U2 or U4 surfaces a genuine wrong-output or memory bug in existing code,
  fixing it is in scope (it is the point of the epic); a fix that is large or
  risky should be raised as an Open Question before landing.
