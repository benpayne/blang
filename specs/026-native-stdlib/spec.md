# Spec: Native stdlib — `math`, `time`, `random`, `env`

**Epic**: 001-toolchain-and-stdlib · **Unit**: U4 · **Branch**: `epic/001-toolchain-and-stdlib/u4-native-stdlib`
**Covers**: REQ-005 · **Speckit**: `native-stdlib` · **Status**: Implemented (gates green; awaiting code review)
**Status update**: Merged (code review APPROVE; gates green at -O0/-O2/-g).
**Reviewed-by: code-reviewer** (Rex; audit self-completed by manager after runtime
interruption, disposition recorded). Verdict APPROVE; **0 blocking**. Verified:
(1) the match temp-string fix was **hardened during review** from a full
`releaseTempStrings()` to a **surgical snapshot** (`tempMarkBeforeSubject`) that
releases only the temps created during subject evaluation — closing a latent
use-after-free where a match subexpression alongside a sibling string-literal
temp (`foo("lit", match ...)`) could have freed the live sibling; re-verified on
nested matches, no-fall-through, non-call subjects, and `println("p {}", match)`;
payload strings are untracked at construction (`CGEnum.cpp:76`) + released via
`mEnumScopeStack`, so never among the flushed temps. (2) `CreateFNeg` blast
radius is minimal (scalar float/double only; integer negation byte-unchanged).
(3) every extern↔C signature checked (long↔int64_t, double↔double, abs_int
int↔int); `env.get` frees its cstring copy. (4) 6 touchpoints complete per
module, single `appendRuntimeLibs` link site (no re-duplication), `-lm` trailing
after `blang_math.a`. Full suite + `--leak-check` clean at -O0/-g.
**Reviewed-by: architect** (Vera). Verdict PASS-WITH-FINDINGS; **0 blocking**.
All 4 MINOR/advisory findings folded in during implementation:
- **F1** (link-gating framing): corrected — the `.a` is offered to the linker
  **unconditionally** (like sys/fs/net) and dropped when unreferenced; only the
  `.b` is import-gated (kKnownOrder). Test-plan item 5 softened from
  "byte-identical" to "no ambient BLang linkage / no libm dependency when
  unused" (a non-importing program builds+runs unchanged — verified: full suite,
  most tests import nothing, stays green + leak-clean).
- **F2** (`-lm`): appended as a trailing linker token AFTER `blang_math.a` (GNU
  ld order), in the single `appendRuntimeLibs`; harmless when unused.
- **F3** (time surface): `Duration`/`strftime` date-formatting is **deliberately
  deferred** to a later stdlib tier (noted in `stdlib/time.b`); U4 ships the
  three clock primitives, satisfying REQ-005's time/date via epoch/monotonic.
- **F4** (env prose): the env determinism paragraph rewritten; the test asserts
  `has("PATH")` (present in the run env) + an unset var → `none` (deterministic).

**Bugs surfaced by the new float/stdlib code and fixed in-unit** (the predicted
"new tests surface bugs" pattern): (B1) unary float negation emitted an invalid
integer `sub` constexpr → now `CreateFNeg` (`CGExpressions.cpp`); (B2)
`math.abs_int` ABI mismatch (`long` C param vs BLang `int`) → C aligned to `int`;
(B3) a `match` on an enum subject whose subject-call had string-literal args
leaked the arg temp on non-first arms (pre-existing) → temp strings flushed in
the pre-branch block (`CGEnum.cpp`). All under `--leak-check`.
**Depends on**: U0 (merged @ 00106d6 — new `.a`s are added in the **one** consolidated `appendRuntimeLibs` link site; imports resolve via `kKnownOrder`) · independent of U1/U2/U3.

## Problem (recon, file:line @ master c7f9b14)

- **Zero** of `math`, `time`, `random`, `env` exist. A BLang program can open
  sockets and query a DB but cannot compute a square root, read the clock, get a
  random number, or read an environment variable.
- The stdlib pattern is a **two-layer sandwich**, proven by `sys`/`fs`/`net`:
  `stdlib/<m>.b` declares `extern fn __blang_*` prototypes + idiomatic BLang
  wrappers, backed by `runtime/blang_<m>.c` (a static `.a`). U0 consolidated the
  formerly-triplicated bcc link sites into a **single** `appendRuntimeLibs`
  (`bcc.cpp:363`), directly de-risking touchpoint 3.
