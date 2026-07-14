# Implementation Plan: Diagnostics Engine and Expected-Error Test Harness (U2)

**Branch**: `epic/blang-ast/u2-diagnostics-engine` | **Date**: 2026-07-13 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-diagnostics-engine/spec.md`
(epic blang-ast, unit U2, REQ-002 / REQ-003 / REQ-011 harness half)

## Summary

Route every user-facing compile error through one `DiagnosticEngine` that
renders the canonical `<file>:<line>:<col>: error: <message>` line from the
`SourceLocation` U1 already stamps on `CompileError`. `CompileError` stays as
the parser's exception/control-flow mechanism but its rendering moves out of
`CompileError::getMessage()` (which currently leads with the compiler's own
`__FILE__:__LINE__`) and into the engine; the C++ throw-site coordinates are
shown only under a new `--debug-compiler` flag. Statement-dispatch
backtracking (`QStatement.cpp`) stops discarding the specific parse error in
favor of a generic `"Unexpected token"` — it keeps the deepest located error.
All unconditional developer output (the `Completed parse` line, per-token
`Symbol …` lexer trace, AST/token dumps) is gated behind a new `-v` flag so a
clean compile is byte-silent. LLVM module-verification failure prints a
concise internal-compiler-error line (bug-report request), never the raw
verifier text. Finally, `run_tests.sh` learns expected-error matching: each
`fail/`/`cgfail/` test may declare a pattern (companion `.expected` file or
inline `// EXPECT-ERROR:` comment) that the compiler's stderr must match, and
≥ 10 existing `fail/` tests adopt it. No semantic checks; no accept/reject
drift. Decisions with rationale in [research.md](research.md) R1–R9.

## Technical Context

**Language/Version**: C++17 (house style: tabs, Allman braces, `m`-prefix
members, `Q`-prefixed parser files, PascalCase headers — CLAUDE.md)

**Primary Dependencies**: none new. LLVM 18 remains optional; the
DiagnosticEngine, both new flags, deepest-error retention, and quiet-by-
default are all LLVM-independent. The LLVM-verifier ICE message (FR-010) is
the only piece behind `BLANG_HAS_LLVM`.

**Storage**: N/A. Test declarations live as companion `<test>.expected` files
and/or `// EXPECT-ERROR:` comments in existing `.b` sources.

**Testing**: `./run_tests.sh` (162 parse tests) in both build modes,
`./test_codegen.sh` (63 E2E), plus the new Gate D quiet-compile check and the
expected-error self-check (add/mutate a pattern).

**Target Platform**: Linux (CI) and macOS; no platform-specific code.

**Project Type**: compiler (single CMake project, sources at repo root).

**Performance Goals**: no measurable compile-time change; diagnostics render
only on the (rare) error path.

**Constraints**: parser shape frozen (`Parse(Lexer&, Scope*)` factories,
`SmartPtr`, `QLang` namespace — design.md seam); **accepted/rejected program
set unchanged** (FR-016); single-error/located/clean only — no multi-error,
carets, warnings, or `--json` (design.md decision 5); `--combine`/multi-module
and the `bcc` pipeline keep working (`bcc`'s stderr grep-filter removal is
optional here).

**Scale/Scope**: 1 new component (`DiagnosticEngine`, header + small impl);
`CompilerHelpers.h`/`qcc.cpp` error-render change; `QStatement.cpp`
backtracking change; 2 new CLI flags in `qcc.cpp` (`-v`, `--debug-compiler`);
gate `Completed parse` + `FileLexer` trace + AST/token dumps behind `-v`;
1 LLVM-verifier message change; `run_tests.sh` harness extension; ≥ 10
`fail/` tests annotated. Estimated 8–15 files.

## Constitution Check

*GATE: evaluated against `.specify/memory/constitution.md` v1.1.0.*

| Principle | Assessment | Status |
|-----------|------------|--------|
| I — One Right Way / spec fidelity | No language syntax/semantics change. `CLAUDE.md` CLI-features list gains `-v` and `--debug-compiler` and the new error format (task included). `docs/language_design.md` untouched — no language rule changes. | PASS |
| II — Test-Gated Changes | Not a new language feature; it is diagnostics plumbing + a test-harness capability. Its verification is: Gate D quiet-compile, the expected-error harness self-check (SC-004), ≥ 10 annotated `fail/` tests (SC-005), and full suites green in both modes (SC-006). No new `codegen_*.b` required because accept/reject behavior is intentionally unchanged. | PASS |
| III — Reject, Don't Coerce | Directly advances it: cleaner located rejections; LLVM verifier failure becomes a loud ICE instead of raw spew; no new silent path, no coercion introduced or removed (removal is U4+). | PASS |
| IV — Memory/Thread Safety | No `runtime/*.c`, ARC, or ownership codegen touched → `--leak-check` not required for this unit (Gate C N/A). Full `test_codegen.sh` still run to prove no regression. | PASS |
| V — House Style | New `DiagnosticEngine` follows C++17 house style; PascalCase header, `m`-prefixed members, Allman braces. | PASS |

