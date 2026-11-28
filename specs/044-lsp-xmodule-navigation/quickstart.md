# Quickstart: validating U4

## Prerequisites

You need `build/` (LLVM) and `build-asan/` built from this branch:
`cmake --build build && cmake --build build-asan`. The `test_build/*` projects
need no prior `bcc build`, because blangd resolves dependencies from source.

## 1. New behavior (goldens)

```bash
./test_lsp.sh                      # all transcripts, including the xmod_* goldens
./test_lsp.sh --selfcheck          # prints SELFCHECK: OK (teeth)
```

Expected: every `xmod_defn_*` result is a `file://${ROOT}/…/*.b` URI (none
ends in `.bmod`). Every `xmod_hover_*` line matches the table in
[contracts/navigation.md](contracts/navigation.md).

Check for G1 by hand:

```bash
grep -l '\.bmod"' test_files/lsp/xmod_*.expected.out   # prints nothing
```

## 2. Same-file behavior is frozen

```bash
git diff --stat 043-lsp-project-context -- 'test_files/lsp/*.expected.out' | grep -v xmod_   # empty
```

## 3. qcc is unchanged (shared-frontend extraction)

```bash
./run_tests.sh && BUILD_DIR=build-nollvm ./run_tests.sh
./test_codegen.sh && test_build/run_build_tests.sh
# IR byte-identity spot check vs the 043 qcc on, e.g., usebox, counterapp and codegen_prelude_no_import
```

## 4. Ownership and no-subprocess

```bash
ctest --test-dir build && ctest --test-dir build-asan   # parser_no_leak (usebox navigation loop), blangd_no_subprocess
```

## 5. Manual probe

Open `test_build/usebox/main.b` in an editor configured per `editors/README.md`
and go to the definition of `get` in `b.get()`. The editor opens
`test_build/boxq/boxq.b` at `pub fn get`.