- **No float/double codegen test exists today** — `math` returning `double` is
  the first exercise of the float FFI path (`paramToCString`/float returns,
  `CGRuntime.cpp`). Budget for surfacing a latent float codegen bug.

## The 6-touchpoint recipe (design-stdlib.md — the authoring contract)

For each module `<m>` ∈ {`math`, `time`, `random`, `env`}:
1. `runtime/blang_<m>.c` + `.h` — the C impl. **Owned-return contract**: any
   function returning a heap `BlangString`/`BlangArray` returns a +1 reference the
   caller releases (`blang_sys.c:33-41`). `env.get` returns a freshly-created
   `BlangString` (already +1) — no shared global to retain.
2. `CMakeLists.txt`: `add_library(blang_<m> STATIC runtime/blang_<m>.c)` +
   `C_STANDARD 11` + include dir + `target_link_libraries` (math/time also link
   `blang_string`/`blang_array` as needed; math adds the system `m` lib) + a
   `BCC_<M>_LIB` compile-def on `bcc`.
3. **The single link site**: add `findLib(baked<M>, "blang_<m>")` to
   `appendRuntimeLibs` (`bcc.cpp:363`) — **one** place, not three. `math` also
   requires the final link to include `-lm` (system libm); added once in the same
   helper (harmless when unused — the linker drops unreferenced objects).
4. `test_codegen.sh`: a `<M>_LIB` var + append to the `cc` link line (+ `-lm` for
   math).
5. `stdlib/<m>.b` (auto-copied by the CMake glob) + add `"<m>"` to `kKnownOrder`
   (`bcc.cpp:960`) so `import <m>;` resolves — **import-gated**, never added to
   the always-on `{sys,buffer,fs,net}` set (design D1).
6. Two test layers: `runtime/tests/test_<m>.c` + a `<M>_CASES` list + `foreach`
   `add_test` in `CMakeLists.txt` (ctest, ASan-clean in `build-asan`) **and** a
   `test_files/codegen_<m>_*.b` behavioral test (golden where deterministic).

## Module surfaces (the spec settles the exact API)

### `math` (C backing: libm)
`extern fn __blang_math_sqrt(double) -> double;` etc. BLang wrappers in
`stdlib/math.b`:
- `sqrt`, `sin`, `cos`, `tan`, `log`, `log10`, `exp`, `pow(x,y)`, `floor`,
  `ceil`, `fabs` → all `double`→`double` (or `(double,double)->double`).
- `abs_int(int) -> int` (integer abs, no libm).
- Constants as pure-BLang wrappers: `pi() -> double` (3.14159265358979323846),
  `e() -> double` (2.71828182845904523536) — functions, not globals, since module
  globals are not yet a codegen feature (documented choice).
- **Determinism**: `sqrt`/`pow`/etc. of fixed inputs are golden-stable → the
  math codegen test is golden-checked.

### `time` (C backing: `clock_gettime`, `time`)
- `now() -> long` — Unix epoch seconds (`time(NULL)`).
- `now_millis() -> long` — epoch milliseconds (`clock_gettime(CLOCK_REALTIME)`).
- `monotonic_nanos() -> long` — `clock_gettime(CLOCK_MONOTONIC)` (for interval
  timing; not wall-clock).
- **Determinism**: wall-clock values are NOT golden-stable. The `time` codegen
  test asserts **invariants** (e.g. `now() > 1_000_000_000`, a monotonic delta is
  `>= 0`) and prints a fixed `ok` line that IS golden-checked — never a raw
  timestamp. ctest asserts the same invariants on the C functions.

### `random` (C backing: seedable PRNG — xorshift/PCG in C, `getrandom` for seed)
- `seed(long)` — set the PRNG state (deterministic stream from a fixed seed).
- `next_int() -> long`, `int_range(long lo, long hi) -> long` (half-open
  `[lo,hi)`), `float01() -> double` (`[0,1)`).
- **Determinism**: a **fixed seed produces a fixed sequence** — the random
  codegen test seeds with a constant and golden-checks the exact sequence
  (deterministic golden). ctest asserts the same known-answer sequence + that
  `int_range` stays in-bounds. (A dedicated C PRNG, NOT libc `rand()`, so the
  sequence is stable across platforms.)

