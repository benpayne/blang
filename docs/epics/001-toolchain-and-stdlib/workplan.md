# Workplan: 001-toolchain-and-stdlib

**Epic**: [overview.md](overview.md) · **Archetype**: evolve

Each unit is one implementing hire's assignment through the **three-role**
lifecycle: the implementer runs the speckit ceremony to produce the unit's spec,
the **architect** performs the Phase-2 spec audit (cross-area coherence +
design-doc conformance), the implementer builds, the **independent code
reviewer** performs the Phase-4 code audit and merges. Branch
`epic/001-toolchain-and-stdlib/uN-<slug>`; every unit boundary keeps
`./run_tests.sh` and `./test_codegen.sh` green.

## Unit map

```text
U0 (pipeline foundation) ─▶ U1 (diagnostics) ──┐
                         ├─▶ U2 (optimization) ─┤
                         ├─▶ U3 (debug info) ───┼─▶ U6 (integration + CI + close-out)
                         ├─▶ U4 (native stdlib) ┤
                         └─▶ U5 (collections/CLI stdlib) ┘
```

U0 is the shared foundation (consolidates the duplicated bcc invocation/link
blocks + the flag-threading path). U1–U5 build on it; they are largely independent (the manager sequences
them — a single implementer runs them serially, PM decision), with one soft conflict (U2 × U3 on the `-g`×`-O` matrix, resolved
in `design-debug-info.md` and re-verified in U6). U6 is the capstone.

## Units

### U0 — Pipeline foundation (behavior-preserving)
- **Covers**: REQ-001
- **Preconditions**: none
- **Work**: consolidate the 4 near-duplicated `llc`/qcc-invocation + runtime-link
  blocks in `bcc.cpp` (`~328, ~1017, ~1139, ~1478`) into a single helper, and
  factor the flag-threading path (qcc arg loop `qcc.cpp:507`, bcc option struct
  `bcc.cpp:43`) so `--json`/`-O`/`-g` and new stdlib link-libs each add in **one**
  place, not three. No user-visible behavior change.
- **Done condition**: all suites green **unchanged** (byte-identical `.ll`/link
  behavior for existing tests); the duplicated blocks are gone (a grep shows one
  llc-invocation helper); U1–U5 can add a flag/lib in a single site.
- **Audit**: per constitution (architect spec audit + code review).
- **Speckit**: `pipeline-foundation`

### U1 — Diagnostics: multi-error, warnings, `--json`
- **Covers**: REQ-002 · **Depends on**: U0
- **Work**: make `DiagnosticEngine` a **collector** (buffer diagnostics, emit at
  `finish()`); add parser **panic-mode recovery** (report-then-resync to
  `;`/`}`/top-level `fn`) so one compile reports all syntax errors; fix the
  per-file early-return (`qcc.cpp:887`) so semantic errors accumulate across
  files; add a `Warning` severity + `-Werror`; add `--json` (schema:
  severity/file/line/col/message/code, covering both syntax and semantic
  diagnostics). See `design-diagnostics.md`.
- **Done condition**: overview done-condition #2 (≥3-error fixture → ≥3 located
  diagnostics; `--json` schema-valid; `warning:` emitted; `-Werror` non-zero);
  suites green.
- **Speckit**: `diagnostics`

### U2 — Optimization: pipeline, `--release`, cross-compile
- **Covers**: REQ-003 · **Depends on**: U0
- **Work**: add `-O0..3/s/z` — an in-process new-PassManager pipeline on
  `mModule` in `CodeGen` before `print()` (`CodeGen.cpp:335`) plus `llc -O` for
  the backend; thread `-O` bcc→qcc; add `--release` (opt + optionally strip
  asserts/contracts — settle in the design doc); add `--target <triple>`
  promoting the host-baked triple (`bcc.cpp:1496`) to a runtime flag. Validate
  ARC retain/release survive optimization. See `design-optimization.md`.
- **Done condition**: overview done-conditions #1 (suite green at `-O2`) and #3
  (delta recorded in `opt-delta.md`; `--release` builds; `--target` emits an
  object); suites green.
- **Speckit**: `optimization`