**Design-conformance (evaluation.md rubric 3)**: single DiagnosticEngine
path (decision 2); `CompileError` retained as control-flow carrying the U1
`SourceLocation`; API leaves room for severity + location + notes without
building them (decision 5); no sema pass introduced (that is U3); typed-AST
and codegen-trusts-loudly decisions are untouched (later units). No fixed
decision is violated. **No Complexity Tracking entries required.**

## Project Structure

### Documentation (this feature)

```text
specs/002-diagnostics-engine/
├── plan.md              # This file
├── research.md          # Phase 0 — R1..R9 decisions
├── data-model.md        # Phase 1 — DiagnosticEngine, Diagnostic, expected-error decl
├── quickstart.md        # Phase 1 — how to validate U2 by hand + gate commands
├── contracts/
│   ├── diagnostic-format.md      # the <file>:<line>:<col>: error: <msg> contract
│   ├── cli-flags.md              # -v and --debug-compiler behavior
│   └── expected-error-harness.md # run_tests.sh EXPECT-ERROR / .expected semantics
├── checklists/
│   └── requirements.md  # spec quality checklist (from /speckit-specify)
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
DiagnosticEngine.h            # NEW — Severity, Diagnostic {severity, loc, message, notes}, DiagnosticEngine::report/emit
DiagnosticEngine.cpp          # NEW — renders "<file>:<line>:<col>: error: <message>" to a std::ostream (stderr)
CompilerHelpers.h             # CompileError keeps SourceLocation + throw-site file/line (already there from U1)
qcc.cpp                       # driver: owns DiagnosticEngine; parse-catch renders via engine; add -v/--debug-compiler;
                              #   gate "Completed parse" + AST/token dumps behind -v; LLVM verifier failure -> ICE line
FileLexer.h / FileLexer.cpp   # per-token "Symbol …" trace already behind mTraceEnabled; ensure -v drives it, default off
QStatement.cpp                # backtracking: keep deepest located CompileError instead of generic "Unexpected token"
CMakeLists.txt                # add DiagnosticEngine.cpp to qcc target sources
run_tests.sh                  # expected-error mode: capture stderr, match .expected / // EXPECT-ERROR:, backward compat
test_files/fail/*.expected    # NEW — >=10 expected-message declarations (or inline // EXPECT-ERROR: comments)
CLAUDE.md                     # CLI-features list: -v, --debug-compiler, new error format, harness note
```

**Structure Decision**: Single compiler project, sources at repo root (matches
the existing layout). The DiagnosticEngine is a new standalone header+impl
compiled into `qcc`; everything else is a targeted edit to an existing file.
No new directories in the source tree; test declarations live beside the
tests they annotate.

## Phasing within the unit (implementation order)

1. **DiagnosticEngine + format** (FR-001..005): new component; route the
   parse-error catch in `qcc.cpp` through it; retire the
   `Compiler Error in <cpp>:<line>` rendering from the default path.
2. **`--debug-compiler`** (FR-003, FR-010 raw-text escape hatch): flag that
   re-enables the C++ throw-site coordinates and (LLVM) raw verifier text.
3. **Deepest-error retention** (FR-006): `QStatement.cpp` keeps the specific
   located error from the decl/expr attempts.
4. **Quiet-by-default + `-v`** (FR-007..009): gate `Completed parse`, lexer
   trace, AST/token dumps behind `-v`; preserve `--dump-locations` output.
5. **LLVM verifier ICE** (FR-010): replace the raw failure print with a
   concise ICE line (behind `BLANG_HAS_LLVM`).
6. **Harness** (FR-011..015): extend `run_tests.sh`; annotate ≥ 10 `fail/`
   tests; add a self-check that a mutated pattern fails.
7. **Gates + docs**: run Gate A/B/D; update `CLAUDE.md`.

Each step keeps suites green; the migration risk is concentrated in step 4
(a stray unconditional print re-appearing) and is covered by Gate D.

## Complexity Tracking

No constitution violations; no entries required.
