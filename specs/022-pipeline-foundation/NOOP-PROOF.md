# U0 no-op proof — byte-identical behavior

Evidence that the pipeline-foundation refactor preserves behavior exactly.
Reproduced on branch `epic/001-toolchain-and-stdlib/u0-pipeline-foundation`
(host x86_64-linux, llvm-18).

## Duplication gone (done-condition #1, grep-teeth #5)

| Pattern | Before | After |
|---------|--------|-------|
| `-filetype=obj` object-emission command vector | 4 sites | **1** (in `emitObject`; the 2nd grep hit is a doc comment) |
| `-mtriple=…apple-darwin` host-triple `#ifdef` | 4 sites | **1** (in `emitObject`) |
| `access( BCC_LLC_PATH …` llc discovery | 4 sites | **1** (in `resolveLlc`) |
| `auto findLib`/`findBuildLib` link lambda | 3 sites | **1** (in `appendRuntimeLibs`) |
| `blang_testrunner` link entry | 1 | **1** (single site; 2nd hit is a comment) |
| `"blang_db"` link entry | 2 | **1** |
| `llcCmd = {` old vector | 4 | **0** |

Call sites after refactor: `resolveLlc()` ×4, `emitObject()` ×4, `appendRuntimeLibs()` ×3.
Net: bcc.cpp −125 lines (150 insertions, 275 deletions).

## Byte-identical output (done-condition #2)

**Single-file path** (`bcc demos/01_fibonacci.b`):
- llc + link `-v` command lines (normalized): `diff BEFORE AFTER` → **IDENTICAL**
- emitted `.o` (`llvm-objdump-18 -d`): `diff` → **IDENTICAL**

  ```
  llc -filetype=obj --relocation-model=pic -mtriple=x86_64-unknown-linux-gnu <ll> -o <o>
  cc <o> …/libblang_db.a …/blang_sys …fs …net …json …buffer …array …string …runtime \
     -L/usr/lib/x86_64-linux-gnu -lsqlite3 -lpthread -o <out> -luv
  ```

**Combined `bcc build` path** (architect Rec-2 fixture `test_build/timerapp`):
- captured pre-refactor via `git stash` of bcc.cpp + rebuild, then post-refactor.
- llc + link `-v` command lines: `diff BEFORE AFTER` → **IDENTICAL** (db-first profile,
  `-lsqlite3 -lpthread … -luv` tail preserved).

## All four consolidated paths exercised

| Path | bcc.cpp block (was) | Exercised by | Result |
|------|---------------------|--------------|--------|
| single-file | 1478 | `test_codegen.sh`, byte-diff above | 107/0 |
| combined `bcc build` | 1140 | `test_build/timerapp` byte-diff | build+run ok |
| lib-build (`ar`) | 1018 | `bcc build` lib projects in suite | ok |
| `bcc test` | 328 | `bcc test test_files/pass/test_basic.b` | 1 passed |

## Gate suite (done-condition #3, unchanged counts)

| Gate | Result |
|------|--------|
| `run_tests.sh` (LLVM) | 195 / 0 |
| `run_tests.sh` (parse-only, `build-parse`) | 190 / 0 |
| `test_codegen.sh` | 107 / 0 (100 golden-checked, 7 quarantined) |
| `ctest --test-dir build` | 54 / 54 |
| `test_codegen.sh --leak-check` | 107 / 0, **Leaks: 0** |

Baseline (master @ ecd39ce) was identical: 195 / 190 / 107 / 54 / 0-leaks.
