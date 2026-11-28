# U3 baseline and post-unit record

- **Branch**: `043-lsp-project-context`.
- **Base**: `git merge-base --is-ancestor dc39603 HEAD` succeeds. The base is U2's converged tip.
- **Pre-unit HEAD**: `9bd4858`. Its source tree is identical to `dc39603`; only `specs/`, `docs/` and `.specify/` changed.
- **Pre-unit gate counts**: U2's final converge record, `specs/042-lsp-stdlib-context/checklists/converge.md`:
  - ctest 86/86 ×3;
  - `run_tests` 251/244/251;
  - `test_lsp` 78/0, both modes;
  - `test_codegen` 172/0, Leaks: 0;
  - `run_build_tests` all pass;
  - stdlib corpus 179/179;
  - reach-in, examples and demos green.
- **`bcc` reference (T002)**: the pre-unit `bcc`, `qcc` and `stdlib` are in `/tmp/u3-ref/bin`, and `test_build` is copied to `/tmp/u3-ref/tree`. `/tmp/u3-ref/capture.sh` records `bcc build -v` for the 10 binaries, plus negative legs: a malformed toml with an unpinned git dependency, the cycle a↔b, a missing path dependency, and diamond under `-v`. Outputs go to `/tmp/u3-ref/before/`, with the cache dir and bin dir normalized. These are scratch files and are not committed.

## T007: R3 neutrality (`bcc` after the `ProjectLayout` / `walkDependencies` / `scanImportsAll` / `loadFromText` moves)

- **`bcc` outputs**: all **28/28** are `cmp`-identical between the pre-unit and post-move `bcc`. This covers stdout, stderr and exit code, with the cache dir and bin dir normalized. The set is:
  - `bcc build -v` on all 10 binaries;
  - the negative legs: malformed toml plus an unpinned git dependency (`<toml>:4: parse error: expected key = value` and `error: git dependency 'greet' must pin a version…`); the cycle a↔b (`error: circular dependency detected: …/cyc_a`); and a missing path dependency (`error: no blang.toml found in …/../does_not_exist`, the N1 2a path);
  - the diamond leg under `-v`.
- **`test_build/run_build_tests.sh`**: all build-system checks pass (107 PASS lines), including the git-dependency leg, the diamond leg and the `.bmod` goldens.
- **Grep gates**:
  - `grep -n 'buildProject(' bcc.cpp` → only `1071:` (the definition) and `1402:` (the `runBuild` call);
  - `grep -nE 'building\.(insert|count|erase)' bcc.cpp` → nothing;
  - `grep -nE "^static .*(discoverSourceFiles|resolvePath|gitCacheRoot|shortHashHex|mergeDep)" bcc.cpp` → nothing.
- **`blangd_no_subprocess`**: OK.
- **ctest**: 86/86 in `build`. `project_layout` was added separately (`ProjectLayoutTest: OK`).

## T009: R1 neutrality (emitter source-text provider)

- **`--emit-bmod` outputs**: **17/17** `cmp`-identical, pre-unit `qcc` vs. post-change `qcc` (no provider: the default path). The set is every `fail/xmodule/*/lib.b` plus the `mathlib`, `boxq`, `midx`, `counterlib`, `todolib` and `printlib` sources.
- **`run_build_tests.sh`**: all pass. The `test_files/golden/bmod/` goldens are unchanged.
- **H3 lock (`project_compile` C9)**:
  - a provider hit slices the **buffer** body, even with the disk cache pre-seeded with different text;
  - a provider miss is reported in `missing` and emits neither the disk body nor the definition.
  - Teeth: forcing the emitter to ignore the provider makes C9 fail 3 checks.

## T013: R2 checkpoint (one isolated run per compile; U2's `sharedResolver` superseded)

