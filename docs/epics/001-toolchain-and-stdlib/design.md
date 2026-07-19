# Design: 001-toolchain-and-stdlib (umbrella)

**Epic**: [overview.md](overview.md)

The four areas each have a detailed companion design doc
([diagnostics](design-diagnostics.md), [optimization](design-optimization.md),
[debug-info](design-debug-info.md), [stdlib](design-stdlib.md)). This umbrella
covers what they share: the pipeline foundation (U0), cross-area interactions,
and the team/architect model.

## Context (recon 2026-07-19)

The compile pipeline is: **`bcc` (driver) → `qcc` (frontend, in-process
parse→Sema→IR, prints textual `.ll`) → out-of-process `llc -filetype=obj` → `cc`
(link)**. Two structural facts shape every unit:

1. **bcc drives qcc over the CLI, and qcc emits textual `.ll`.** Every new
   capability (`--json`, `-O`, `-g`) must be (a) added to qcc's arg loop
   (`qcc.cpp:507`), (b) threaded from bcc's option struct (`bcc.cpp:43`) into the
   `llc`/qcc invocations, and (c) survive the text-`.ll` boundary (critical for
   debug metadata).
2. **The `llc`/qcc-invocation + runtime-link logic is duplicated 4×** in
   `bcc.cpp` (`~328` test path, `~1017` lib build, `~1139` combined build, `~1478`
   single-file) and the runtime-link edits are triplicated. Adding three flags
   and several stdlib libs across four copies is the epic's biggest maintenance
   hazard.

## Shared foundation — U0

U0 consolidates those duplicated blocks into a single helper and factors the
flag-threading path, so that:
- U1 adds `--json`, U2 adds `-O`, U3 adds `-g`, U4/U5 add link libs — **each in
  one place**, not three or four.
- The architect can audit that every unit routes through the single path (a spec
  that re-duplicates is a blocking finding).

U0 is behavior-preserving: existing tests produce byte-identical `.ll`/link
behavior. It is the one hard sequencing constraint — U1–U5 all build on it.

## Cross-area interactions

| Interaction | Where | Handling |
|-------------|-------|----------|
| **`-g` × `-O`** (debug × opt) | U2 ∥ U3 | The classic hazard. Stance fixed in `design-debug-info.md` (S-A: `-g` forces `-O0`, emission verifier-clean under `-O`). Soft conflict; second-to-merge rebases + re-runs the matrix; U6 re-verifies. |
| **`--json` over both diagnostic paths** | U1 | Syntax and semantic diagnostics use different control paths; the collector is the single sink for both; tested against a program with both error kinds. |
| **ARC × opt × debug** | U2, U3 | Retain/release/lock instructions must survive opt (U2: `-O2` suite is the guard) and carry synthetic `DebugLoc` (U3: else verifier fails under `-O`). |
| **Stdlib link wiring** | U4, U5 on U0 | The triplicated link sites U0 consolidates; every new module is import-gated. |
| **float codegen untested** | U4 | Math is the first float exercise; budget for a float codegen bug. |

## Team — the 3-role model (user decision)

| Role | Model | Owns |
|------|-------|------|
| **Architect** | Opus (stronger) | **Phase-2 spec audits** for all four areas — cross-area coherence, conformance to these design docs, that each unit threads through U0's single path; a single cross-epic role so the four workstreams stay consistent. |
| **Implementer** | Sonnet | Speckit ceremony (specify/plan/tasks/implement) + the code, one unit at a time. |
| **Code reviewer / merger** | Sonnet | **Phase-4 code audits** + the merge; independent of the implementer; runs all gates. |

This separates *spec* review (architect, cross-area, design-level) from *code*
review (reviewer, correctness/gates), which matters for an epic whose risk is
four workstreams drifting apart. The manager (Opus controller) directs all
three and gates phase transitions.

## Key decisions (umbrella)

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | U0 first — consolidate the pipeline before adding flags | Avoids multiplying the 4×/3× duplication across three new flags + N stdlib libs |
| D2 | Single architect for spec audits across all areas | Cross-area coherence is the epic's main risk at this breadth |
| D3 | Opt gated on correctness + recorded delta, not a perf threshold | User decision; perf numbers are brittle |
| D4 | `-g` forces `-O0` (S-A) but emission is verifier-clean under `-O` | Fidelity-first, with `-g -O2` a small future step |
| D5 | All new stdlib modules import-gated, 6-touchpoint recipe | Keep the always-on set minimal; codify the wiring so nothing is half-linked |

## Invariants — must not break
- U0 is behavior-preserving; existing suites stay green with identical output.
- Correctness holds at `-O0`, `-O2`, and `-g` (the codegen suite runs at each).
- ARC retain/release/lock semantics survive optimization and carry `DebugLoc`.
- New stdlib is import-gated; the always-on `{sys,buffer,fs,net}` set is unchanged.
- Existing goldens/`--leak-check`/`ctest`/fuzz stay green at every unit boundary.
