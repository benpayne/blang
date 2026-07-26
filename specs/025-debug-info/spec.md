# Spec: Debug info — DWARF via `-g`

**Epic**: 001-toolchain-and-stdlib · **Unit**: U3 · **Branch**: `epic/001-toolchain-and-stdlib/u3-debug-info`
**Covers**: REQ-004 · **Speckit**: `debug-info` · **Status**: Merged (code review APPROVE; gates green at -O0/-O2/-g).
**Reviewed-by: code-reviewer** (Rex; audit self-completed by manager after runtime
interruption, disposition recorded). Verdict APPROVE; **0 blocking**. Verified:
DIBuilder created once in --combine (guarded); `finalize()` idempotent before
`verify()`/`print()`; "Debug Info Version" module flag present so DWARF survives
the text-.ll→llc boundary; non-`-g` build byte-identical (all paths gated on
`mDebugInfo`); `-g` rides U0's single flag/link path and forces `-O0` in the one
`effectiveOpt` block; `-g -O2` verifies (the "inlinable call needs !dbg" rule is
**caller-gated** + a func-entry default DebugLoc covers every module-function
instruction — confirmed empirically on the full -g suite + a method/lambda/async
stress test). Smoke script has teeth; gdb skip-not-fail; golden deterministic.
**Documented residual risk (not blocking):** struct methods, lambdas, and async
bodies get no `DISubprogram`/line entries (spec §B/§D documented-partial), so you
cannot breakpoint *inside* them via DWARF; the hard gate (subprogram + line table
+ breakpoint) holds for free functions. `DW_LANG_C` is deliberate (no registered
BLang DWARF code). Suite green at -O0/-O2/-g (109 each) + ctest 54 + -g
--leak-check 0.
**Reviewed-by: architect** (Vera). Verdict PASS-WITH-FINDINGS; **0 blocking**.
Verified: §A–F map 1:1 to `design-debug-info.md`; adopts Stance S-A (`-g`→`-O0`)
+ verifier-clean-under-`-O` (§F), composing with U2's merged `effectiveOpt` block;
`-g` rides U0's single `emitObject`/qcc-arg-loop seam (no re-dup, no bypass);
`Debug Info Version` module flag + `finalize()`-before-`print()` mandatory; clean
documented-partial boundary for locals. Four MINOR gate-tightening findings
folded into the test plan: (1) U3's own test asserts subprogram **count** ≥
function count, not just presence; (2) `DW_LANG_C` is deliberate (no registered
BLang DWARF language code — gdb/dwarfdump will report C); (3) the committed gate
exercises `demos/01_fibonacci.b` (the epic-gate file), the new fixture is
supplementary; (4) a `--combine` multi-`DIFile` check is added where cheap.
**Depends on**: U0 (merged @ 00106d6 — `-g` extends U0's `emitObject`/qcc-invocation single path; bcc→qcc rides the Options+arg-loop seam) · U1 (merged @ 82af089, diagnostics) · U2 (merged @ 3602bae — the `-g`×`-O` product stance interacts with U2's `-O` pipeline)

## Problem (recon, file:line @ master 3602bae)

- **Zero DWARF.** A grep for `DIBuilder|DISubprogram|createCompileUnit|DebugLoc|
  SetCurrentDebugLocation|"-g"` across all `*.cpp/*.h` is empty. `bcc`/`qcc` have
  no `-g`; the `llc` step (`emitObject`, `bcc.cpp`) passes no `-g` and no debug
  metadata is produced, so a debugger cannot map machine code to `.b` source.
- **The source-position data already exists.** Every AST node carries an accurate
  `SourceLocation {file, line, col}` (`SourceLocation.h`) with an `isSet()`
  invariant, stamped at parse time and reachable via `getLocation()` on every
  node. DWARF's line/scope data is therefore already threaded into codegen — the
  work is emission plumbing, not new analysis.
- **CodeGen holds `mContext`/`mModule`/`mBuilder`** (`CodeGen.h:427-429`) as
  `unique_ptr`s; `generate()` (`CodeGen.cpp:26`) is the module-setup site,
  `genFunction()` (`CodeGen.cpp:401`) the per-function site, and `print()`
  (`CodeGen.cpp:337`) serializes the textual `.ll` consumed out-of-process by
  `llc`.

## Design (matches `design-debug-info.md` A–F + the `-g`×`-O` stance)

### A. Module setup — `DIBuilder` + compile unit
- Add members: `std::unique_ptr<llvm::DIBuilder> mDIBuilder`, `llvm::DICompileUnit
  *mDICompileUnit`, `bool mDebugInfo` (off by default), and a per-file
  `std::map<std::string, llvm::DIFile*> mDIFileCache` (for `--combine`
  multi-source). A `setDebugInfo(bool)` setter (mirrors `setTestMode`), driven by
  qcc's `-g` arg.