- **`test_lsp.sh`**: 78/0 in `build` and in `build-nollvm`. The stdlib corpus is 179/179, and all U2 goldens are unchanged.
- **ctest**: 87/87 in each of `build`, `build-nollvm` and `build-asan` (86 + `project_layout`). The `parser_no_leak` U2 loop is LSan-clean in `build-asan`.
- **`grep -n gScope lsp/*.cpp lsp/*.h lsp/AstLocator.cpp Sema.cpp`**: the only hits outside comments are `GlobalAliasScope`'s save, install and restore (`lsp/Run.h:38-39`). No post-compile code reads the alias.
- **No cached builtin-scope pointers**: `grep` for `static` `Type*`/`Scope*`/`SmartPtr` caches of `gScope` lookups in `Q*.cpp`, `Sema.cpp`, `Type.h` and `Expression.h` found none. A fresh global scope per run is therefore safe.

## T017 / N5: blangd derives no project rule

`grep -nE 'onPath|mergeByOrigin|building' lsp/*.cpp lsp/*.h` returns one hit, `lsp/ProjectContext.cpp:118: for ( const auto &d : *node.onPath )`. It sits inside `BlangdVisitor::onCycle`, which only **renders** the cycle message from the walk's own on-path list. The cycle detection, the fold and the merge-by-origin all live in `BlangProject::walkDependencies` (shared with `bcc`). `lsp/` has no `mergeByOrigin` and no `building` set.

## T043: final verification (HEAD before this record: f717adc)

| Gate | `build` | `build-nollvm` | `build-asan` |
|---|---|---|---|
| `run_tests.sh` | 251/0 | 244/0 | 251/0 |
| ctest | 87/87 | 87/87 | 87/87 |
| `test_lsp.sh` | **91/0**, with the stdlib corpus 177/177 and the project corpus 29/29 | **90/0**, with the stdlib corpus 177/177; the project corpus SKIPs (no LLVM, per R2) | 82/8, see the note below |
| `test_lsp.sh --selfcheck` | `SELFCHECK: OK` | `SELFCHECK: OK` | `SELFCHECK: OK` |

**The build-asan `test_lsp` 8** are U2's documented pre-existing parse-error-recovery leak: a raw `new` in `ReturnStatement::Parse`/`StringInterpolation::Parse` is lost on a `COMPILE_ERROR` throw (`specs/042-lsp-stdlib-context/checklists/baseline.md`). The entries are `diag_open`, `diag_change`, `stdlib_missing`, `stdlib_missing_env`, the three `fail/sema` fixtures (`interp_unknown_base`, `undefined_function`, `undefined_variable`) and one corpus session exit.

- The count matches U2's 8.
- The session-exit entry is now the **project** corpus, from the xmodule consumers whose compile takes the parse-error path. The **stdlib** corpus is now LSan-clean (177/177).
- Every new `project_*` golden and the 17 clean projects are LSan-clean (`corpus-run.md` T042).

The other gates:

- **`test_codegen.sh`**:
  - `--leak-check`: 172 passed, 0 failed, **Leaks: 0**;
  - `--selfcheck`: `SELFCHECK: OK`;
  - plain: 172/0, with 165 golden-checked and 7 quarantined.
- **`run_build_tests.sh`**: rc 0, 107 PASS lines.
- **T007** (`bcc build -v` on the 10 binaries, the negative legs and diamond `-v`): pre-unit vs. final `bcc`/`qcc`, **28/28 `cmp`-identical**.
- **T009** (`--emit-bmod`, 11 xmodule libs plus mathlib, boxq, counterlib, todolib, printlib and midx): pre-unit vs. final `qcc`, **17/17 `cmp`-identical**.
- **SC-007 / M1**: the stdlib corpus is 177/177 in both modes, and `myapp` and `counterapp` PASS in the project corpus, so **179/179 are accounted for**.
- **Reach-in gate**: `OK: no field reach-ins found in: examples/ test_build/`.
- **Examples**: 6/6 `test_*.sh` rc 0.
- **Demos**: `make -C demos run` gives 10 passed, 0 failed.
- **Standing rule 3** (`git diff --name-status 042-lsp-stdlib-context HEAD -- test_files/`): one `M`, `test_files/lsp/corpus.txt`, and 44 `A`, all under `test_files/lsp/projects/` or `test_files/lsp/project_*`. No other golden or `.expected` file changed anywhere.
- **`git diff 285b362 HEAD -- specs/043-lsp-project-context/spec.md`**: empty.

