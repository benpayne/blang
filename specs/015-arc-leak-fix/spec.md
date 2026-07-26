# Spec: Codegen ARC rvalue-temporary leak fix (U8 / REQ-008)

**Epic**: test-validation · **Unit**: U8 · **Branch**: `epic/test-validation/u8-arc-leak-fix`
**Covers**: REQ-008 (from Open Question OQ-1) · **Design honored**: Principle IV
(memory safety verified, not assumed); invariants in `design.md` §Invariants
(default build artifacts unchanged; quality gates stay green).

## Problem

U4's `--leak-check` surfaced four codegen ARC leaks that were tracked in
`test_files/codegen_leak_quarantine.txt` (OQ-1): refcounted rvalue temporaries
that are allocated but never released. Recon during U8 refined the four into
**two distinct root causes**:

1. **Struct rvalue temporary not released.** A function/method that returns a
   user struct (allocated via `__blang_rc_alloc`) hands the caller an owned
   refcount-1 reference, but when that result is an unbound rvalue — e.g. the
   `fn().method()` receiver in `codegen_method_chain` (`get_valid_info().is_valid()`)
   — nothing releases it. Codegen tracked struct *literals* as temporaries but
   not struct *call/method results*.

2. **Array<T> rvalue temporary not released.** `codegen_file_io`,
   `codegen_fs_convenience`, and `codegen_http` leak the `Array<byte>` backing a
   `Buffer` (via `Buffer.get_bytes()` returning `self._bytes` retained on return,
   never released at the call site) and the `Array<string>` returned by
   `fs.list_dir`. Codegen had no array-temporary release discipline at all
   (there was `trackTempString`/`trackTempStruct` but no array analog), and the
   array-return retain was unconditional, double-counting fresh call results.

A latent contributor also surfaced: `__blang_sys_get_args()` returns a cached
immortal global (`g_sys_args`) as a **borrowed** reference with no element
destructor. The old unconditional array-return retain masked this; a correct
"owned return" discipline would let a caller free the shared global and orphan
its strings.

## Scope

**In scope**
- Track struct-returning call and method results as temporaries (mirror the
  existing struct-literal temp discipline), released at statement end unless the
  result is stored into a variable / struct field / enum payload / returned
  (each untracks — ownership transfers).
- Fix the lambda-body return path: `mCurrentFunction` is null inside a lambda,
  so the struct-return untrack was skipped, releasing the returned struct rvalue
  just before `ret` (a use-after-free the struct-temp tracking exposed). Untrack
  the returned value unconditionally.
- Add a symmetric **array-temporary** ARC discipline: `mTempArrays` +
  `trackTempArray`/`releaseTempArrays`/`untrackTempArray`, tracked at
  Array-returning calls/methods, released at statement end, untracked at every
  ownership-transfer site (var declaration/reassignment, struct-literal field,
  struct field assignment, enum payload, return).
- Make the array-return retain conditional (var/field sources only — mirroring
  the struct-return retain policy) so fresh call/method array results are not
  double-counted.
- Make `__blang_sys_get_args()` return an **owned** reference (retain the
  immortal global before returning), so the ABI is uniformly "array returns are
  owned" and the codegen discipline is correct for every array-returning
  function.
- Empty `test_files/codegen_leak_quarantine.txt` to comment-only (no active
  entries), with a header documenting the U8 resolution.

**Out of scope**
- Any change to default (non-sanitizer) build artifacts or normal codegen
  output beyond the added release/retain calls that balance refcounts.
- General field-assignment refcount management for non-array types (unchanged;
  U8 only adds the array untrack needed to avoid a double-free it could
  otherwise introduce).
- New language features; the fuzzing/CI/other epic units.

## Acceptance criteria (machine-checkable)

- **SC-001** `grep -vcE '^\s*#|^\s*$' test_files/codegen_leak_quarantine.txt` is
  `0` (no active quarantine entries).
- **SC-002** `./test_codegen.sh --leak-check` exits 0 and reports `Leaks: 0` and
  `Known-leaks (quarantined): 0`, with `codegen_method_chain`, `codegen_file_io`,
  `codegen_fs_convenience`, and `codegen_http` each reported `CLEAN` (not
  `KNOWN-LEAK`).
- **SC-003** Teeth preserved: `./test_codegen.sh --leak-check test_files/testblock/leak_probe.b`
  exits non-zero (an injected leak is still fatal).
- **SC-004** No double-free/UAF: the full `./test_codegen.sh --leak-check` run
  has zero `LEAK`/`SEGV`/AddressSanitizer-error lines across all tests.
- **SC-005** Unit boundary green: `./run_tests.sh` (186), `BUILD_DIR=build-parse
  ./run_tests.sh` (181), `BUILD_DIR=build-asan ./run_tests.sh` (186),
  `./test_codegen.sh` (63/63, goldens intact), `ctest --test-dir build` and
  `ctest --test-dir build-asan` (54) all exit 0.

## Risk & verification

This is codegen ARC work with real double-free/use-after-free risk. The
verification net is the **full 63-test `--leak-check` suite under ASan/LSan**:
any over-release manifests as an ASan SEGV/`AddressIsPoisoned` and any
under-release as a reported leak. Every ownership-transfer site is covered so
that a tracked array/struct temp is untracked exactly once when ownership moves,
mirroring the already-tested string/struct disciplines.
