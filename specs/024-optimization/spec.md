# Spec: Optimization — `-O`, `--release`, cross-compile

**Epic**: 001-toolchain-and-stdlib · **Unit**: U2 · **Branch**: `epic/001-toolchain-and-stdlib/u2-optimization`
**Covers**: REQ-003 · **Speckit**: `optimization` · **Status**: Merged (code review PASS; gates green at -O0 and -O2)
**Reviewed-by: code-reviewer** (Rex; audit self-completed by manager after runtime
interruption, disposition recorded). Verdict APPROVE; 0 blocking. Verified:
`CodeGen::optimize` analysis-manager lifetimes correct + post-opt `verify()` on
both qcc paths; `-O`/`--target` extend U0's single `emitObject` (no re-dup, no
bypass); opt is opt-in (byte-identical when unset); `--release`→-O2 (explicit -O
wins); `test_codegen.sh` threads `OPT_LEVEL` into qcc + llc. One MINOR
(non-blocking, spec-permitted): `--target`+link not explicitly rejected — only
`-c` object emission is in scope (done-condition #3).
**Depends on**: U0 (merged @ 00106d6 — `-O`/`--target` extend U0's `emitObject`; `-O` bcc→qcc rides the Options+arg-loop seam) · U1 (merged @ 82af089)
**Reviewed-by: architect** (Vera; audit self-completed by manager after runtime
interruption, disposition recorded). Verdict PASS-WITH-FINDINGS; 0 blocking.
Verified: recon accurate (qcc + llc `test_codegen.sh` call sites; `passes`
component linked; `emitObject` is the single llc site); conforms to
`design-optimization.md` A/B/C + D1–D5; `-O`/`--target` extend `emitObject` in one
place (no re-duplication, no U0 bypass); U2 keeps emission verifier-clean under
`-O` and does not contradict U3's `-g→-O0` stance (soft conflict re-verified in
U6); Principle-II carve-out correctly invoked (the `OPT_LEVEL=2` suite discharges
it, no `codegen_*.b` mandated); correctness is the hard gate, delta informational.

## Problem (recon, file:line @ master 82af089)

- **No optimization anywhere.** No `-O0..3`, no `opt`, no `PassBuilder`/
  `PassManager` in `bcc.cpp`/`qcc.cpp`/`CodeGen.cpp`. `CodeGen::print`
  (`CodeGen.cpp:335`) emits `mModule` with **no** passes run. The (U0-consolidated)
  `llc` invocation `emitObject` (`bcc.cpp`) runs `llc -filetype=obj
  --relocation-model=pic` with **no `-O`**. Output is `-O0`-equivalent.
- **The `passes` LLVM component is already linked** (`CMakeLists.txt:26-31`), so an
  in-process new-PassManager pipeline is buildable **without new LLVM libs**.
- **Triple is host-baked** in `emitObject` via `#if defined(PLATFORM_*) …
  -mtriple=<BCC_HOST_ARCH>-…` (U0 made this the single site). No `--target`, no
  `--release`.
- `test_codegen.sh` compiles each test as `qcc … <file>` (`:270`) then `llc-18
  -filetype=obj -relocation-model=pic <ir> -o <obj>` (`:304`) — **no `-O`**; the
  `-O2` suite gate needs a level threaded into both steps.

## Design (matches `design-optimization.md` A/B/C)

### A. Optimization pipeline — two complementary layers
1. **In-process IR passes in qcc (the bulk of the win).** Add
   `CodeGen::optimize( llvm::OptimizationLevel level )`: build a
   `PassBuilder`, register the analysis managers, and run
   `buildPerModuleDefaultPipeline(level)` (or `buildO0DefaultPipeline` for O0) as
   a `ModulePassManager` over `mModule`, **after** `verify()` and **before**
   `print()` (`qcc.cpp` codegen block ~`:1104`/`CodeGen.cpp:335`). No
   `TargetMachine` is required for these IR-level passes (kept target-independent
   so qcc needs no per-target backend libs; the backend/target codegen opt is
   llc's job). Requires `#include "llvm/Passes/PassBuilder.h"` (in the `passes`
   component already linked).
2. **`llc -O<n>` (backend).** Extend U0's `emitObject` — the **single** llc site —
   to append `-O<n>` when a level is set.
- **Level mapping**: `-O0→O0`, `-O1→O1`, `-O2→O2`, `-O3→O3`, `-Os→Os`, `-Oz→Oz`.
  A qcc `-O<lvl>` flag (parsed in the arg loop, the U0 flag seam) drives layer 1;
  bcc forwards `-O<lvl>` to **both** qcc (layer 1) and `emitObject`/llc (layer 2).

### B. `--release` (design D3)
- `--release` implies `-O2` (unless an explicit `-O` is also given, which wins).
- **Asserts/contracts are KEPT by default** (design D3 — contracts are a safety
  feature; dropping them silently is a footgun). `--release` does **not** elide
  `assert`/`requires`/`ensures` codegen in this unit. (A future explicit
  `-DNDEBUG`-style flag could; out of scope here, documented as a non-goal.)
- `--release` is a bcc `Options` bool; it sets the effective opt level to O2 when
  no `-O` was passed.

### C. Cross-compile `--target <triple>` (design D4)
- Promote `emitObject`'s host-baked `-mtriple` to a **runtime parameter**. Add a
  `targetTriple` argument to `emitObject` (default empty = the existing host
  triple `#ifdef` behavior, byte-identical to today when unset). When
  `--target <triple>` is given, `emitObject` passes `-mtriple=<triple>` instead.
- Ships as an **object-emission smoke test** only: `bcc --target <non-host> -c
  foo.b -o x.o` produces a `.o` for that target (verified by `file`). **Not** a
  fully-linked foreign binary (per-target runtime `.a`s are a Non-goal); `--target`
  with linking is rejected or documented as unsupported. The in-process IR passes
  are target-independent, so they run unchanged; llc does the target codegen.
- Preflight: the aarch64 gate is skipped if `llc-18 --version` lacks aarch64
  (per `evaluation.md` Prerequisites).

## Threads through U0's single path (architect coherence check)
- **`-O<n>`**: added once to the qcc arg loop (layer-1 seam) and once to bcc's
  `Options`; bcc forwards it to qcc and into `emitObject` (layer-2, the single
  llc site).
- **`--target`**: extends `emitObject`'s triple in **one** place (the U0 helper);
  bcc `Options.targetTriple` feeds it. No re-duplication of the llc invocation.
- **`--release`**: a bcc `Options` bool mapping to `-O2`; no new llc/qcc site.
- No new stdlib link work; `appendRuntimeLibs` untouched.

## Requirements traceability

| REQ-003 clause | Covered by |
|----------------|-----------|
| `-O0..3/s/z` in-process pipeline + `llc -O` | A (CodeGen::optimize + emitObject -O) |
| `--release` semantics | B |
| `--target` cross-compilation (object smoke) | C (emitObject triple param) |
| correctness preserved, delta recorded | done-conditions below |

## Test plan / done condition (contributes to epic done-conditions #1, #3)

**Constitution II carve-out (U2)**: `-O`/`--release`/`--target` are toolchain
*flags*, not language features — the Principle-II obligation is discharged by
running the **existing suite built under the flag** (`-O2`) plus the smoke
checks, **not** by a new `codegen_*.b` (per `evaluation.md` lines 55-63). An
architect must not raise a missing-`codegen_*.b` finding against U2.

1. **Correctness at `-O2` is the hard gate.** Add `OPT_LEVEL` support to
   `test_codegen.sh`: when set, append `-O$OPT_LEVEL` to the qcc call (`:270`)
   and `-O$OPT_LEVEL` to the llc call (`:304`). `OPT_LEVEL=2 ./test_codegen.sh`
   passes **107/107** with all goldens matching (opt must not change observable
   behavior). `OPT_LEVEL=2 ./test_codegen.sh --leak-check` → **0 leaks** (ARC
   retain/release/lock survive optimization — the load-bearing ARC×opt guard).
2. **Baseline suites still green at `-O0`**: `./run_tests.sh` (LLVM + parse-only),
   `./test_codegen.sh`, `ctest`, `--leak-check` all unchanged (opt is opt-in;
   default behavior byte-identical — `emitObject`/`CodeGen::print` unchanged when
   no `-O`).
3. **Delta recorded** (informational, **no threshold**): create
   `docs/epics/001-toolchain-and-stdlib/opt-delta.md` with `-O2` vs `-O0`
   binary-size numbers for a representative set (e.g. `demos/01_fibonacci.b` and a
   handful of codegen binaries), measured via `bcc -O2` vs `bcc -O0` + `wc -c` /
   `size`. A runtime delta is included if a cheap benchmark exists.
4. **`--release` builds + runs**: `./build/bcc --release demos/01_fibonacci.b -o
   /tmp/rel && /tmp/rel` exits 0 (asserts retained; -O2 applied).
5. **Cross-compile smoke**: `llc-18 --version | grep -q aarch64 && ./build/bcc
   --target aarch64-unknown-linux-gnu -c test_files/pass/func_simple.b -o /tmp/x.o
   && file /tmp/x.o | grep -qi aarch64` (skipped if no aarch64 target).
6. **`-O` levels accepted**: `bcc -O0/-O1/-O2/-O3/-Os/-Oz foo.b` each build and
   run a sample; an invalid `-O9` is rejected with a clear error.

## Risks
- **ARC × opt** — retain/release/lock calls must survive optimization (not be
  reordered/elided wrongly). Mitigation: the `OPT_LEVEL=2` full suite + its
  `--leak-check` are the guard (done-condition #1). If opt breaks a specific ARC
  pattern, that test goes red — a hard gate, not informational.
- **In-process pass API** (LLVM 18 new PassManager) — `PassBuilder` +
  `buildPerModuleDefaultPipeline`; verified the module still passes `verify()`
  after opt (add a post-opt verify).
- **`-O` × `-g`** — U3's concern; the `-g` forces `-O0` stance
  (`design-debug-info.md`) means they don't combine by default. U2 does not
  implement `-g`; the soft conflict is re-verified in U6 (second-to-merge
  rebases). U2 ensures emission is verifier-clean under `-O` so `-g -O2` at least
  verifies later.
- **Cross-compile scope** — object-emission only; full foreign linking is a
  Non-goal. If `--target` needs deeper support (per-target runtimes), that is an
  Open Question to the manager, not a silent scope cut.
- **test_codegen goldens under opt** — opt could theoretically change floating
  output formatting; the golden compare at `OPT_LEVEL=2` is the detector (any
  drift is a real bug to fix, not to mask).