### U3 — Debug info (DWARF)
- **Covers**: REQ-004 · **Depends on**: U0
- **Work**: `DIBuilder` + `createCompileUnit` + `Debug Info Version` module flag
  in `CodeGen::generate`; per-function `DISubprogram` at `genFunction`
  (`CodeGen.cpp:357`); per-statement `SetCurrentDebugLocation` from each node's
  `getLocation()`; param/local `dbg.declare`; **synthetic `DebugLoc` on
  ARC-emitted instructions** (else the verifier rejects under `-O`); `-g` flag
  threaded to the `llc` step; each `--combine` source gets its own file entry.
  Implement the `-g`×`-O` stance from the design doc. See `design-debug-info.md`.
- **Done condition**: overview done-condition #4 (`llvm-dwarfdump-18` shows line
  tables naming the `.b` + `DISubprogram`; a `gdb` breakpoint smoke test hits;
  the `-g`×`-O` stance verified — `-g -O2` verifies clean); suites green.
- **Speckit**: `debug-info`

### U4 — Native stdlib modules (math, time, random, env)
- **Covers**: REQ-005 · **Depends on**: U0
- **Work**: author `math` (libm: sqrt/sin/cos/pow/log + PI/E consts), `time`/date
  (clock/monotonic/duration/format), `random` (seeded PRNG, ranges, shuffle),
  and `env` (getenv), each via the 6-touchpoint recipe (`design-stdlib.md`):
  `stdlib/<m>.b` + `runtime/blang_<m>.c/.h` + CMake lib/def/dep (+`-lm` for math)
  + the U0-consolidated link site + `kKnownOrder` entry + `runtime/tests/test_<m>.c`
  (ctest) + `test_files/codegen_<m>_*.b` (behavioral). Fills the current
  float/double codegen-test gap.
- **Done condition**: each module usable via `bcc` `import`, with a passing
  codegen test (golden where deterministic) and ctest unit tests (ASan-clean);
  suites green.
- **Speckit**: `stdlib-native`

### U5 — Collections & CLI stdlib (hashmap/set, sort, flags)
- **Covers**: REQ-006 · **Depends on**: U0
- **Work**: a real **hashed** `Map<K,V>` + `Set<K>` (open-addressing; a small
  `__blang_hash` C helper or hash via existing string bytes) that **replaces**
  the O(n) `collections.Map` (fix its missing-return bug too); a generic `sort`
  (comparator; C `qsort`-backed helper is the lower-risk fallback if closure
  generics hit codegen limits — decide in the design doc); CLI flag/arg parsing
  built on `sys.args()` + string methods. Behavioral tests + goldens.
- **Done condition**: hashed `Map`/`Set` usable via `bcc` with a test showing
  hashed (not O(n)) behavior; `sort` sorts a fixture; flag parser parses a
  sample `--flag=value`/positional set; suites green.
- **Speckit**: `stdlib-collections`

### U6 — Integration, cross-area verification, CI, close-out
- **Covers**: REQ-007 · **Depends on**: U1, U2, U3, U4, U5
- **Work**: cross-area verification — build+run the codegen suite at `-O2` AND
  `-g` (and `-g -O2` per the stance); confirm `--json` covers both syntax and
  semantic diagnostics; wire CI legs (opt build, `-g` build, dwarfdump smoke,
  new ctest modules, new codegen tests); confirm every area's speckit artifacts
  passed the architect audit; update `CLAUDE.md` (counts, feature lists,
  known-issues closed) and `docs/language_design.md`.
- **Done condition**: the overview done condition holds, checked by
  `evaluation.md`'s acceptance block; CI green on the final commit.
- **Speckit**: `integration-closeout`

## Sequencing notes for the manager
- **U0 first, always** — U1–U5 all extend the flag-threading/link path it
  consolidates; starting them before U0 lands means re-doing the wiring 3×.
- U1–U5 are independent after U0 but run serially (single implementer). **Soft conflict U2 × U3** (the `-g`×`-O` matrix):
  the stance is fixed in `design-debug-info.md`; whichever merges second rebases
  and re-runs the matrix. Recorded in `manifest.yaml soft_conflicts`.
- The **architect** reviews every unit's spec before implementation — it is the
  cross-area coherence guard (e.g. that U1's `--json`, U2's `-O`, U3's `-g` all
  thread through U0's single path consistently). A spec that bypasses U0's path
  is an architect-blocking finding.
- If a unit's "full depth" turns out to need splitting (e.g. U2 cross-compile,
  or U3 variable-level debug info), that is an Open Question to the manager, not
  a silent scope cut.