- In `generate()`, when `mDebugInfo`: create the `DIBuilder(*mModule)`; call
  `createCompileUnit(DW_LANG_C, primaryDIFile, "blang", /*isOptimized*/false, "",
  0)`; add module flags `"Debug Info Version" = DEBUG_METADATA_VERSION` and
  `"Dwarf Version" = 4`. **The `Debug Info Version` module flag is mandatory** —
  without it `llc` silently strips all debug metadata across the text-`.ll`
  boundary (the load-bearing serialization risk, design §E/Risks).
- **`--combine` multi-file**: each distinct source path gets its own `DIFile` via
  the cache, so line tables point at the correct `.b`. The compile unit is
  anchored on the primary (last/user) source file.

### B. Per-function `DISubprogram`
- In `genFunction()` (and the method/async/extern-with-body sites), when
  `mDebugInfo`: build a `DISubroutineType` (a minimal signature is acceptable for
  v1 — a null/`{null}` type array is verifier-legal; richer param types are a
  documented nice-to-have, not required), create a `DISubprogram` via
  `createFunction(scope=DIFile, name, linkageName, DIFile, line, subType, line,
  DINode::FlagZero, DISubprogram::SPFlagDefinition)`, and attach it with
  `llvmFunc->setSubprogram(sp)`. Push the subprogram as the current lexical scope
  for the function body; pop/clear at function end so the next function's
  locations resolve against its own scope.
- Extern **declarations without a body** get **no** `DISubprogram` (nothing to map).

### C. Per-statement `DebugLoc`
- At `SetInsertPoint`/statement-emit sites, call `mBuilder->
  SetCurrentDebugLocation(DILocation::get(ctx, line, col, currentScope))` from the
  node's `getLocation()`. Because every node has a location, this is mechanical.
  A helper `applyDebugLoc(Statement*)` (no-op when `!mDebugInfo`) centralizes it
  and is called at statement dispatch, keeping the change surface small and the
  non-`-g` path byte-identical.
- On leaving a function, `SetCurrentDebugLocation({})` is reset so stray builder
  state never leaks a stale scope into the next function.

