# Design: Stdlib breadth — Units U4 (native) & U5 (collections/CLI)

**Epic**: [overview.md](overview.md) · **Requirements**: REQ-005 (U4), REQ-006 (U5)

## Current state (recon)

Every stdlib module is a **two-layer sandwich**: a `.b` wrapper declaring
`extern fn __blang_*` FFI prototypes + idiomatic BLang structs/functions,
backed by a C `runtime/blang_<name>.c`. Today: `sys`, `buffer`, `fs`, `net`
(also hosts timer), `timer`, `collections` (pure-BLang `Map`, **O(n)**, with a
missing-return bug at `collections.b:35-41`). **Zero** math/time/random/env/
sort/regex exist. Primitives to build on are solid: `Array<T>`
(push/pop/insert/remove/get/set/sort-able), `string` (incl. `to_int`/`to_float`/
`index_of`/`starts_with`), and first-class `float`/`double` (with **no float
codegen tests today** — a gap U4 fills).

## The 6-touchpoint authoring recipe (load-bearing — the design's core contract)

Adding a C-backed module `<m>` requires editing exactly these places:
1. `runtime/blang_<m>.c` + `.h` — the C impl (honor the **owned-return
   contract**: retain before returning a shared/global array/string, per
   `blang_sys.c:33-41`).
2. `CMakeLists.txt`: `add_library(blang_<m> STATIC ...)` + `C_STANDARD 11` +
   include dir + `target_link_libraries` (add `-lm` for math); a `BCC_<M>_LIB`
   compile-def (~`:266`).
3. The **U0-consolidated link site** in `bcc.cpp` (today triplicated at ~402,
   ~1244, ~1571 — U0 makes this **one** place) + `findBuildLib`.
4. `test_codegen.sh`: `<M>_LIB` var + append to the `cc` link line.
5. `stdlib/<m>.b` (auto-copied by the CMake glob) + add `"<m>"` to `kKnownOrder`
   (`bcc.cpp:831`) so `import <m>;` resolves — **import-gated, never** added to
   the always-on `{sys,buffer,fs,net}` set.
6. Two test layers: `runtime/tests/test_<m>.c` + `<M>_CASES` + `foreach` in
   `CMakeLists.txt` (ctest, runs under ASan in `build-asan`) **and** a
   `test_files/codegen_<m>_*.b` behavioral test (golden where deterministic).

U0's consolidation of the triplicated `bcc.cpp` link sites directly de-risks
step 3 for every module — that is why U4/U5 depend on U0.

## U4 — Native modules (C-runtime backed)

| Module | C backing | Surface (illustrative — spec settles exact API) |
|--------|-----------|--------------------------------------------------|
| `math` | libm (`-lm`) | `sqrt/sin/cos/tan/pow/log/exp/floor/ceil/abs` returning `double`; consts `PI`, `E` (pure-BLang consts in `math.b`) |
| `time` | `clock_gettime`/`localtime`/`strftime` | `now()` (unix), `monotonic()`, `Duration`, `format(fmt)` |
| `random` | `getrandom`/PRNG state | `seed(n)`, `int_range(lo,hi)`, `float01()`, `shuffle(Array<T>)` |
| `env` | `getenv`/`environ` (pattern already in `blang_db.c`) | `get(name) -> Option<string>`, `get_or(name, default)` |

`math` returning `double` is well-supported (`paramToCString` handles floats,
`CGRuntime.cpp:877`); U4 also adds the first float/double codegen tests.

## U5 — Collections & CLI (mostly BLang)

- **Hashed `Map<K,V>` + `Set<K>`** — replace the O(n) `collections.Map`.
  Open-addressing over bucket `Array`s; needs a hash primitive — a small
  `__blang_hash(cstring) -> long` C helper (or hash existing string bytes) is the
  low-risk choice. Fix the missing-return bug. A test must demonstrate hashed
  behavior (e.g. large N stays fast / distinct buckets), not just correctness.
- **`sort`** — generic, comparator-based. **Decision**: a comparator closure in
  pure BLang may hit codegen limits (generics + closures); the lower-risk
  fallback is a `__blang_sort` C helper (qsort-backed) taking a comparator fn
  pointer, or a monomorphized BLang sort for the common `int`/`string`/`double`
  cases. The spec picks; the architect audits the choice against codegen reality.
- **CLI flag/arg parsing** — pure BLang on `sys.args()` + string methods
  (`starts_with("--")`, `index_of("=")`, `to_int`). Parse `--flag`, `--flag=val`,
  `-x`, and positionals into a small `Args`/`Flags` struct. No new C.

## Key decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | All new modules **import-gated** (`kKnownOrder`) | Keep the always-on set minimal; avoid ambient bloat |
| D2 | Every C-backed module honors the owned-return refcount contract | The known gotcha (`blang_sys.c:33-41`); ASan ctest catches violations |
| D3 | Hashed Map replaces O(n) `collections.Map` (not a parallel new type) | One canonical Map; fixes the latent missing-return bug |
| D4 | `sort` implementation (C-helper vs BLang) settled per codegen reality in the spec | Closure-generics may not be codegen-ready; don't assume |
| D5 | Two test layers per C module (ctest + codegen) | ctest catches C/ARC bugs under ASan; codegen proves the FFI boundary via `bcc` |

## Area done-gate (contributes to epic done-condition #5)
- Each of `math`/`time`/`random`/`env`/`sort`/flags/hashed-`Map`/`Set` is usable
  via `bcc import` with ≥ 1 passing behavioral `codegen_*.b` test.
- C-backed modules have passing `ctest` unit tests, ASan-clean.
- A test shows the new `Map` is hashed (distinct-bucket / large-N) and the
  missing-return bug is gone.

## Risks
- **Triplicated link wiring** (step 3) — U0 relieves it; without U0 a module
  silently fails to link in one of three paths.
- **`sort`/closure-generics** may exceed current codegen — hence the C-helper
  fallback and an explicit spec decision.
- **float codegen is untested today** — U4's math tests are the first exercise;
  budget for finding a float codegen bug (the functional-hardening pattern).
