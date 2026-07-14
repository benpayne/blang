# Design: test-validation

**Epic**: [overview.md](overview.md)

Product/architecture level. Detailed engineering (exact golden format, CTest
wiring, fuzzer harness shape) is decided by the hires in their per-unit speckit
plans; this fixes the seams, contracts, and decisions they must honor.

## Context (plan-time recon, current tree at 2026-07-14)

The test infrastructure as it stands:

- **`run_tests.sh`** — parse/sema gate. Locates qcc via `BUILD_DIR` (env
  override, added 2026-07-13); honors an expected-error harness added by
  `blang-ast` U2: negative tests under `test_files/fail/` and `fail/sema/` may
  declare an expected diagnostic via a `<test>.b.expected` companion or an
  inline `// EXPECT-ERROR:` marker, and `fail/sema/` files must additionally
  match the canonical `^[^:]+\.b:[0-9]+:[0-9]+: error: ` regex. Exit-code +
  diagnostic-pattern only; no stdout checking (parse tests produce none).
- **`test_codegen.sh`** — E2E pipeline (qcc → llc → cc → run). Honors
  `BUILD_DIR`; accepts multiple file args (added 2026-07-13). Checks the
  compiled binary's **exit code** only; it captures `run_output` and discards
  it. Has `--leak-check` (ASan/LSan, `LSAN_OPTIONS=exitcode=23`) and
  `--valgrind` machinery that works but is unused by CI. Stdlib auto-combine by
  filename heuristic. 63 `codegen_*.b` tests, all green.
- **`.github/workflows/ci.yml`** — two legs: `parse-only`
  (`-DBLANG_ENABLE_LLVM=OFF`) and `with-llvm`. Both run `./run_tests.sh`; only
  `with-llvm` runs `./test_codegen.sh`. **No** sanitizer/leak/fuzz leg; demos
  are **not** run (`demos/Makefile` has a ready `run` target).
- **`bcc test`** (`bcc.cpp` ~260–347) — discovers `.b` files and calls
  `parseFile` (`qcc <file>` exit-code); **parse-only**, never compiles/runs
  `test{}` blocks. `genTestRunner` (`CGRuntime.cpp`) emits `__blang_run_tests()`
  that `puts` per test and relies on `assert`→`exit(1)`; no counting, aborts on
  first failure, and nothing invokes it from a normal build.
- **Runtime C libs** (`runtime/*.c`) — no unit tests; exercised only
  transitively through linked codegen binaries.
- **AST source locations** — `blang-ast` U1 added `SourceLocation {file,line,col}`
  to every node and the lexer tracks column/filename; `bcc test` failure
  locations (U3) build directly on this.

## Architecture (target)

```text
Deterministic correctness (new):
  codegen_*.b ─▶ compile+run ─▶ stdout ──compare──▶ <name>.expected.out (golden)
                                             │ mismatch ⇒ FAIL (with diff)
  codegen_quarantine.txt ─▶ listed tests skip golden compare (still run for exit code)

Behavioral test runner (new):
  bcc test *.b ─▶ compile test{} blocks ─▶ run isolated ─▶ counts + file:line ─▶ exit code

Memory safety (wired in):
  build-asan (BLANG_SANITIZE) ─▶ run_tests.sh          } CI required jobs
  test_codegen.sh --leak-check (existing) ─▶ 0 leaks   }

Runtime units (new):
  runtime/*_test.c ─▶ CTest add_test ─▶ ctest (ASan)   } CI required job

Fuzzing (new, bounded):
  fuzz_parse (libFuzzer) ─▶ corpus + campaign ─▶ crash-free replay } CI leg
```

## Interfaces & contracts

- **Golden files**: `test_files/<name>.expected.out`, exact-match against the
  binary's stdout (documented normalization only, e.g. trailing-newline). A
  missing golden for a non-quarantined test is a failure; the quarantine list is
  `test_files/codegen_quarantine.txt`, one test name + reason per line.
- **`test_codegen.sh` new modes**: `--update-goldens` (regenerate for
  deterministic tests), `--selfcheck` (must exit non-zero — teeth proof). These
  do not change the default no-arg behavior (run all, compare, exit 0/1).
- **`bcc test` contract**: exit 0 iff all discovered tests pass; human-readable
  per-test PASS/FAIL + a total; `--filter <substring>`; assertion failures print
  `<file>:<line>:` from the AST SourceLocation. One failing test never aborts
  the others.
- **`BLANG_SANITIZE` CMake option**: comma list (`address`, `undefined`);
  off by default so normal/CI non-sanitizer builds are unchanged.
- **Runtime tests**: registered with CTest so `ctest --test-dir <build>` runs
  them; each is a standalone C program returning non-zero on failure.
- **`fuzz_parse`**: standard libFuzzer entry (`LLVMFuzzerTestOneInput`) over the
  lexer+parser; built only when its CMake option is on and clang is the
  compiler; corpus at `test_files/fuzz/corpus/`.

## Key decisions

| # | Decision | Rationale | Alternatives rejected |
|---|----------|-----------|----------------------|
| D1 | Extend `test_codegen.sh` for goldens rather than a new runner | The script already builds/links/runs each test; goldens are one more comparison step | New harness (duplicates the build pipeline) |
| D2 | Exact-match goldens + explicit quarantine list | Exact match is the only thing with real teeth; quarantine is honest about non-determinism | Loose/regex matching (hides wrong output); normalizing everything (masks bugs) |
| D3 | All 63 deterministic tests get goldens (user decision) | Closes the "wrong output passes silently" hole completely, not partially | Subset-only (leaves exit-code-only tests) |
| D4 | `bcc test` reuses `genTestRunner` + AST SourceLocation | Locations already exist post-blang-ast; keeps failure reporting consistent with the compiler | A separate test-metadata channel |
| D5 | Wire the **existing** `--leak-check`/`--valgrind` into CI, don't rebuild it | The machinery works; the gap is purely CI integration | New leak tooling |
| D6 | Fuzzing is bounded (harness + fixed campaign + fix-or-quarantine) | A machine-checkable done condition needs a finite target; open-ended hunts don't converge | Coverage-guided open-ended campaign as a done condition |
| D7 | No external C test framework | Runtime has zero deps by design; a tiny assert runner keeps it that way | gtest/Unity (adds a dependency + build complexity) |

## Invariants — must not break

- `./run_tests.sh` (162 LLVM / 154 parse-only) and `./test_codegen.sh` (63/63)
  stay green at every unit boundary. A test that goes red under golden
  comparison because it prints genuinely wrong output is a **bug to fix**, not a
  reason to loosen the golden.
- Default (non-sanitizer, non-fuzz) build configuration and its artifacts are
  unchanged — new options are opt-in and off by default.
- `bcc test`'s `test{}` codegen changes must not alter normal (`bcc build` /
  single-file) codegen output.
- CI's existing `parse-only` and `with-llvm` legs keep working; new legs are
  additive.
- Quarantined non-deterministic tests still **run** (exit-code checked); they
  are never silently dropped.
