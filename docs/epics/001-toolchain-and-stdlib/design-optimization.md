# Design: Optimization (`-O`, `--release`, cross-compile) — Unit U2

**Epic**: [overview.md](overview.md) · **Requirement**: REQ-003

## Current state (recon, file:line)

- **No optimization anywhere.** No `-O0..3`, no `opt`, no `PassManager`/
  `PassBuilder` (grep across `bcc.cpp`/`qcc.cpp`/`CMakeLists.txt`). `bcc` goes
  `qcc → .ll → llc -filetype=obj --relocation-model=pic → .o → cc`. `llc` is
  invoked with **no `-O`** (`bcc.cpp:1478` + 3 duplicate blocks). qcc emits naive
  IR and runs **no** module passes before `print()` (`CodeGen.cpp:335`).
- qcc **cannot** create a `TargetMachine` today — it links only LLVM `support
  core irreader passes` (`CMakeLists.txt:26-31`); note `passes` **is** already
  linked, so an in-process pass pipeline is buildable without new LLVM libs.
- **Triple is host-baked**: `-mtriple=<BCC_HOST_ARCH>-{apple-darwin|unknown-
  linux-gnu}` chosen by `#ifdef PLATFORM_*` (`bcc.cpp:1496` + 3 dups);
  `BCC_HOST_ARCH` from `CMAKE_SYSTEM_PROCESSOR`. No `--target`, no `--release`.

## Design

**A. Optimization pipeline.** Two complementary layers:
1. **In-process IR passes in qcc** — the most aligned option, and linkable today
   (`passes` component present). Run a new-`PassBuilder`/`ModulePassManager`
   `buildPerModuleDefaultPipeline(OptLevel)` on `mModule` in `CodeGen` **before**
   `print()` (`CodeGen.cpp:335`). This gets inlining/GVN/SROA/DCE on the naive
   IR — the bulk of the win.
2. **`llc -O<n>`** — push the level into all (post-U0: the single) `llc`
   invocation for backend codegen opt.
`-O` is threaded bcc→qcc via a new qcc flag (bcc already drives qcc by CLI).
Map `-O0/1/2/3/s/z` to the corresponding `OptimizationLevel`.

**B. `--release`.** = `-O2` + (decision) strip debug + **elide `assert`/contract
codegen** (`CGStatements.cpp:794-830, 851-869`). Settle whether `--release`
drops asserts (production semantics) or keeps them; recommend a documented,
explicit choice (default: keep asserts unless `-DNDEBUG`-style flag, to avoid
silent behavior change). `--release` implies `-O2` unless `-O` given.

**C. Cross-compile `--target <triple>`.** Promote the compile-time triple
(`bcc.cpp:1496`) to a runtime flag feeding the `-mtriple` on `llc`. Ships as an
**object-emission smoke test** (`bcc --target <non-host> -c` produces a `.o`),
NOT a fully-linked foreign binary — full cross-linking needs per-target runtime
`.a`s, which is out of scope (Non-goal).

## Key decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | In-process PassBuilder in qcc (IR) + `llc -O` (backend) | Best fit; IR passes are the bulk of the win and need no new LLVM libs |
| D2 | Correctness is the hard gate; delta is recorded, not thresholded | User decision — perf thresholds are brittle/hardware-dependent |
| D3 | `--release` keeps asserts by default; dropping is explicit | Avoid a silent behavior change; contracts are a safety feature |
| D4 | `--target` = object-emission smoke only | Full cross-link needs per-target runtimes (Non-goal) |
| D5 | Validate ARC across opt | BLang emits retain/release/lock; opt must not reorder/elide them wrongly — the suite at `-O2` is the guard |

## Area done-gate (contributes to epic done-conditions #1, #3)
- `cmake`/`bcc` build the whole codegen suite at `-O2`; `./test_codegen.sh`
  (built `-O2`) is green — **correctness under opt is the hard gate**.
- `opt-delta.md` records `-O2` vs `-O0` binary-size (and, if a benchmark exists,
  runtime) delta — informational.
- `--release` builds a sample; `bcc --target <non-host-triple> -c hello.b`
  emits a `.o` (verified by `file`/`llvm-objdump`).

## Risks
- **ARC × opt** — the reference-counting calls must survive; the `-O2` suite run
  is the safety net. Any future ARC-elision pass is opt-order-sensitive (flagged
  for a later epic, not here).
- **`-O` × `-g`** — see `design-debug-info.md`; the two units coordinate on the
  matrix stance (soft conflict, resolved there, re-verified in U6).
