# U4 qcc byte-for-byte equivalence vs the 043 qcc (T009/T010/T049)

Reference: `/tmp/blang-043`, a detached worktree of `7e6183c` (branch
`043-lsp-project-context`, not modified). It was built twice: `build/` (LLVM)
and `build-nollvm/` (`-DBLANG_ENABLE_LLVM=OFF`).

The harness is `specs/044-lsp-xmodule-navigation/qcc_equivalence.sh`. It takes
the `qcc` lines once from the LLVM build's `bcc` and replays each line against
both binaries of a pair, adding `--parse-only` for the non-LLVM pair. For every
input it compares the exit code, stdout, stderr and every output file with
`cmp`-style byte equality. It fails if category (a), (c) or (d) has 0 inputs.

## T009: after shared-compiler changes A (useCapabilityTypes lift) and B (type-reference recording)

| Pair | (a) codegen_*.b | (b) pass/fail/sema/cgfail | (c) lib `--emit-bmod` | (d) bin final line | Result |
|------|----------------:|--------------------------:|----------------------:|-------------------:|--------|
| LLVM `build/` vs 043 `build/` | 172 | 233 | 9 | 10 | `EQUIV: 424/424 identical (llvm)` |
| parse-only `build-nollvm/` vs 043 `build-nollvm/` | 172 | 233 | 9 | 10 | `EQUIV: 424/424 identical (parse-only)` |