## T044: structural greps

- `grep -n "ProjectCompile::run" lsp/Run.h lsp/*.cpp` has exactly one call, `lsp/Run.h:56` (`runPlan`). Every blangd compile goes through it.
- `grep -nE "runCommand|popen|fork\(|exec[lv]p?\(|system\(" ProjectLayout.cpp lsp/*.cpp lsp/*.h` matches only the comment `ProjectLayout.cpp:3` ("Process-free: no fork/exec/system/popen, no network.").
- `grep -nE "opendir|\"blang.toml\"" lsp/*.cpp` matches only the diagnostic **codes** `"blang-toml"` (`lsp/Compile.cpp:118,167`). There is no `opendir` and no toml-name rule in `lsp/`: discovery, listing and resolution are `BlangProject::`'s.
- `grep -rn "sharedResolver" lsp/` prints nothing.

## T046 (converge cycle 1): declared-at fallback for dependency-compile-failure and cycle diagnostics

- **Change.**
  - The shared `BlangProject::WalkNode` gains `parent` and `viaDep`, the edge that reached the node. `detail::visit` already held both. The change is additive and `bcc`'s visitor never reads them.
  - `BlangdVisitor::derive` sets `declaredAt` from the first edge that reached the dependency.
  - `BlangdVisitor::onCycle` sets `declaredAt` from the edge that closes the cycle.
  - So when such a diagnostic falls back to 1:1, it names `<toml>:<line>` (FR-009 / D-5).
- **Locks.**
  - **New golden `project_dep_failed_unimported`**: the document does not import the failing `lib`. It publishes one 1:1 Error, `… failed to compile: …lib.b:2:12: … (declared at …/dep_failed_unimported/blang.toml:7)`, plus the unrelated error at 6:10.
  - **`project_cycle`**: re-generated and hand-reviewed. The only change is that the 1:1 message gains `(declared at ${ROOT}/test_files/lsp/projects/cycle_b/blang.toml:7)`. This golden was added by U3, so no pre-existing golden changed.
  - **`project_layout`**: asserts each node's edge (`leaf<-l:leaf:4`, `r<-top:r:5`, `top<-root`, …) and the cycle-closing edge `cycle<-cb:ca:4`. Teeth: a wrong line number fails the check.
  - A transitive failure (`app -> mid -> leaf`) names `mid/blang.toml:6`.
- **Re-verification on the final tree.**

  | Gate | `build` | `build-nollvm` | `build-asan` |
  |---|---|---|---|
  | ctest | 87/87 | 87/87 | 87/87 |
  | `run_tests.sh` | 251/0 | 244/0 | 251/0 |
  | `test_lsp.sh` | 92/0, with the stdlib corpus 177/177 and the project corpus 29/29 | 91/0 | 83/8 |
  | `test_lsp.sh --selfcheck` | OK | OK | OK |

  - The `build-asan` 8 are U2's known parse-error-recovery leak: `diag_open`, `diag_change`, `stdlib_missing`, `stdlib_missing_env`, three `fail/sema` fixtures and the project-corpus session exit. The stdlib-corpus session is **not** among them (177/177 PASS under ASan).
  - Opened individually, the **17/17** clean projects are LSan-clean, and all 9 `project_*` goldens run clean under `build-asan/blangd`.
  - **`test_codegen.sh`**: `--leak-check` 172/0 with Leaks: 0; `--selfcheck` OK; plain 172/0.
  - **`run_build_tests.sh`**: rc 0.
  - **Reach-in gate, examples, demos**: OK; 6/6; 10/0.
  - **T007**: `bcc build -v` against a fresh full build of `042-lsp-stdlib-context` gives **28/28 `cmp`-identical** (11 builds rc 0, 3 negatives rc 1).
  - **T009**: `--emit-bmod`, **17/17** identical.
