---
description: "Task list for U2 — Diagnostics Engine and Expected-Error Test Harness"
---

# Tasks: Diagnostics Engine and Expected-Error Test Harness (U2)

**Input**: Design documents from `/specs/002-diagnostics-engine/`

**Prerequisites**: plan.md, spec.md, research.md (R1–R9), data-model.md,
contracts/ (diagnostic-format, cli-flags, expected-error-harness)

**Epic**: blang-ast — Unit U2, covers REQ-002, REQ-003, REQ-011 (harness half).
**Branch**: `epic/blang-ast/u2-diagnostics-engine`.

**Tests**: This unit's "tests" are negative fixtures + harness self-checks (per
constitution II, a reject-only unit is verified by expected-message negative
tests and the gate suites), not new `codegen_*.b`. Test tasks are included
where the spec requires them (US1 diagnostics, US3 harness).

**Scope guardrails**: NO semantic checks. NO change to which programs are
accepted or rejected (FR-016). Every currently-passing test stays passing;
every currently-failing test stays failing with an improved message.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on incomplete task)
- **[Story]**: US1 (located errors), US2 (quiet), US3 (harness)

## Path Conventions

Single compiler project, sources at repo root (`DiagnosticEngine.*`, `qcc.cpp`,
`QStatement.cpp`, `FileLexer.*`, `CompilerHelpers.h`, `CMakeLists.txt`,
`run_tests.sh`, `test_files/fail/`, `CLAUDE.md`).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Confirm baseline before any change (no accept/reject drift check
starts here).

- [X] T001 Record the pre-U2 baseline: build both configs
  (`cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"`
  and `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"`),
  run `./run_tests.sh`, `./test_codegen.sh`, and `BUILD_DIR=build-parse ./run_tests.sh`,
  and note the passing/failing counts in the PR description (SC-007 anchor).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The single DiagnosticEngine that US1/US2 report through. MUST
complete before US1.

**⚠️ CRITICAL**: No user-story work begins until this phase is complete.

- [X] T002 Create `DiagnosticEngine.h` (repo root, `QLang` namespace, house
  style) per data-model.md: `enum class Severity { Error };`,
  `struct Note { SourceLocation location; std::string message; };`,
  `struct Diagnostic { Severity severity; SourceLocation location; std::string message; std::vector<Note> notes; };`,
  and `class DiagnosticEngine` with `std::ostream& mOut` (default `std::cerr`),
  `bool mHasErrors`, `bool mDebugCompiler`, methods `report(const Diagnostic&)`,
  `error(const SourceLocation&, const std::string&)`,
  `reportCompileError(const CompileError&)`, `hasErrors()`,
  `setDebugCompiler(bool)`. Include `SourceLocation.h` and forward/`#include`
  `CompilerHelpers.h` as needed.
- [X] T003 Create `DiagnosticEngine.cpp` implementing the render contract
  (`contracts/diagnostic-format.md`): line 1 always
  `<file>:<line>:<col>: error: <message>`; under `mDebugCompiler` append the
  C++ throw-site line for `reportCompileError`. No location prefix stored in
  `message`. Set `mHasErrors` on report.
- [X] T004 Wire `DiagnosticEngine.cpp` into the `qcc` target in `CMakeLists.txt`
  (add to the qcc sources list next to `qcc.cpp`); confirm both build configs
  still compile (`cmake --build build` and `cmake --build build-parse`).

**Checkpoint**: Engine compiles and links in both build modes; nothing routed
through it yet.

---

## Phase 3: User Story 1 — A rejected program points at the user's source (Priority: P1)

**Goal**: One canonical `<file>:<line>:<col>: error: <message>` line per
rejected compile; no C++ coords / raw IR by default; deepest error kept;
`--debug-compiler` opt-in; LLVM verifier failure is a clean ICE.

**Independent test**: `./build/qcc --parse-only test_files/fail/missing_brace.b 2>&1`
prints exactly one line matching `^[^:]+\.b:[0-9]+:[0-9]+: error: `, no
`Compiler Error in` line, no raw LLVM text; `line:col` points at the offending
token.

