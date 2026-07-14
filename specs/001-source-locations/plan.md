# Implementation Plan: Source Locations End to End (U1)

**Branch**: `epic/blang-ast/u1-source-locations` | **Date**: 2026-07-13 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-source-locations/spec.md`
(epic blang-ast, unit U1, REQ-001)

## Summary

Give every AST node an accurate `SourceLocation {file, line, col}`. Line/
column counting moves into `LexerReader::popChar()` (covering multi-line
tokens); per-token positions are frozen into the lexer's `SymbolInfo` replay
list so parser backtracking cannot desynchronize them; every `Parse` factory
stamps its node with the location of the construct's first token;
`CompileError` snapshots the location at throw time instead of reading the
live lexer. A new `qcc --dump-locations` flag renders a deterministic
pre-order `<file>:<line>:<col> <NodeKind>` dump, locked by two committed
golden files. No change to which programs are accepted or rejected; all
suites stay green in both build modes. Full decisions with rationale:
[research.md](research.md) R1–R8.

## Technical Context

**Language/Version**: C++17 (house style: tabs, Allman, `m`-prefix members — CLAUDE.md)

**Primary Dependencies**: none for this unit (LLVM 18 remains optional; the
new flag and all location plumbing are LLVM-independent — FR-009)

**Storage**: N/A (two committed golden text files under `test_files/golden/`)

**Testing**: `./run_tests.sh` (162 parse tests), `./test_codegen.sh` (63 E2E),
parse-only suite via `BUILD_DIR=build-parse ./run_tests.sh`, plus the U1
golden-diff commands (evaluation.md §Unit-specific checks)

**Target Platform**: Linux (CI), macOS supported; Itanium C++ ABI assumed for
`typeid` demangling (research R6)

**Project Type**: compiler (single CMake project, sources at repo root)

**Performance Goals**: no measurable compile-time regression; location
tracking is O(1) per character/token

**Constraints**: parser shape frozen (`Parse(Lexer&, Scope*)` factories,
SmartPtr, QLang namespace — design.md seam); accepted/rejected program set
unchanged; default-mode console output unchanged except where
`--dump-locations` is active (research R7); user-visible error *format*
redesign deferred to U2

**Scale/Scope**: ~30 `Q*.cpp` parser files stamp locations; 3 AST base
classes gain a member; 1 new header, 1 new walker class, 1 new CLI flag,
2 golden files; ~413 `COMPILE_ERROR` sites migrate via a single macro change

## Constitution Check

*GATE: evaluated against `.specify/memory/constitution.md` v1.1.0.*

| Principle | Assessment | Status |
|-----------|------------|--------|
| I — One Right Way / spec fidelity | No language syntax/semantics change. `CLAUDE.md` gains `--dump-locations` in CLI features (task included). `docs/language_design.md` untouched — no semantic change to document. | PASS |
| II — Test-Gated Changes | Not a language feature — an internal capability + CLI flag; its tests are the two golden diffs + determinism checks (this unit's done-when), run as per-unit gates. No new pass/fail/codegen tests required because accepted/rejected behavior is intentionally unchanged; full suites must stay green in both modes (Gate A/B). | PASS |
| III — Reject, Don't Coerce | No new coercion or silent-drop paths; groundwork FOR located diagnostics. Compiler-internal `__FILE__:__LINE__` retained in `CompileError` but its user-visible demotion is U2's mandate (workplan). | PASS |
| IV — Memory/Thread Safety | No `runtime/*.c` or ARC/ownership codegen changes → leak-check gate not triggered. | PASS (N/A) |
| V — House Style | New code follows tabs/Allman/`m`-prefix/PascalCase headers; `LocationDumper` mirrors `BmodEmitter` precedent. | PASS |

**Post-design re-check** (after Phase 1 artifacts): no violations introduced;
Complexity Tracking empty.

## Project Structure

### Documentation (this feature)

```text
specs/001-source-locations/
├── spec.md              # /speckit-specify output
├── plan.md              # This file
├── research.md          # Phase 0 — decisions R1..R8
├── data-model.md        # Phase 1 — SourceLocation, SymbolInfo, node/CompileError shapes
├── quickstart.md        # Phase 1 — validation runbook
├── contracts/
│   └── dump-locations-cli.md   # CLI + dump-line grammar + internal API contracts
├── checklists/
│   └── requirements.md  # spec quality checklist (all pass)
└── tasks.md             # /speckit-tasks output
```

### Source Code (repository root — flat layout, existing convention)

```text
SourceLocation.h         # NEW — SourceLocation value struct
FileLexer.h / FileLexer.cpp      # SymbolInfo {+line,+col}; token-position capture;
                                 #   getTokenLocation(); setTraceEnabled(); trace gating
LexerReader.cpp / FileLexer.h    # LexerReader {+mFileName,+mLine,+mCol}; counting in popChar
Type.h                   # Statement/Symbol/Type bases gain mLocation + accessors
Expression.h             # (nodes inherit from Statement — no per-class edits expected)
CompilerHelpers.h        # CompileError {+mLocation, -mLexer}; COMPILE_ERROR macro snapshots
qcc.cpp                  # --dump-locations flag; CompileError::getMessage() reads mLocation;
                         #   dump-mode output suppression (research R7)
Q*.cpp (~30 files)       # every Parse factory stamps node locations (capture-at-entry)
LocationDumper.h / LocationDumper.cpp   # NEW — pre-order location dump walker
CMakeLists.txt           # add LocationDumper.cpp (+ SourceLocation.h header-only) to qcc target
CLAUDE.md                # document --dump-locations (constitution I)
test_files/golden/func_simple.locations   # NEW golden
test_files/golden/match_basic.locations   # NEW golden
```

**Structure Decision**: keep the repository's flat single-project layout;
one new header + one new walker class pair, everything else edits in place.
No new directories except `test_files/golden/`.

## Implementation strategy (maps research → code)

1. **Reader counters** (R2): `LexerReader` retains filename, counts
   line/col in `popChar`; expose getters. Fixes multi-line-token line drift
   as a side effect.
2. **Per-token positions** (R1): `Lexer` snapshots reader position at
   token-recognition start into widened `SymbolInfo {symbol, text, line,
   col}`; `getTokenLocation()` reads the record at `mCurrentPos` (peek/get
   consistent; correct after `setCurrentPos`). `getLineNumber()` delegates
   to the current token record.
3. **Node stamping** (R3, R4): `SourceLocation.h`; `mLocation` +
   accessors on `Statement`, `Symbol`, `Type`; each `Parse` factory
   captures `l.getTokenLocation()` at entry and stamps every node it
   constructs; desugared nodes inherit their construct's location.
4. **CompileError** (R5): macro snapshots location at throw; reporting
   (`getMessage`, qcc.cpp) uses the stored location; live-lexer dependency
   removed. Message shape otherwise unchanged (U2 owns format).
5. **Dumper + flag** (R6, R7): `LocationDumper` pre-order walker with
   `typeid`-demangled NodeKinds; `--dump-locations` flag implies
   parse-only, suppresses non-dump stdout (lexer trace gate + qcc info
   prints) so stdout is exactly the dump.
6. **Goldens + docs** (R8): generate, hand-verify every line against the
   source, commit; update CLAUDE.md; run Gates A/B + unit-specific diffs.

## Verification (per-unit gates — evaluation.md)

```bash
# Gate A: LLVM build + full suites; Gate B: parse-only build + suite
# (commands in quickstart.md)
# U1-specific: both golden diffs exit 0; no :0 line/col in dumps;
# determinism and build-mode parity diffs empty.
```

## Complexity Tracking

*No constitution violations — table intentionally empty.*
