# Baseline and post-unit record (U1)

- **Base**: `epic/modules-v2-lsp` = `2f68efe28ed0a7f9bffb2e2d05b16f7eee129953`. The KG-7 commit `efb099a` is an ancestor of `HEAD`.
- **Pre-unit reference builds**: a worktree at `/tmp/u1-base`, with `/tmp/u1-base/build` (LLVM: `qcc`, `bcc`, `blangd`) and `/tmp/u1-base/build-asan` (`qcc`, `blangd`). The `build-asan` tree was added to establish that the leak predates the unit.
- **Method.** The pre-unit binaries are run **live** against the post-unit binaries, with identical argument lists, inputs and working directories. Output is compared with `cmp` on stdout, stderr and exit code. This takes the place of T004's stored `/tmp/u1-ref` capture and checks the same thing. The sweep scripts were `/tmp/u1-scripts/ident.sh` and `ident2.sh` (scratch, not committed).
- **Base gate counts.** The suites were not re-run on the base tree; the counts below are evaluation.md's base figures. Every post-unit count matches them exactly, and the byte-identity sweeps compare the base binaries directly.

## Gates — final tree (branch `041-shared-project-compile`, all U1 code committed)

| Gate | Base (evaluation.md) | Final |
|---|---|---|
| `./run_tests.sh` (LLVM) | 251/0 | 251/0 |
| `BUILD_DIR=build-nollvm ./run_tests.sh` | 244/0 | 244/0 |
| `BUILD_DIR=build-asan ./run_tests.sh` | — | 251/0 |
| `./test_codegen.sh` | 172/0 | 172/0 |
| `./test_codegen.sh --leak-check` | Leaks: 0 | 172/0, Leaks: 0 |
| `./test_codegen.sh --selfcheck` | SELFCHECK: OK | SELFCHECK: OK |
| `test_build/run_build_tests.sh` | pass | All build-system checks passed |
| `ctest` build / build-asan / build-nollvm | 82/82 each | 85/85 each (+`project_compile`, `blangd_no_subprocess`, `blangd_no_subprocess_teeth`) |
| `./test_lsp.sh` / `--selfcheck` | 70/0 / OK | 70/0 / SELFCHECK: OK |
| `tools/check_no_field_reachins.sh examples/ test_build/` (+ `--selfcheck`) | OK | OK (+ SELFCHECK: OK) |
| `examples/*/test_*.sh` (6) | pass | 6/6 rc=0 |
| `make -C demos run` | 10/0 | 10 passed, 0 failed |
| Zero-golden-edit commands (F7) | — | both print nothing; the only additions are under `test_files/project_compile/` |

## Byte-identity (SC-005, T026/T027) — pre-unit vs. final

| Sweep | Runs | Diffs |
|---|---|---|
| Diagnostics: every `pass/`, `fail/`, `fail/sema/`, `fail/warn/`, `cgfail/`, `xfail/`, `codegen_*.b`, `examples/*`, `test_build/*` file; single-file, `--json`, combine (bcc's stdlib rule), combine `--json`, combine `-Werror`; plus `--dump-locations` on every `pass/` file | 2279 | 0 |
| IR (`--combine -S` on every `codegen_*.b`, `--module-origin`), `.bmod` emission (every `fail/xmodule` lib, `test_build/mathlib`), xmodule consumers (plain / `--json` / combine, with and without `--bmod-origin`; T027's authoritative interface gate), a missing input file, and the `-v` trace | 596 | 0 |
| `bcc -v -S` on the six overview files | 6 | 5 identical. `codegen_prelude_no_import.b` differs only in the generic digest, which hashes each build's own absolute stdlib path. With identical stdlib paths, old and new are byte-identical (236,210 bytes of IR). |

`test_files/pass/import_basic.b` is included in the sweep. It stays lenient (no import diagnostics), identically in both builds.
