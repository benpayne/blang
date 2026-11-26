# Quickstart: validating U1 (behavior-neutral extraction)

This is a run guide for proving the extraction moved code and changed **nothing observable**. The contracts are in [contracts/](contracts/); the ordering checklist is in [research.md](research.md) R3.

## 0. Prerequisites

- On branch `041-shared-project-compile`, whose base `epic/modules-v2-lsp` contains `efb099a`:

  ```bash
  git merge-base --is-ancestor efb099a HEAD
  ```
- LLVM 18 dev packages (for `build/`), and clang/gcc with ASan (for `build-asan/`).

## 1. Capture the pre-unit reference binaries (once, before any code change)

```bash
git worktree add /tmp/u1-base epic/modules-v2-lsp
cmake -S /tmp/u1-base -B /tmp/u1-base/build -DCMAKE_BUILD_TYPE=Debug && make -C /tmp/u1-base/build -j"$(nproc)" qcc bcc
# Baseline gate counts on the base (record them; report any pre-existing red, do not absorb it):
(cd /tmp/u1-base && ./run_tests.sh && ./test_codegen.sh && ./test_lsp.sh && ctest --test-dir build)
```

## 2. Gates after each phase (both modes + sanitizer)

```bash
git diff --stat epic/modules-v2-lsp..HEAD -- test_files/   # ONLY new files under test_files/project_compile/
./run_tests.sh                                            # LLVM build: baseline count, 0 fail
BUILD_DIR=build-nollvm ./run_tests.sh                     # parse-only: baseline count, 0 fail
./test_codegen.sh && ./test_codegen.sh --leak-check       # Leaks: 0
./test_codegen.sh --selfcheck                             # prints SELFCHECK: OK (exits non-zero by design)
test_build/run_build_tests.sh
ctest --test-dir build && ctest --test-dir build-asan     # includes project_compile, parser_no_leak
./test_lsp.sh && ./test_lsp.sh --selfcheck
tools/check_no_field_reachins.sh
for t in examples/*/test_*.sh; do (cd "$(dirname "$t")" && "./$(basename "$t")"); done
make -C demos run
```

**Expected**: every command green at the baseline counts, with ctest +1 (`project_compile`) after phase D-5. **If anything is red, do not update a golden. Stop and raise a `BLOCKER:`** (plan constraint 3).

## 3. Byte-identity spot check (SC-005)

```bash
OLD=/tmp/u1-base/build NEW=build
for f in test_files/codegen_prelude_no_import.b test_build/myapp/main.b \
         test_files/codegen_http_routing.b test_build/counterapp/main.b \
         examples/kv/main.b examples/todo_app/main.b; do
  for v in OLD NEW; do
    d=${!v}; "$d/qcc" --parse-only "$f" >"/tmp/u1.$v.out" 2>"/tmp/u1.$v.err"; echo $? >>"/tmp/u1.$v.err"
  done
  cmp /tmp/u1.OLD.err /tmp/u1.NEW.err && cmp /tmp/u1.OLD.out /tmp/u1.NEW.out || echo "DIFF: $f"
done
```

Repeat with the `qcc` command line that `bcc -v -S <file>` prints. Run each binary's own `bcc` so the stdlib paths are each build's, and compare after substituting the build-dir prefix. Then repeat with:
- `--json`;
- `-Werror` (on `test_files/fail/warn/*`);
- `--dump-locations test_files/pass/func_simple.b`;
- `--emit-bmod` over `test_build/mathlib/*.b` (`cmp` the two `.bmod` files).

**Expected**: no `DIFF:` lines.

## 4. Structural checks (SC-002 / SC-003)

```bash
grep -n "ProjectCompile::run" qcc.cpp lsp/Compile.cpp              # one call in each
grep -nE "isUserFile|isPreludeModule|injectBmodSymbols|authoritativeModules|Sema::analyze" qcc.cpp   # no output
grep -nE "resolveStdlibFiles|parseImports" bcc.cpp                  # no output
grep -n "BlangStdlib::" bcc.cpp                                     # the four call sites
grep -nE "popen|system\(|fork|exec" ProjectCompile.cpp StdlibSelect.cpp   # no output (I8 / S2)
```

## 5. The new test on its own

```bash
ctest --test-dir build -R project_compile --output-on-failure
ctest --test-dir build-asan -R project_compile --output-on-failure   # LSan-clean
```

**Expected**: C1–C6 report OK. A missing `qcc` oracle is a hard failure, never a skip.
