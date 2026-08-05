# Evaluation: modules-v2-exports

**Epic**: [overview.md](overview.md) · **Constitution**:
`.specify/memory/constitution.md` (v1.2.0)

## Harnesses

| Harness | Command | Expected | Used by |
|---------|---------|----------|---------|
| parse/sema suite (LLVM build) | `./run_tests.sh` | fully green; every `fail/sema` and `fail/xmodule/` fixture matches its `.expected` pattern (the `fail/xmodule/` lib+consumer runner leg is added by U3) | every unit |
| parse/sema suite (parse-only) | `cmake -S . -B build-nollvm -DBLANG_ENABLE_LLVM=OFF && BUILD_DIR=build-nollvm ./run_tests.sh` | fully green (semantic checks fire without LLVM) | every unit |
| codegen E2E + goldens | `./test_codegen.sh` | fully green; new tests have committed `.expected.out` goldens | every unit |
| leak check | `./test_codegen.sh --leak-check` | `Leaks: 0`, fatal on any leak | U1, U2, U5 (mandatory); others on any runtime-adjacent touch |
| build-system fixtures | `test_build/run_build_tests.sh` | green, including this epic's new lib+bin pairs | U1, U2, U3, U5 |
| LSP goldens | `./test_lsp.sh` | green (no regression; blangd untouched) | every unit |
| runtime units | `ctest --test-dir build` | all pass | U1 |
| examples | each `examples/*/` integration script | pass | U4, U5 |
| field reach-in sweep | `tools/check_no_field_reachins.sh examples/ test_build/` (committed script with a maintained opaque-type field list; U4 seeds it, U5 extends it) | exit 0 | U4 (examples/ only), U5 (full) |
| cache invalidation | dedicated test added in U2 (bump format version → warm cache entry misses) | pass | U2, U5 |

## Audit plan (instantiating the constitution)

| Audit | When | Rubric | Gates? |
|-------|------|--------|--------|
| design audit | before implementation of each unit; **named checkpoint for U4** (stdlib API) and U1 (ABI spike write-up) | design handles the general case + failure modes; no attack-surface widening; fits existing phase architecture; conforms to D1–D17 — via `/speckit-clarify` + `/speckit-analyze` | gates |
| spec audit | after each unit's speckit spec | spec covers the unit's REQ IDs; done conditions testable; no scope creep into Epic B | gates |
| code review | before each unit's PR merges | constitution's five dimensions (correctness, tests/diagnostics, security, maintainability/robustness, style/spec-fidelity), each an explicit pass/finding by the reviewer hire | gates |
| security review | U1 (allocation ABI, dtor function pointers), U2 and U5 (`.bmod` is parsed input — malformed-interface handling; no consumer-side crash on a corrupt `.bmod`) | untrusted-input rubric from constitution Quality Gate 7; findings recorded in the PR | gates |
| functional review | epic completion (end of U5) | every numbered bullet of overview.md's done condition demonstrated by a named, committed, CI-run test; the `Verified by` column of each REQ checked | gates |

## Regression protection (evolve)

- Baseline: master @ the epic's start commit — `./run_tests.sh`,
  `./test_codegen.sh` (both build modes), `./test_codegen.sh --leak-check`,
  `./test_lsp.sh`, `test_build/run_build_tests.sh`, `ctest --test-dir build`
  all green. Any pre-existing red must be reported before U1 starts, not
  absorbed.
- Must not change: same-module program behavior; existing codegen goldens
  (except tests deliberately migrated off field reach-ins — each such golden
  change must be called out in its PR); diagnostic format; quiet clean
  compiles; cross-module generic instantiation.
- Test-count floor: the pass/fail/codegen totals in CLAUDE.md may only grow;
  quarantine list unchanged (`docs/epics/test-validation/approved_quarantine.txt`).

## Evidence requirements

- Each unit's PR body: gate-command outputs (or CI links), the reviewer
  hire's per-dimension verdicts, and disposition of every finding.
- U1: spike write-up (what was tried, ASan output) committed under the
  unit's speckit dir.
- U4: the approved API-design artifact committed before implementation
  commits.
- U5: `tools/check_no_field_reachins.sh` exit-0 run recorded in the PR, and
  the resolved answer to open question #1 recorded in overview.md.
- Epic completion: overview.md status log updated via fold-back; CLAUDE.md
  feature lists + `docs/language_design.md` updated in the same PRs as the
  behavior they describe (Principle I).