### D. Params / locals (`dbg.declare`)
- v1 target: **line tables + `DISubprogram` are the hard done-gate** (done-cond
  #4). `createParameterVariable`/`createAutoVariable` + `insertDeclare` for
  params and locals at the alloca sites is **implemented where mechanical** and
  is what makes `gdb` able to print locals; if a specific alloca site is awkward,
  locals may be a documented partial (the gate is breakpoint-hit + line table +
  subprogram, not full local inspection). Every `dbg.declare` inserted **must**
  carry a `DebugLoc` in the subprogram scope (verifier requirement).

### E. `-g` flag threading (through U0's single path)
- **bcc**: add `bool debugInfo` to `Options` (`-g`), forward `-g` to the qcc
  invocation (the same arg-vector U1/U2 extend), and — per the stance below —
  when `-g` is set, force the effective opt level to `-O0` (both layer 1 in qcc
  and layer 2 in `emitObject`/llc). `emitObject` also passes `-g` to `llc` so the
  backend preserves/emits DWARF into the object.
- **qcc**: add `-g` to the arg loop (the U0 flag seam) → `codegen.setDebugInfo(
  true)`. `DIBuilder::finalize()` is called after codegen and **before**
  `print()` (unfinalized debug metadata is invalid and the verifier rejects it).
- No new stdlib link work; `appendRuntimeLibs` untouched.

### F. ARC-emitted instructions carry a synthetic `DebugLoc`
- Retain/release/lock calls (`CGRuntime.cpp`, `CGExpressions.cpp`) have no direct
  source line. Under debug info the LLVM verifier requires **every** instruction
  in a function with a `DISubprogram` to have a non-null `DebugLoc` *when
  optimizations run* ("function with debug info must have non-null debug
  locations"). Mitigation: the per-statement hook (C) keeps the builder's current
  `DebugLoc` set to the enclosing statement while ARC calls are emitted, so ARC
  instructions inherit a valid location. Where ARC is emitted at scope-exit with
  no current statement, a synthetic function-scope location (line 0 / the
  subprogram scope) is set. This makes emission **verifier-clean under `-O`**.

## The `-g` × `-O` stance (the load-bearing decision — Stance S-A)

Per `design-debug-info.md`, **ship S-A: `-g` forces `-O0`.** `bcc -g` overrides
any `-O`/`--release` to `-O0` (best line-table fidelity, simplest, matches how
`-g` is used for interactive debugging). `-g -O2` is **not** a supported product
combination for fidelity — but per the design's recommendation the **emission is
made verifier-clean under `-O`** (the ARC `DebugLoc` work of §F) so that a
manual `qcc -g -O2` module still **passes the LLVM verifier** (no "must have
non-null debug locations" error). The done-gate tests both: S-A breakpoint
fidelity at `-O0`, and that a `-g -O2` module *verifies*.

## Threads through U0's single path (architect coherence check)
- **`-g`**: added once to the qcc arg loop (flag seam) and once to bcc's
  `Options`; bcc forwards it to qcc and into `emitObject` (the single llc site,
  U0/U2). No re-duplication of the llc invocation or the qcc-invocation block.
- **`-g` forcing `-O0`**: computed in bcc's existing `effectiveOpt` block (U2) —
  one place — before qcc and `emitObject` are called. No U0 bypass.
- No new stdlib touchpoints.

## Requirements traceability

| REQ-004 clause | Covered by |
|----------------|-----------|
| `DIBuilder` compile unit | A |
| per-function `DISubprogram` | B |
| per-statement `DebugLoc` | C |
| param/local `dbg.declare` | D |
| `-g` flag | E |
| resolved `-g`×`-O` stance | S-A (§ stance) |
| ARC instrs carry synthetic locations | F |

## Test plan / done condition (contributes to epic done-conditions #1, #4, #6)

1. **DWARF is present and correct** (done-cond #4). A committed `test_files/`
   fixture (e.g. `codegen_debug_hello.b`) built with `./build/bcc -g` yields a
   binary/object whose `llvm-dwarfdump-18` output:
   - contains a `DW_TAG_subprogram` **per function** (grep count ≥ the number of
     `.b` functions);
   - has a **line table** whose file entry names the `.b` source
     (`llvm-dwarfdump-18 --debug-line | grep -q '<name>.b'`).
2. **gdb breakpoint smoke** (done-cond #4). A scripted `gdb -batch` run sets
   `break <file>.b:<line>`, `run`s, and the output contains `Breakpoint 1` being
   hit at that line. Committed as `test_files/debug/gdb_smoke.sh` (or folded into
   a codegen test harness); **skipped if `gdb` is absent** (per `evaluation.md`
   Prerequisites) but present and runnable where gdb exists.
3. **`-g -O2` verifies** (the S-B safety net). `./build/qcc -g -O2 <fixture> -S`
   (or `bcc -g` then a manual `-O2` module) passes `verify()` — no "non-null
   debug locations" verifier error. Proven by a scripted check that the `-g -O2`
   compile exits 0.
4. **`-g` forces `-O0`** (stance S-A). `./build/bcc -g -O2 hello.b`: assert (via
   `-v` output or the emitted object) that the effective level is `-O0`. A
   scripted check confirms the stance.
5. **Correctness preserved** (done-cond #1). Default (no `-g`) build is
   **byte-identical** to pre-U3 (all debug paths gated on `mDebugInfo`).
   `./run_tests.sh` (LLVM + parse-only), `./test_codegen.sh` (`-O0` and
   `OPT_LEVEL=2`), `ctest --test-dir build`, and `./test_codegen.sh --leak-check`
   stay green.
6. **Behavioral `codegen_*.b` tests toward the +25 target** (done-cond #6). At
   least one `codegen_debug_*.b` with a stdout golden that also exercises the
   `-g` build path (the golden proves the `-g` binary still runs correctly; the
   dwarfdump/gdb checks prove the debug info).

## Risks
- **Text-`.ll` boundary strips debug metadata** if the `Debug Info Version`
  module flag is missing — the explicit module-flag add (A) + a dwarfdump check
  in the gate is the detector.
- **Verifier failure under `-O`** from ARC instructions lacking `DebugLoc` — the
  mandatory current-`DebugLoc`/synthetic-location work (F) is the mitigation;
  the `-g -O2` verify check (test 3) is the guard.
- **`--combine` multi-file** — each source needs its own `DIFile` or line tables
  point at the wrong `.b`; the per-file cache (A) mitigates, and the dwarfdump
  file-name check (test 1) is the detector.
- **Unfinalized `DIBuilder`** produces invalid metadata — `finalize()` before
  `print()` (E) is mandatory; the post-codegen `verify()` catches a miss.
- **gdb absence in CI/headless** — the gdb smoke is Prerequisite-gated (skipped
  cleanly), never a hard failure where gdb is unavailable.
