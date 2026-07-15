# Code audit — U8 (ARC leak fix / REQ-008)

**Auditor role**: secondary reviewer (independence by discipline — distinct pass
from implementation; **all gates re-run from a from-scratch rebuild** of `build`,
`build-asan`, `build-parse`). Reviewer is the only merging actor.
**Verdict**: PASS — merge.

## Method

`rm -rf build build-asan build-parse` then reconfigured and rebuilt all three
from clean. Every acceptance command below was run against the freshly built
artifacts (no reuse of the implementer's build).

## Acceptance results (clean rebuild)

| ID | Check | Result |
|----|-------|--------|
| SC-001 | `grep -vcE '^\s*#|^\s*$' test_files/codegen_leak_quarantine.txt` | `0` ✔ (empty) |
| SC-002 | `./test_codegen.sh --leak-check` | exit 0; `Leaks: 0`; `Known-leaks: 0`; the four targets each `CLEAN` ✔ |
| SC-003 | `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b` | exit 1 (injected leak fatal) ✔ |
| SC-004 | grep of full `--leak-check` for `LEAK`/`SEGV`/`AddressIsPoisoned`/`use-after-free`/`double-free` | `0` lines ✔ |
| SC-005 | `./run_tests.sh` / parse-only / build-asan / `./test_codegen.sh` / `ctest` build+asan | 186 / 181 / 186 / 63-0 (goldens intact) / 54 + 54 ✔ |

## Carried spec-audit findings — resolution

- **F1 (teeth of the empty quarantine).** RESOLVED. Scripted a leak
  (`malloc(48)` never freed) into `test_files/codegen_method_chain.b`; since it
  is no longer quarantined, `--leak-check` went **RED/fatal** (exit 1, `LEAK`,
  not `KNOWN-LEAK`). Restoring the file returned it to `CLEAN` (exit 0). The
  empty quarantine gates the four ex-quarantined tests for real.
- **F2 (over-release / UAF).** RESOLVED. The full 63-test `--leak-check` under
  ASan/LSan has zero SEGV / `AddressIsPoisoned` / use-after-free / double-free
  lines (SC-004). Note during implementation the struct-temp tracking exposed a
  genuine pre-latent UAF in the lambda return path (`mCurrentFunction` null →
  struct-return untrack skipped → returned rvalue released before `ret`); the
  fix (unconditional untrack of the returned value) is verified by the clean
  `codegen_http` run, which previously SEGV'd under ASan with the incomplete
  fix.
- **F3 (borrowed-array ABI).** RESOLVED. All runtime functions returning
  `BlangArray*` return fresh/owned references (`__blang_array_create`,
  `__blang_array_create_from_data`, `__blang_array_concat`, `__blang_fs_list_dir`
  — the last with an element destructor) except `__blang_sys_get_args`, which
  returns the immortal global and now **retains** before returning, making the
  contract uniformly "array returns are owned." No other borrowed-array return
  exists.

## Correctness review of the ARC change

- The struct/array temp discipline is symmetric with the pre-existing,
  already-tested string/struct disciplines: a producing call/method tracks the
  temp; every ownership-transfer site untracks exactly once (var decl at
  CGStatements, var reassignment at CGExpressions, struct-literal field and
  field assignment at CGStruct, enum payload at CGEnum, return at
  CGStatements). Release happens once at statement end (`genBlock`) or is
  transferred at `return`.
- The array-return retain was narrowed to var/field sources, matching the
  struct-return retain policy — fresh call/method array results are no longer
  double-counted (the root of the `Buffer.get_bytes()`/`fs.list_dir` leaks),
  while local-var and field returns still receive the retain that keeps them
  alive across scope cleanup / hands the caller an owned reference.
- Default (non-sanitizer) build artifacts and normal codegen output are
  unchanged apart from added balancing retain/release calls; `./test_codegen.sh`
  goldens are byte-identical (57 golden-checked, 0 mismatches).

## Disposition

All findings resolved; all gates green from a clean rebuild; memory-safety
evidence (leak-check `Leaks: 0`, four `CLEAN`, teeth fatal) attached above.
Approved for squash-merge to `master`.
