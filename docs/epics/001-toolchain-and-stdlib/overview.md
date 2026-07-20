# Epic 001: toolchain-and-stdlib — Phase 4: diagnostics, optimization, debug info, and stdlib breadth

**Archetype**: evolve (extends the existing compiler and standard library)

**Status**: planning

**Owner**: Ben Payne

**Created**: 2026-07-19 · **Updated**: 2026-07-19

**Source documents**: the production-readiness roadmap (Phase 4 — "make it
usable day-to-day"); plan-time recon 2026-07-19 (two agents — compiler side
and stdlib side), summarized in `design.md` §Context and the four area design
docs.

## Why

Phases 1–3 made BLang *correct* (a real semantic pass), *tested* (a
teeth-checked harness), *unified* with origin's features, and *hardened*
against interaction bugs. What remains for a language you'd actually build on
day-to-day is Phase 4 — four independent gaps, all confirmed by recon:

1. **Diagnostics** — the compiler reports **one error and stops** (the parser
   is first-error-fatal; Sema collects within a module but the driver aborts at
   the first failing file). There are **no warnings** (the severity enum has a
   single value) and **no machine-readable output** — a real miss for the
   LLM-codegen goal.
2. **Optimization** — **zero**. `bcc` runs `llc` with no `-O`, no `opt` stage,
   no in-process pass pipeline; output is `-O0`-equivalent. No `--release`, no
   cross-compilation (the target triple is baked to the host).
3. **Debug info** — **zero DWARF** (no `DIBuilder`, no `-g`), so debuggers can't
   map to `.b` source. The groundwork exists: every AST node already carries a
   `SourceLocation`.
4. **Stdlib breadth** — no **math, time/date, random, env/args-parsing, sort**,
   and the only map (`collections.Map`) is an **O(n) parallel-array scan** with
   no hash map and no set. You can build a networked DB service but not a CLI
   tool that reads config, parses flags, does math, and times operations.

