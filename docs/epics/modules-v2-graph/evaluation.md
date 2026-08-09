# Evaluation: modules-v2-graph

**Epic**: [overview.md](overview.md) · **Constitution**:
`.specify/memory/constitution.md` (v1.2.0)

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema suite (LLVM) | `./run_tests.sh` | fully green; every `fail/sema` + `fail/xmodule/` fixture matches its `.expected` pattern | every unit |
| parse/sema suite (parse-only) | `cmake -S . -B build-nollvm -DBLANG_ENABLE_LLVM=OFF && BUILD_DIR=build-nollvm ./run_tests.sh` | fully green (semantic checks fire without LLVM) | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | fully green; new tests have committed `.expected.out` goldens | every unit |
| leak check | `./test_codegen.sh --leak-check` | `Leaks: 0`, fatal on any leak | U2 (mandatory), U5, U6; others on any runtime-adjacent touch |
| build-system fixtures | `test_build/run_build_tests.sh` | green, incl. this epic's new lib+bin pairs (identity/collision, transitive dep, search-path) | U1, U3, U5, U6, U9 |
| LSP goldens | `./test_lsp.sh` | green (blangd keeps building against the extracted resolver; no functional change) | every unit (regression); U4 (links resolver) |
| runtime/unit tests | `ctest --test-dir build` | all pass, incl. the new **resolver component** unit test and **`ResolverReuseTest`** (constructs the resolver as `lsp/Compile.cpp` does; asserts a fixture resolves identically to the `qcc` path) | U4 |
| resolver call-site check | `grep -n <resolver-entry> qcc.cpp lsp/Compile.cpp` | both files call the single resolver entry point | U4 |
| identity/mangling regression | `nm`/IR assertion inside the U1 `test_build/` fixture script (two same-named exported generic types → distinct symbols) | distinct mangled symbols; no `linkonce_odr` collapse | U1 |
| cache invalidation | dedicated test: bump `.bmod` format (4→5) → warm cache entry misses (`BuildCacheTest`) | pass | U5 |
| reach-in / field privacy | `tools/check_no_field_reachins.sh` **and** a Sema `fail/*` fixture (KI-23) | grep gate exits 0; the Sema fixture makes a combine-mode reach-in a located error | U6 |
| CI (authority) | GitHub Actions on `master` (`gh run watch --exit-status`) | all jobs green | epic completion |

## Audit plan (instantiating the constitution)

| Audit | When | Rubric | Gates? |
|-------|------|--------|--------|
| design audit | before implementation of **U1** (canonical identity — the keystone), **U2** (the codegen crux), and **U5** (the `.bmod` foreign-ref format + transitive-closure model — changes the interface format and parses untrusted input); via `/speckit-clarify` + `/speckit-analyze` | model handles the general case + failure modes (un-named foreign-generic instantiation; portable-vs-realpath identity; double-free root cause; malformed `.bmod` foreign refs); no ARC-semantics change beyond the fix; conforms to D1–D17 | gates |
| spec audit | after each unit's speckit spec | spec covers the unit's REQ IDs; done conditions testable; no scope creep into Epic C (LSP), a package manager, re-export, or aliases beyond D8/D11 | gates |
| code review | before each unit's PR merges | constitution's five dimensions (correctness, tests/diagnostics, security, maintainability/robustness, style/spec-fidelity), each an explicit pass/finding by the distinct reviewer hire | gates |
| security review | **U2** (ARC/double-free/dtor), **U5** (`.bmod` is parsed input — foreign-ref/malformed-interface handling; no consumer-side crash on a corrupt `.bmod`) | untrusted-input rubric (constitution Quality Gate 7); findings recorded in the PR | gates |
| functional review | epic completion | every numbered bullet of overview.md's done condition demonstrated by a named, committed, CI-run test; each REQ's `Verified by` checked; **owner independent verification** on a clean rebuild (both build modes) before close-out | gates |

## Regression protection (evolve)

- **Baseline**: `master` @ the epic's start commit (`125fb0f` or later) —
  `./run_tests.sh`, `./test_codegen.sh` (both build modes),
  `./test_codegen.sh --leak-check`, `./test_lsp.sh`,
  `test_build/run_build_tests.sh`, `ctest --test-dir build` all green, and
  GitHub CI green. Any pre-existing red is reported before U1 starts, not
  absorbed. (Current baseline verified green 2026-08-09: run_tests 239/0 +
  232/0, test_codegen 165/0, leak 0, lsp 62/0, test_build pass, CI green.)
- **Must not change**: same-module behavior; the Epic-A export model; existing
  codegen goldens except tests deliberately migrated onto `import` lines at U6
  (each called out in its PR); the located-diagnostic format; ARC/ownership/
  concurrency semantics; cross-module generic instantiation; blangd behavior
  (beyond linking the resolver) and `test_lsp` goldens.
- **Test-count floor**: the pass/fail/codegen totals in `CLAUDE.md` may only
  grow; the quarantine list is unchanged
  (`docs/epics/test-validation/approved_quarantine.txt`).

## Evidence requirements

- Each unit's PR body: gate-command outputs (or CI links), the reviewer hire's
  per-dimension verdicts, and disposition of every finding.
- **U1**: the identity-model design artifact committed under the unit's speckit
  dir before implementation; the mangling-collision regression output.
- **U2**: the spike write-up (repro of the double-free, ASan/`--leak-check`
  before/after) committed under the unit's speckit dir.
- **U5**: `.bmod` foreign-ref goldens; the transitive-dependency build fixture;
  the cache-invalidation run.
- **U6**: the corpus-migration diff called out; the KI-23 Sema fixture; proof the
  global-injection block is removed.
- **Epic completion**: `CLAUDE.md` feature lists + `docs/language_design.md`
  updated in the same PRs as the behavior (Principle I); overview status log
  updated via fold-back; the four inherited KIs (KI-3/5/16/23) marked closed
  where done, or re-filed with rationale if any is deferred.
