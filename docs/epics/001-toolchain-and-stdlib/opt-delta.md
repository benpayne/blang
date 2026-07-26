# Optimization delta — `-O2` vs `-O0` (informational)

**Epic**: 001-toolchain-and-stdlib · **Unit**: U2 (optimization) · Measured 2026-07-20
(host x86_64-linux, llvm-18).

This is **informational only** — the epic's optimization bar is *correctness*
(the whole `codegen_*.b` suite is green built at `-O2`, with `--leak-check`
clean), **not** a performance threshold (design D2: perf numbers are brittle and
hardware-dependent). No pass/fail gate keys off the numbers below.

## Method

- **Pipeline**: `bcc -O2` runs the in-process LLVM new-PassManager
  `buildPerModuleDefaultPipeline(O2)` over the module in qcc (layer 1), then
  `llc -O2` for backend codegen (layer 2). `-O0` runs neither (byte-identical to
  the pre-U2 build).
- **Binary size**: `bcc -O<n> <src> -o out && wc -c out` (whole executable,
  statically linked with the BLang runtime `.a`s — the runtime dominates, so the
  whole-binary delta understates the code-opt effect).
- **User-code object size**: `bcc -O<n> -c <src> -o out.o && wc -c out.o` (the
  compiled user + combined-stdlib code, before linking the prebuilt runtime — a
  truer measure of what optimization does to emitted code).

## Whole-binary size (`wc -c` of the linked executable)

| program | -O0 bytes | -O2 bytes | delta |
|---------|-----------|-----------|-------|
| demos/01_fibonacci        | 92600 | 84408 | −8.8% |
| codegen_comprehensive     | 92648 | 84456 | −8.8% |
| codegen_binexpr           | 92576 | 84384 | −8.8% |
| codegen_forin             | 92536 | 84320 | −8.9% |
| **total**                 | **370360** | **337568** | **−8.9%** |

## User-code object size (`wc -c` of `-c` output, before runtime link)

| program | -O0 .o | -O2 .o | delta |
|---------|--------|--------|-------|
| demos/01_fibonacci        | 54848 | 47688 | −13.1% |
| demos/02_primes           | 55736 | 48000 | −13.9% |
| demos/03_collatz          | 58344 | 50240 | −13.9% |
| demos/10_math_library     | 67168 | 54136 | −19.4% |
| codegen_comprehensive     | 54096 | 45816 | −15.3% |

## Runtime

The demo/codegen programs are micro-benchmarks (they complete in well under a
millisecond of wall time — below the resolution of a shell `time` loop), so a
runtime delta is not meaningfully measurable here and is **not** recorded as a
number. The IR-level effect is visible directly: e.g. `fn sq(x)=x*x; main{ b =
sq(3) }` compiles to `ret i32 9` at `-O2` (full inline + constant fold) vs three
`alloca`s + a `call` at `-O0`. A representative-workload benchmark harness is out
of scope for this epic (Non-goal: a perf target).

## Interpretation

`-O2` consistently shrinks emitted user code ~13–19% and the linked binary ~9%,
via the standard inlining / mem2reg-SROA / GVN / DCE pipeline. Crucially, **every
one of the 107 `codegen_*.b` tests produces byte-identical golden stdout at
`-O2`** and the suite is `--leak-check`-clean — i.e. optimization changed the
code but not the observable behavior or the ARC memory discipline, which is the
actual acceptance bar.