This epic delivers all four to **first-class depth** (user's decision), each
area producing its own speckit spec reviewed by a dedicated **architect** role,
so the four workstreams stay coherent.

## Scope note (this is the largest epic in the program)

The user chose **full/complete each area**, not a bounded first pass. That makes
this substantially bigger than prior epics — four deep workstreams plus a shared
pipeline foundation. It is structured as 7 units with a 3-role team (architect
+ implementer + code reviewer) precisely to keep it tractable, and readiness
review should explicitly consider whether to split it (e.g. compiler-toolchain
vs stdlib) before launch.

## Done condition (epic level)

All of the following hold on a clean checkout of the epic's final state
(concrete commands in `evaluation.md`):

1. **Correctness preserved at every setting**: `./run_tests.sh` (LLVM and
   parse-only), `./test_codegen.sh`, and `ctest --test-dir build` exit 0; and
   the codegen suite also passes when built at **`-O2`** and with **`-g`**.
2. **Diagnostics**: a committed multi-error fixture with ≥ 3 independent errors
   produces ≥ 3 located `file:line:col:` diagnostics from **one** `qcc`
   invocation; `qcc --json` emits schema-valid JSON (severity/file/line/col/
   message/code) parseable by `python3 -c 'import json,sys; json.load(sys.stdin)'`;
   a `warning:`-severity diagnostic is emitted by a committed fixture and
   `-Werror` promotes it to a non-zero exit.
3. **Optimization**: `bcc -O2` compiles and runs the whole codegen suite green
   (correctness is the hard gate); a size/perf delta of `-O2` vs `-O0` is
   measured and recorded in `docs/epics/001-toolchain-and-stdlib/opt-delta.md`
   (informational, no threshold); `--release` builds; `bcc --target
   <non-host-triple> -c` emits an object file (cross-compile smoke).
4. **Debug info**: `bcc -g hello.b` yields a binary whose DWARF shows line
   tables (naming the `.b` file) and a `DISubprogram` per function
   (`llvm-dwarfdump-18` greps confirm); a scripted `gdb` smoke test sets a
   breakpoint in the binary and hits it; the `-g` × `-O` stance is implemented
   and tested (either `-g` forces `-O0`, or debug info is opt-safe and the
   module verifies under `-O2 -g`).
5. **Stdlib breadth**: `math`, `time`, `random`, `env`, CLI-flag parsing, a
   generic `sort`, and a **hashed** `Map`/`Set` are each usable via `bcc`
   (`import`), each with ≥ 1 behavioral `codegen_*.b` test (golden where
   deterministic); C-backed modules also have `ctest` runtime unit tests; the
   new `Map` is hashed (not the O(n) scan) with a test demonstrating it.
6. **Process + integration**: each of the four areas has committed speckit
   artifacts under `specs/` that passed the architect's spec audit; the
   `codegen_*.b` count grew by ≥ 25 vs the launch baseline (107); CI runs all
   new legs green on the final commit.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | **Pipeline foundation**: consolidate the 4 duplicated `llc`/qcc-invocation + runtime-link blocks in `bcc.cpp` into one, and establish the shared flag-threading path (qcc arg loop + bcc option struct) that `--json`/`-O`/`-g`/new-stdlib-link all extend. Behavior-preserving. | P1 | Done-condition #1 (suites green post-refactor) |
| REQ-002 | **Multi-error diagnostics**: parser panic-mode recovery + fixed per-file early-return so one compile reports all errors; a `warning` severity + `-Werror`; `--json` structured output (both syntax and semantic diagnostics). | P1 | Done-condition #2 |
| REQ-003 | **Optimization**: `-O0..3/s/z` via an in-process pass pipeline (+ `llc -O`); `--release` semantics; `--target` cross-compilation; correctness preserved, delta recorded. | P1 | Done-conditions #1, #3 |
| REQ-004 | **Debug info (DWARF)**: `DIBuilder` compile unit + per-function `DISubprogram` + per-statement `DebugLoc` + param/local `dbg.declare`; `-g` flag; a resolved `-g`×`-O` product stance; ARC-emitted instructions carry synthetic locations. | P1 | Done-condition #4 |
| REQ-005 | **Native stdlib modules** (C-runtime backed): `math` (libm), `time`/`date`, `random`, `env` — each following the 6-touchpoint authoring recipe (`.b` + `blang_*.c` + CMake + import-gated + C unit tests + codegen tests). | P1 | Done-condition #5 |
| REQ-006 | **Collections & CLI stdlib**: a real **hashed** `Map` + `Set` (replacing O(n) `collections.Map`), a generic `sort`, and CLI flag/arg parsing (built on `sys.args`). | P1 | Done-condition #5 |
| REQ-007 | **Per-area speckit specs + architect review + integration**: each area produces committed speckit artifacts that passed the architect's Phase-2 spec audit; cross-area verification (esp. `-g`×`-O2`, `--json` over both diagnostic paths); CI legs; docs updated. | P1 | Done-condition #6 |

## Non-goals

- **A source-level debugger UI / IDE integration (LSP)** — this epic ships the
  DWARF the debugger reads, not a debugger or language server.
- **A perf-optimization *target*** — optimization's bar is correctness +
  recorded delta, not a speed threshold (user decision; a metric is brittle).
- **Full cross-compilation matrix / runtime for every target** — `--target`
  ships as an object-emission smoke test, not a fully-linked foreign binary for
  N platforms.
- **Regex, process/exec, encoding (base64/hex)** — deliberately deferred stdlib
  modules; a second stdlib tier if wanted later.
- **Rewriting the test harness** — reuse goldens/`--leak-check`/`ctest`/fuzz.

## Companion documents

| File | Purpose |
|------|---------|
| `workplan.md` | 7 units, dependency map, per-unit done conditions |
| `design.md` | umbrella: shared pipeline (U0), cross-area interactions, team/architect |
| `design-diagnostics.md` | detailed design — multi-error, warnings, `--json` |
| `design-optimization.md` | detailed design — pass pipeline, `--release`, cross-compile |
| `design-debug-info.md` | detailed design — DWARF emission, `-g`×`-O` stance |
| `design-stdlib.md` | detailed design — the 6-touchpoint recipe + each module |
| `evaluation.md` | harness commands, the 3-role audit plan, epic acceptance |
| `manifest.yaml` | machine-readable run definition |
| `opt-delta.md` | (created by the run) recorded `-O2` vs `-O0` size/perf delta |

## Constraints & context for the manager

- **Team is 3 roles** (user decision): an **architect** (Phase-2 spec audits
  across all four areas, for cross-area coherence and design-doc conformance),
  an **implementer**, and an **independent code reviewer/merger** (Phase-4 code
  audits + merges). See `manifest.yaml` `hires` and `design.md` §Team.
- **The bcc pipeline is bcc→qcc(CLI)→textual `.ll`→out-of-process `llc`.** Every
  new flag (`--json`/`-O`/`-g`) must be added to qcc's arg loop, threaded from
  bcc's option struct, and survive the text-`.ll` boundary (esp. debug metadata).
  U0 consolidates the 4 duplicated invocation blocks first to avoid multiplying
  that hazard.
- **`-g` × `-O` is the classic hazard** (Area 3 × Area 2): a product stance is
  required, not discovered at merge time — see `design-debug-info.md`.
- **New stdlib modules are import-gated** (`kKnownOrder` in `bcc.cpp:831`), never
  added to the always-on `{sys,buffer,fs,net}` set; each needs the 6-touchpoint
  wiring (`design-stdlib.md`), and the triplicated bcc link sites are a trap U0
  should relieve.
- **Do not weaken existing green suites**; keep `--leak-check`/goldens/`ctest`
  green at every unit boundary. New C runtimes get `ctest` tests (they run under
  ASan in `build-asan`).
- Constitution applies: `.specify/memory/constitution.md`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| Q1 | Split this epic (compiler-toolchain vs stdlib) before launch, given its size and single-implementer serial execution? | Yes (pre-launch) | **Resolved** | PM (2026-07-19): **keep one epic, single implementer, serial**. Accept the exhaustion risk; if the run halts mid-way, resume/redo (a supported, previously-exercised pattern). U1–U5 are independent but the single implementer runs them sequentially. |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-07-19 | — | epic created | Phase 4, all four areas at full depth; 3-role team (architect + implementer + code reviewer); 7 units; grounded in two recon reports. |
| 2026-07-19 | — | readiness review round 1 | 2 blockers + mediums (fresh-context + self audit). Blocker-1: acceptance used bare qcc/bcc (not on PATH) + llvm-dwarfdump (only -18 exists) + lldb (absent) → all fixed to ./build/qcc, ./build/bcc, llvm-dwarfdump-18, gdb-only, + a Prerequisites block. Blocker-2 (too big / no real parallelism with 1 implementer): PM chose keep-one-epic-serial (OQ Q1 resolved); 'parallel' language corrected to serial. HIGH-3: gdb gate made literal (break main) + asserted (grep 'Breakpoint 1') + added to acceptance block. MEDIUM-5: warning/-Werror, --release, JSON schema-keys folded into the runnable block. MEDIUM-6: specs glob marked necessary-not-sufficient + Reviewed-by: architect marker. MEDIUM-7: U2/U3 Principle-II carve-out added. Cleared: aarch64 target present, baseline pinned 132, done_condition verbatim, DAG acyclic, hires well-formed. |