### `env` (C backing: `getenv`)
- `get(string name) -> Option<string>` — `some(value)` if set, `none` otherwise
  (built-in `Option`). Returns a freshly-created `BlangString` (owned).
- `get_or(string name, string fallback) -> string`.
- `has(string name) -> bool`.
- **Determinism** (per architect F4): the BLang program cannot `setenv` its own
  process (no setenv wrapper in scope), so the codegen test reads variables that
  are deterministic in the harness run environment: it asserts `has("PATH")` is
  true (PATH is always present when `test_codegen.sh` runs the binary) and that
  `get("__BLANG_DEFINITELY_UNSET_XYZ__")` is `none` and `get_or(..., "default")`
  falls back — all golden-stable. The ctest layer covers `getenv` hit/miss
  hermetically with `setenv`/`unsetenv` in the C test.

## Threads through U0's single path (architect coherence check)
- Each module's `.a` is added **once** in `appendRuntimeLibs` (the U0 single link
  site) — no re-duplication across the -c/-S/link paths.
- `import <m>;` gating is **one** `kKnownOrder` entry per module; the `.b` is
  combined only when imported (never in the always-on set).
- `-lm` for math is added once, in the same link helper.
- No qcc flag changes; no interaction with U1/U2/U3 flags.

## Requirements traceability

| REQ-005 clause | Covered by |
|----------------|-----------|
| `math` (libm) | math.b + blang_math.c + touchpoints |
| `time`/`date` | time.b + blang_time.c |
| `random` | random.b + blang_random.c (seedable, deterministic) |
| `env` | env.b + blang_env.c (Option<string>) |
| 6-touchpoint authoring recipe each | §recipe applied per module |

## Test plan / done condition (contributes to epic done-conditions #1, #5, #6)

1. **Each module usable via `bcc import`** with ≥ 1 behavioral `codegen_*.b`:
   - `codegen_math.b` (golden: sqrt/pow/floor/etc. of fixed inputs) — **also the
     first float/double codegen test**.
   - `codegen_random.b` (golden: fixed-seed sequence — deterministic).
   - `codegen_time.b` (golden: an `ok` line from invariant asserts, no raw
     timestamp).
   - `codegen_env.b` (golden: `has("PATH")` true + unset var `none`).
2. **ctest unit tests** per C module: `test_math`/`test_time`/`test_random`/
   `test_env` with `<M>_CASES`, registered via `foreach add_test`; ASan-clean in
   `build-asan`. ctest total grows by the new case count (was 54).
3. **Correctness preserved** (done-cond #1): `./run_tests.sh` (LLVM +
   parse-only), `./test_codegen.sh` at `-O0`, `OPT_LEVEL=2`, and `DEBUG_INFO=1`,
   `ctest --test-dir build`, and `./test_codegen.sh --leak-check` all green.
   The new codegen tests are leak-clean (owned-return contract honored).
4. **Count toward +25** (done-cond #6): U4 adds ≥ 4 `codegen_*.b` (math, random,
   time, env), moving the count from 109 toward the ≥ 132 target.
5. **Import-gated** (design D1, per architect F1): a program that does **not**
   `import math;` has **no ambient BLang linkage** of the new `.b` (kKnownOrder
   gates the combine) and **no libm dependency when unused** (the `.a`/`-lm` are
   offered to the linker but dropped when unreferenced). It builds and runs
   unchanged — verified by the full suite (most tests import nothing) staying
   green + leak-clean. (Not asserted as bit-for-bit byte-identical, since the
   link line now always offers the `.a`/`-lm`.)

## Risks
- **Float codegen untested today** — `math` returning `double` may surface a
  latent bug in the float FFI/return path. Mitigation: `codegen_math.b` is the
  detector; a found bug is fixed in-unit (the functional-hardening "new tests
  surface bugs" pattern), not deferred.
- **Owned-return refcount** — `env.get` returns a new `BlangString`; a wrapper
  that drops it leaks. Mitigation: `--leak-check` on `codegen_env.b` + the
  ASan ctest.
- **Non-determinism** (time/random) — golden drift if a raw value is printed.
  Mitigation: time prints only invariant-`ok`; random uses a fixed seed with a C
  PRNG (not libc `rand()`), so the sequence is platform-stable.
- **`-lm` linkage** — math needs libm; added once in `appendRuntimeLibs` and the
  test link line. If a math symbol is unresolved at link, the math codegen test
  goes red (the detector).