- [X] T005 [US1] In `qcc.cpp`, reduce `CompileError::getMessage()` to the raw
  message body only (remove the `Compiler Error in <cpp-file>:<cpp-line>` and
  ` at line: N` formatting at `qcc.cpp:28-37`); the located prefix is now the
  engine's job (R2).
- [X] T006 [US1] In `qcc.cpp`, route the top-level parse-error catch
  (`qcc.cpp:294-295`, `cerr << err.getMessage()`) through a driver-owned
  `DiagnosticEngine` instance via `reportCompileError(err)`; ensure the process
  still exits non-zero on error. Reuse one engine across all input files
  (`--combine` safe).
- [X] T007 [US1] Add the `--debug-compiler` flag to `qcc.cpp` arg parsing
  (`bool debugCompiler`), pass it into the engine (`setDebugCompiler`), and add
  it to `printUsage`/`--help` (contracts/cli-flags.md).
- [X] T008 [US1] Deepest-error retention in `QStatement.cpp:70-88`: capture the
  `CompileError` from the `VariableDeclaration::Parse` and `Expression::Parse`
  attempts; on total failure rethrow the one with the greater `SourceLocation`
  (line then col); fall back to a located `"Unexpected token"` at the
  statement's first token only when neither carries a usable location (R4).
- [X] T009 [US1] LLVM verifier ICE (behind `BLANG_HAS_LLVM`): at the
  `codegen.verify()` failure sites (`qcc.cpp:820-823`, `885-887`) replace the
  raw `Module verification failed` print with
  `internal compiler error: generated IR failed verification; please report this bug`
  (no `file:line:col` prefix); print the raw verifier text only under
  `--debug-compiler` (R7).

### US1 verification (tests)

- [X] T010 [P] [US1] Spot-check ≥ 3 distinct `test_files/fail/` parse fixtures
  by hand: emitted `line:col` points at the offending token, exactly one
  `error:` line, no `Compiler Error in`, no raw LLVM text (SC-002, SC-003).
  Record the actual messages for use in Phase 5 patterns.
- [X] T011 [P] [US1] Verify `--debug-compiler` adds the throw-site line and the
  default mode omits it, on the same fixture (contracts/cli-flags.md).

**Checkpoint**: Every rejection prints the canonical located line; suites still
green (no accept/reject drift — re-run Gate A/B here).

---

## Phase 4: User Story 2 — A successful compile is silent (Priority: P1)

**Goal**: Clean compile is byte-silent by default; `-v` re-enables developer
output; `--dump-locations` unchanged.

**Independent test**: `out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"`
succeeds, exit 0.

- [X] T012 [US2] Add `-v` / `--verbose` flag to `qcc.cpp` arg parsing
  (`bool verbose`, default false) and list it in `printUsage`/`--help`.
- [X] T013 [US2] Gate the `cout << "Completed parse"` line (`qcc.cpp:717`) and
  any AST/token dump prints behind `verbose`.
- [X] T014 [US2] Drive the lexer trace from `verbose`: call
  `l.setTraceEnabled(verbose)` (default off), so the `FileLexer.cpp:315-321`
  per-token `Symbol …` dump only appears under `-v`. Confirm `--dump-locations`
  still forces trace off and its node dump is unaffected (FR-009).
- [X] T015 [P] [US2] Verify `-v` re-enables `Completed parse`/trace on a passing
  file and exit code stays 0 (contracts/cli-flags.md).

**Checkpoint**: Gate D passes; `-v` restores developer output; suites green.

---

## Phase 5: User Story 3 — Negative tests assert the diagnostic (Priority: P1)

**Goal**: `run_tests.sh` matches per-test expected patterns; ≥ 10 `fail/` tests
adopt them; backward compatible when absent.

**Independent test**: adding a declaration to a `fail/` test makes it pass;
mutating it to a non-matching pattern makes that test fail.

- [X] T016 [US3] Extend `run_tests.sh` `run_test()` for the `fail`/`cgfail`
  categories (contracts/expected-error-harness.md): capture stderr (stop
  discarding it), resolve an expected pattern with precedence
  companion-`<test>.expected`-file > inline `// EXPECT-ERROR: <pattern>` >
  none, and judge PASS iff exit≠0 AND (`grep -Eq` match when a pattern applies)
  — exit-code-only when no pattern (FR-011..014). In `--verbose`, on a
  pattern-mismatch print the expected pattern and actual stderr.
