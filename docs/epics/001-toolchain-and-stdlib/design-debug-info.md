# Design: Debug info (DWARF, `-g`) — Unit U3

**Epic**: [overview.md](overview.md) · **Requirement**: REQ-004

## Current state (recon, file:line)

- **Zero DWARF** confirmed (grep for `DIBuilder|DISubprogram|createCompileUnit|
  DebugLoc|Dwarf|"-g"` across all `*.cpp/*.h` is empty). No `-g` in bcc/qcc;
  `SetCurrentDebugLocation` never called.
- **The hard part is already done**: every AST node carries an accurate
  `SourceLocation` (file + 1-based line/col), `SourceLocation.h:11-22`, with an
  `isSet()` invariant. So the source-position data DWARF needs is already
  threaded into codegen (`getLocation()` on every node).

## Design

**A. Module setup** (`CodeGen::generate`, `CodeGen.cpp:24`): create a
`DIBuilder`, `createCompileUnit` (one per source file; each `--combine` source
gets its own `DIFile`, `bcc.cpp:1139` path), and `addModuleFlag("Debug Info
Version", DEBUG_METADATA_VERSION)` + `"Dwarf Version"`. `DIBuilder::finalize()`
before `print()` (`CodeGen.cpp:335`).

**B. Per-function** (`genFunction`, `CodeGen.cpp:357`, and the extern/decl/async
sites `105,159,432`): attach a `DISubprogram` to each `llvm::Function` with its
`DIFile`/line/`DISubroutineType`.

**C. Per-statement/line**: `mBuilder->SetCurrentDebugLocation(DILocation::get(
ctx, line, col, scope))` from each node's `getLocation()` at the `SetInsertPoint`
sites (`CGStatements.cpp:636-879` for if/while/for; `CodeGen.cpp:200,439,496,
523,619`). Because every node has a location, this is mechanical per node type.

**D. Params/locals**: `createParameterVariable` + `dbg.declare` at the
alloca/malloc sites (`CodeGen.cpp:502,557`).

**E. `-g` flag**: bcc option → qcc arg → the (U0-consolidated) `llc` step must
pass `-g` / preserve debug metadata. **Text-`.ll` round-trip risk**: because qcc
emits textual IR consumed out-of-process by `llc`, the debug metadata and the
`Debug Info Version` module flag must serialize into the `.ll` and be re-read by
`llc` — verify `llc` doesn't silently strip it (a known failure mode if the
module flag is absent).

**F. ARC-emitted instructions** (retain/release/lock in `CGExpressions.cpp`) have
no direct source line; they **must** carry a synthetic `DebugLoc` (function
scope / line 0), or the LLVM verifier rejects the module **under `-O`** ("function
with debug info must have non-null debug locations"). This makes the per-node
hook (C) effectively total, not best-effort, once `-O` is in play.

## The `-g` × `-O` stance (the load-bearing decision)

Debug info + optimization is the classic hazard: opt degrades line-table
accuracy and turns locals into `<optimized out>`. Two viable stances — **the
epic must pick one and test it**:

| Stance | What it means | Cost |
|--------|---------------|------|
| **S-A: `-g` forces `-O0`** (recommended default) | `bcc -g` ignores/overrides `-O`; best fidelity, simplest | `-g -O2` unsupported (documented) |
| **S-B: opt-safe debug info** | Every emitted instruction (incl. ARC) carries a `DebugLoc`; the module verifies under `-O2 -g`; locals may still be `<optimized out>` | The per-node + ARC `DebugLoc` hook becomes mandatory and total |

**Recommendation**: ship **S-A** as the product stance (fidelity-first, matches
how `-g` alone is used for debugging), AND make the emission **verifier-clean
under `-O`** (do the ARC `DebugLoc` work of S-B) so a future `-g -O2` is a small
step and the module never fails the verifier. The done-gate tests S-A's
breakpoint fidelity and that `-O2 -g` at least *verifies* (no verifier error).

## Area done-gate (contributes to epic done-condition #4)
- `bcc -g hello.b -o hello`; `llvm-dwarfdump hello` (or the `.o`) shows a
  `DW_TAG_subprogram` per function and a line table mapping to `hello.b`
  (grep-checkable).
- A scripted `gdb -batch` / `lldb` run sets `break hello.b:<line>`, runs, and
  confirms the breakpoint is hit at that line.
- `bcc -g -O2 hello.b` produces a module that passes the LLVM verifier (no
  "must have non-null debug locations" error) — proving the ARC `DebugLoc` work.

## Risks
- **Text-`.ll` boundary** strips debug metadata if the module flag is missing —
  explicit verify step.
- **Verifier failure under `-O`** from ARC instructions lacking `DebugLoc` — the
  mandatory synthetic-location work (F) is the mitigation.
- **`--combine` multi-file** — each source needs its own `DIFile` or line tables
  point at the wrong `.b`.
