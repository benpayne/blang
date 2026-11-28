# U2 converge gate (workplan U2 (f)): **converged**

- **Date**: 2026-11-26.
- **Tree**: `900a334`, which includes the SC-001 wording correction `e3c1d08` and review L1 (`lsp/Server.cpp` now replaces a result by erase+emplace, so `context` is released last).
- **Outcome**: `converged`, with **zero actionable findings**. `tasks.md` was left unchanged by converge, and all 38 tasks are complete.
- **Remaining unrequested item**: one LOW. The corpus oracle's child processes run with `ASAN_OPTIONS=detect_leaks=0`. It is justified by U1's oracle rule: blangd itself keeps leak detection on.

## Per-leg results on the final tree

| Leg | Result |
|---|---|
| make build / build-nollvm / build-asan | rc=0 ×3 |
| ctest build / build-nollvm / build-asan | 86/86 ×3, including `stdlib_locate`, `parser_no_leak` with the 50-reparse stdlib loop (LSan-clean under build-asan), `project_compile` and `blangd_no_subprocess` (+ teeth) |
| run_tests.sh build / nollvm / asan | 251/0 · 244/0 · 251/0 |
| test_lsp.sh build / nollvm | 78/0 each: corpus **179/179**, latency median **79.3 ms** (budget 500), all 6 `stdlib_*` goldens, 58 fail/sema, 12 pre-existing transcripts unchanged |
| test_lsp.sh --selfcheck | SELFCHECK: OK |
| test_codegen.sh · --leak-check · --selfcheck | 172/0 · **Leaks: 0** · SELFCHECK: OK |
| test_build/run_build_tests.sh | all pass |
| reach-in gate · examples · demos | OK · 6/6 · 10 passed, 0 failed |

## Extra leg, not in the gate list

`BUILD_DIR=build-asan ./test_lsp.sh` fails the same 8 entries as before L1. All 8 are the pre-existing parse-error-recovery leak documented in `baseline.md`: a raw `new` is lost when `COMPILE_ERROR` throws, and U2 did not touch the parser. The set is unchanged by this turn's edits, and it is filed as a follow-up.
