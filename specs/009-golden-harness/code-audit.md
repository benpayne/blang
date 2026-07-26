# Code Audit — U1 Golden-output harness (`009-golden-harness`)

**Phase**: 4 (code audit, pre-merge) · **Reviewer pass**: independent of the
implementation; every gate/SC command re-run **fresh** from a clean tree (not
trusting implementer output). **Gate**: blocks merge until PASS.
**Rubric**: evaluation.md §Per-unit gates + §Audit plan "code review" row +
constitution Quality Gates & Audit Pattern.

## SC verification (each command re-run by the reviewer)

| SC | Command(s) | Result | Evidence |
|----|-----------|--------|----------|
| SC-001 | `./test_codegen.sh` | **PASS** | exit 0; `Passed: 63 / Failed: 0`; `Golden-checked: 2  No golden: 55  Quarantined: 6`. |
| SC-002 | `./test_codegen.sh --selfcheck` | **PASS** | exit **1** (non-zero); output contains literal `SELFCHECK: OK`; grep found it. |
| SC-003 | patch test → run → revert → run | **PASS** | source-patched `println("z = {}")→("WRONG = {}")` (program still exits 0) → suite exit **1** with diff `-z = 80 / +WRONG = 80`; revert → suite exit **0**. Green→red→green on the *normal* path. |
| SC-004 | quarantine a name; run | **PASS** | 6 quarantined tests report `(quarantined: exit-code only)` and still compile+run; a quarantined test forced to `return 7` still **FAIL**s `(runtime exit 7)` — quarantine waives only golden compare, not exit-code teeth. |
| SC-005 | sha256 goldens pre/post `--selfcheck` | **PASS** | `codegen_binexpr` `e5e0dff4…` and `codegen_simple` `e3b0c442…` (canonical empty-file hash) identical before and after selfcheck; committed goldens byte-for-byte unchanged. |
| SC-006 | `./run_tests.sh`; `BUILD_DIR=build-parse ./run_tests.sh`; `./test_codegen.sh` | **PASS** | LLVM parse suite **186/186**; parse-only **181/181**; codegen **63/63**. Baseline preserved. |

## Explicit teeth verification (epic requirement: no trivially-passing gate)

- **(a) Present golden + wrong output ⇒ FATAL.** Source-patched binexpr (exit 0,
  wrong stdout) → suite red with expected/actual diff. Golden mismatch, not exit
  code, drove the failure. ✅ (Closes carried-forward **F1** half-1.)
- **(b) Missing golden ⇒ never a silent pass.** Non-goldened tests report a
  distinct visible `NO GOLDEN` status and are counted under `No golden: 55`,
  never rolled into golden passes. ✅ (Closes carried-forward **F1** half-2.)
- **(c) `--selfcheck` teeth.** Exits non-zero AND emits `SELFCHECK: OK` only after
  the corrupted-temp-copy comparison actually went red; broken/недetecting
  comparator would hit a distinct `SELFCHECK: FAILED — …` path and never print
  `SELFCHECK: OK`. ✅
- **(d) Committed goldens immutable under selfcheck.** sha256 stable. ✅
- **(e) Green→red→green flip.** Demonstrated on both a golden edit and a source
  edit; revert restores green. ✅
- **(f) Suites green at the boundary.** 186 / 181 / 63. ✅
- **Normalization has no loose matching.** Direct boundary probe of
  `golden_matches`: actual `z = 80\n` vs golden `z = 80` (0 nl) → MATCH; vs
  `z = 80\n` (1 nl) → MATCH; vs `z = 80\n\n` (2 nl) → DIFFER. Exactly
  single-trailing-newline semantics via `cmp` on normalized bytes — no
  substring/regex/whitespace matching. ✅ (design.md D2.)

## Design/constitution conformance

- **D1** honored: change confined to `test_codegen.sh` + `codegen_quarantine.txt`
  + two sample goldens; no new runner, no compiler/runtime/CMake/CI edits.
- **D2** honored: exact match, single-trailing-newline the only transform,
  explicit quarantine file (not loose matching).
- **FR-010 / leak semantics unchanged**: `--leak-check` smoke → `CLEAN`,
  `Leaks: 0`, exit 0, no golden-compare in leak mode; the exit-23 LEAK path is
  byte-identical in behavior to pre-U1 (still non-fatal — U4 owns making it
  fatal). No leak-semantics change slipped in.
- **Sample golden correctness (not just presence)**: `codegen_binexpr` golden
  `z = 80` independently matches the test's own stated expectation
  (`// Expected: z == 80 (5 + 15 * 5 = 80)`); `codegen_simple` legitimately prints
  nothing (empty golden = empty-stdout edge case).
- Help text updated to document `--update-goldens` / `--selfcheck`.

## Findings

- **F1 (carried from spec audit): RESOLVED.** Both halves verified above —
  present golden ⇒ wrong output fatal (a); missing golden ⇒ visible non-fatal,
  never silent (b). The non-fatal missing-golden window is auditable (`No golden`
  count) and is closed by U2's gate.
- No blocking findings. No teeth-free gate. No masked bug. Working tree verified
  clean after all adversarial probes (git status empty; goldens/quarantine/source
  hashes intact).

## Verdict

**PASS — approved for merge.** All SC-001..SC-006 pass on fresh re-run; teeth
proven on the normal path, the selfcheck path, and the normalization boundary;
leak semantics untouched; scope confined per D1/D2. Proceed to Phase 5
(squash-merge to master).