- [X] T017 [US3] Ensure the harness invokes the compiler deterministically so
  the asserted stderr matches `build/qcc --parse-only <file>` for parse-stage
  errors and does not pass `-v`/`--debug-compiler` (contracts/expected-error-harness.md).
- [X] T018 [US3] Annotate ≥ 10 existing `test_files/fail/` tests with
  expected-message declarations, patterns derived from the **real U2 output**
  captured in T010 (mix of the canonical regex `^[^:]+\.b:[0-9]+:[0-9]+: error: `
  and stable message substrings). Suggested set from R9:
  `missing_brace.b`, `missing_paren.b`, `c_style_func.b`, `for_c_style.b`,
  `duplicate_func.b`, `undefined_func.b`, `undefined_var.b`,
  `enum_missing_brace.b`, `struct_missing_brace.b`, `test_missing_name.b`
  (adjust to whichever have stable messages; keep ≥ 10).
- [X] T019 [US3] Run `./run_tests.sh` (both build modes) and confirm the ≥ 10
  annotated tests pass in the new mode and total pass/fail counts are unchanged
  from the T001 baseline (SC-005, SC-007).

**Checkpoint**: Harness live; ≥ 10 annotated tests green; backward compatible.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T020 [P] Update `CLAUDE.md` CLI-features list: add `-v`/`--verbose`,
  `--debug-compiler`, the new `<file>:<line>:<col>: error:` diagnostic format,
  quiet-by-default behavior, and the `run_tests.sh` expected-error mode
  (constitution I). No `docs/language_design.md` change (no language rule
  changed).
- [X] T021 Confirm `--combine`/multi-module builds and the `bcc` pipeline still
  work (compile a multi-file `--combine` case and a `bcc` build); note that
  `bcc`'s stderr grep-filter is now unnecessary but its removal is deferred
  (assumption in spec).

---

## Phase 7: Gates (per-unit, must all pass before requesting review)

- [X] T022 Gate A (LLVM build): `./run_tests.sh && ./test_codegen.sh` exit 0.
- [X] T023 Gate B (parse-only build): `BUILD_DIR=build-parse ./run_tests.sh`
  exit 0.
- [X] T024 Gate D (quiet compile):
  `out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"`
  succeeds with exit 0.
- [X] T025 SC-004 harness self-check: temporarily mutate one annotated test's
  pattern to `ZZZ_NO_MATCH`, confirm `./run_tests.sh` now FAILs that test, then
  revert; confirm the un-mutated suite is green.

---

## Dependencies & Execution Order

- **Setup (T001)** → baseline recorded.
- **Foundational (T002–T004)** blocks everything downstream (the engine).
- **US1 (T005–T011)** depends on Foundational. Delivers the format contract.
- **US2 (T012–T015)** depends only on Foundational (independent of US1) but is
  most naturally done after US1; can run in parallel with US1 by a second pair
  of hands since it touches disjoint code paths in `qcc.cpp` (flag parsing +
  dump gating vs. error catch). Coordinate the shared `qcc.cpp` edits.
- **US3 (T016–T019)** depends on US1 (patterns are derived from US1's real
  output, T010→T018) and on US2 (deterministic quiet stderr).
- **Polish (T020–T021)** after US1–US3.
- **Gates (T022–T025)** last.

## Parallel Opportunities

- T010, T011 (US1 verification) are `[P]` — independent read-only checks.
- T015 (US2 verification) is `[P]`.
- T020 (docs) is `[P]` relative to T021.
- The DiagnosticEngine header (T002) and impl (T003) are sequential (impl needs
  the header); CMake wiring (T004) follows.

## MVP Scope

US1 (located, clean, single error) is the MVP and the epic's format
foundation. US2 (silence) and US3 (harness) complete the unit and are required
for the done-condition, but US1 alone already delivers REQ-002's core value.

## Implementation Strategy

Follow plan.md's implementation order (DiagnosticEngine → route catch →
`--debug-compiler` → deepest-error → quiet/`-v` → verifier ICE → harness →
annotate → docs → gates). Keep suites green at each checkpoint; the highest-
risk step is US2's quiet-by-default (a stray unconditional print) — Gate D
guards it.
