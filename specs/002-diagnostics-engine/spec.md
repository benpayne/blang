# Feature Specification: Diagnostics Engine and Expected-Error Test Harness (U2)

**Feature Branch**: `epic/blang-ast/u2-diagnostics-engine`

**Spec Directory**: `specs/002-diagnostics-engine`

**Created**: 2026-07-13

**Status**: Draft

**Epic**: blang-ast (`docs/epics/blang-ast/`) — Unit U2, covers **REQ-002**,
**REQ-003**, and the harness half of **REQ-011**.

**Input**: User description: "U2 — Diagnostics engine and expected-error test
harness. Introduce a single diagnostic reporting path formatting
`<file>:<line>:<col>: error: <message>`; name the offending token; keep the
deepest error in statement backtracking; demote C++ `__FILE__:__LINE__` to a
`--debug-compiler` flag; gate all per-token/trace/AST-dump output behind `-v`;
LLVM verifier failure prints an internal-compiler-error message, not raw IR;
`run_tests.sh` learns expected-error matching for `fail/` and `cgfail/`."

## Context

U1 gave every AST node and every `CompileError` a `SourceLocation
{file, line, col}`. But the location is not yet used in the user-facing
message: `CompileError::getMessage()` still emits
`Compiler Error in <cpp-file>:<cpp-line>\n<message> at line: <line>`
(`qcc.cpp:28-37`) — it leads with the compiler's own C++ `__FILE__:__LINE__`,
omits the column and filename of the user's source, and never uses the
canonical `<file>:<line>:<col>: error:` shape that every later unit's
diagnostics and the epic's done-condition depend on.

Three more gaps this unit closes:

- **Noise.** A successful compile is not silent. `qcc.cpp:717` prints
  `Completed parse` on every parsed module; the lexer can dump per-token
  `Symbol …` lines (`FileLexer.cpp:315-321`, gated by `mTraceEnabled`); and
  AST/token dumps exist. REQ-003 requires a good compile to be byte-silent by
  default, with all such output moved behind a verbosity flag.
- **Swallowed errors.** The statement-dispatch backtracking
  (`QStatement.cpp:70-88`) catches the real parse error from the
  declaration/expression attempts and rethrows a generic `"Unexpected token"`
  with the position reset to the start of the statement — discarding the
  deeper, more specific, correctly located error.
- **No diagnostic assertion in tests.** `run_tests.sh` checks only the exit
  code of `fail/` tests (`run_tests.sh:96-104`) and discards stderr
  (`2>/dev/null`, line 74). A test cannot assert *which* error was produced,
  so a program that fails for the wrong reason still "passes." The later
  units (U3–U8) that add `fail/sema/` audit programs need a harness that
  asserts the diagnostic content; this unit builds it.

This unit is **diagnostics plumbing and test-harness only**. It introduces no
semantic checks and changes **no** program's accept/reject status. The only
observable behavior change for existing programs is that already-failing
parses report a cleaner, correctly located, more specific message, and
successful compiles go quiet.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - A rejected program points at the user's source (Priority: P1)

As a BLang author whose program fails to compile, I get exactly one error
line that names the file, line, and column of the offending token and
describes what is wrong — not the compiler's own C++ source coordinates, not
a raw LLVM IR dump, and not a vague "Unexpected token" when a more specific
cause is known.

**Why this priority**: This is REQ-002 itself and the format contract every
later unit's diagnostics (and the epic done-condition #3 regex) build on.

**Independent Test**: Compile a known-bad fixture and confirm stderr contains
exactly one line matching `^[^:]+\.b:[0-9]+:[0-9]+: error: `, that the
file/line/column point at the offending token, and that no
`Compiler Error in <cpp-file>:<line>` line and no raw LLVM verifier output
appear.

**Acceptance Scenarios**:

1. **Given** a source file with a parse error (e.g., a missing closing
   brace), **When** it is compiled with no extra flags, **Then** stderr
   contains exactly one line of the form
   `<file>:<line>:<col>: error: <message>`, `<file>` is the user's `.b` path,
   and `<line>`/`<col>` are ≥ 1 and identify the offending token.
2. **Given** the same error, **When** the message is inspected, **Then**
   where an offending token is applicable the message includes that token's
   source text (from the lexer), and **Then** no compiler-internal
   `__FILE__:__LINE__` string appears anywhere in the output.
3. **Given** a statement that is neither a valid declaration nor a valid
   expression, **When** it is compiled, **Then** the reported error is the
   deeper, specific cause from the parse attempt (located at the actual
   offending token), not a generic `"Unexpected token"` located at the start
   of the statement — unless no more specific error is available.
4. **Given** `--debug-compiler` is passed, **When** the same bad file is
   compiled, **Then** the compiler-internal `__FILE__:__LINE__` of the throw
   site is additionally shown (for compiler developers), while the primary
   located error line is still present and still first/canonical.

---

### User Story 2 - A successful compile is silent (Priority: P1)

As a BLang author (and as the `bcc` pipeline and CI that shell out to the
compiler), a program that compiles cleanly produces no output unless I ask
for it. No `Completed parse`, no per-token `Symbol …` trace, no AST dump on
stdout or stderr.

**Why this priority**: This is REQ-003 and epic done-condition #4; noisy
success output pollutes `bcc` and defeats machine-checkable quiet-compile
gates.

**Independent Test**: `out=$(build/qcc --parse-only
test_files/pass/func_simple.b 2>&1); test -z "$out"` succeeds and exit code
is 0.

**Acceptance Scenarios**:

1. **Given** a well-formed program, **When** it is compiled with no verbosity
   flag, **Then** combined stdout+stderr is empty and the exit code is 0.
2. **Given** the same program, **When** it is compiled with `-v`, **Then**
   the previously-unconditional diagnostic/trace/AST-dump output (per-token
   `Symbol …`, `Completed parse`, AST/token dumps) is emitted, and the exit
   code is still 0.
3. **Given** the existing `--dump-locations` mode, **When** it is run,
   **Then** its output (the one-line-per-node dump) is unchanged and remains
   the entire stdout of that run (no regression from U1).

---

### User Story 3 - Negative tests assert the diagnostic, not just the exit code (Priority: P1)

As a compiler maintainer, a negative test can declare the error message it
expects, and the test harness fails the test if the compiler exits zero **or**
if the produced diagnostic does not match the declared expectation. This lets
the `fail/sema/` audit suite (built in later units) prove that each program
is rejected *for the right, correctly-located reason*.

**Why this priority**: This is the harness half of REQ-011 and the mechanism
epic done-condition #3 relies on; without it the later audit programs cannot
be asserted.

**Independent Test**: Add an expected-message declaration to a known `fail/`
test, run `run_tests.sh`, and confirm the test passes; then corrupt the
declared pattern and confirm the harness now fails that test.

**Acceptance Scenarios**:

1. **Given** a `fail/` (or `cgfail/`) test that declares an expected-message
   pattern, **When** `run_tests.sh` runs it, **Then** the test passes only if
   the compiler exits non-zero **and** the declared pattern matches the
   compiler's stderr.
2. **Given** a `fail/` test whose declared pattern does **not** match the
   actual stderr, **When** `run_tests.sh` runs it, **Then** the harness
   reports that test as failed (even though the exit code is non-zero).
3. **Given** a `fail/` test with **no** expected-message declaration, **When**
   `run_tests.sh` runs it, **Then** it is judged exactly as today — pass iff
   the compiler exits non-zero (backward compatible).
4. **Given** the whole suite in both build modes, **When** `run_tests.sh`
   runs, **Then** at least 10 existing `fail/` tests carry expected-message
   declarations and pass in the new mode, and the overall suite is green.

---

### Edge Cases

- **Token text for symbolic/EOF tokens**: when the offending token has no
  meaningful text (end-of-file, or a purely structural symbol), the message
  omits the "offending token text" clause rather than printing an empty or
  garbage string; the located `file:line:col: error:` prefix is still
  produced.
- **Errors with no better cause in backtracking**: if both the declaration
  and expression parse attempts fail without a locatable specific error, the
  generic `"Unexpected token"` message is still produced, but located at the
  offending token, not at column 1.
- **`--combine` / multi-module builds**: the error's `<file>` names the
  specific source file in which the error occurred, even when several `.b`
  files are compiled together; quiet-by-default and the single-error contract
  hold across the combined build.
- **Expected-error declaration on a test that unexpectedly passes**: the
  harness treats an exit-zero as a failure regardless of the declared
  pattern (a program expected to be rejected must be rejected first).
- **Both `.expected` file and inline `// EXPECT-ERROR:` present**: the
  harness applies one deterministic precedence rule (documented in the plan)
  rather than matching against both ambiguously.
- **LLVM verifier failure** (LLVM builds only): the compiler prints a concise
  internal-compiler-error line requesting a bug report, not the raw LLVM
  verifier dump; the raw dump is available only under `--debug-compiler`.

## Requirements *(mandatory)*

### Functional Requirements

**Diagnostic format and content (REQ-002)**

- **FR-001**: Every user-facing compile error MUST be printed in exactly the
  form `<file>:<line>:<col>: error: <message>`, where `<file>` is the user's
  source file, and `<line>`/`<col>` come from the offending token's
  `SourceLocation` (both ≥ 1).
- **FR-002**: The error message MUST include the offending token's source
  text (via the lexer's token-text accessor) where a token is applicable, and
  MUST omit that clause cleanly when no token text is meaningful (EOF /
  structural token).
- **FR-003**: User-facing error output MUST NOT contain the compiler's own
  C++ `__FILE__:__LINE__` location, and MUST NOT contain raw LLVM IR verifier
  output. The C++ throw-site coordinates MUST be reachable only when
  `--debug-compiler` is passed.
- **FR-004**: There MUST be a single diagnostic reporting path (one
  `DiagnosticEngine`-style component, owned by the driver) through which the
  parser reports errors, and which the semantic pass (U3+) can reuse without
  changing the format. `CompileError` MAY remain as the parser's control-flow
  (exception) mechanism, but its rendered message MUST go through the single
  path and carry the `SourceLocation`.
- **FR-005**: The reporting component's public shape MUST accommodate a
  severity, a location, and an ordered list of notes (to leave room for the
  deferred diagnostics epic), but this unit emits only single located
  `error:` diagnostics — multi-error recovery, caret/snippet rendering,
  warnings, and `--json` output are explicitly out of scope.

**Deepest-error retention (REQ-002)**

- **FR-006**: In statement-dispatch backtracking, when the
  declaration-then-expression parse attempts both fail, the compiler MUST
  report the most specific located error available from those attempts (the
  "deepest" error), rather than unconditionally replacing it with a generic
  `"Unexpected token"`. The generic message is used only when no more
  specific located error exists.

**Quiet by default (REQ-003)**

- **FR-007**: Compiling a well-formed program with no verbosity flag MUST
  produce zero bytes on stdout and stderr and exit 0. This includes
  suppressing the `Completed parse` line, per-token `Symbol …` traces, and
  AST/token dumps.
- **FR-008**: A verbosity flag (`-v`) MUST re-enable the developer-facing
  output that is silenced by FR-007 (per-token trace, parse-progress lines,
  AST/token dumps), without changing exit codes.
- **FR-009**: The existing `--dump-locations` behavior (U1) MUST be
  unchanged: its node dump remains the entire stdout of a dump run and is not
  gated behind `-v`.

**LLVM verifier failure shape (REQ-002; LLVM builds only)**

- **FR-010**: When LLVM module verification fails, the compiler MUST print a
  concise internal-compiler-error message that requests a bug report, and
  MUST NOT print the raw LLVM verifier text as normal output. The raw text
  MAY be shown only under `--debug-compiler`. (This is the reporting shape;
  the goal of never reaching this path via ill-typed input is later units.)

**Expected-error test harness (REQ-011, harness half)**

- **FR-011**: `run_tests.sh` MUST support a per-test expected-message
  declaration, provided either as a companion `<test>.expected` file or as an
  inline `// EXPECT-ERROR: <pattern>` comment in the `.b` source, for tests
  in `test_files/fail/` and `test_files/cgfail/`.
- **FR-012**: A negative test that declares an expected-message pattern MUST
  pass only if the compiler exits non-zero **and** the declared pattern
  matches the compiler's stderr; otherwise the harness MUST report it as
  failed.
- **FR-013**: A negative test with **no** expected-message declaration MUST
  continue to be judged by exit code alone (pass iff non-zero), preserving
  backward compatibility with the current suite.
- **FR-014**: The harness MUST capture the compiler's stderr (rather than
  discarding it) for negative tests so the pattern can be matched, and MUST
  support asserting the canonical format regex
  `^[^:]+\.b:[0-9]+:[0-9]+: error: ` as an expected pattern (the mechanism
  later `fail/sema/` audit programs use).
- **FR-015**: At least 10 existing `test_files/fail/` tests MUST be given
  expected-message declarations and MUST pass under the new mode in both
  build modes.

**Scope preservation (no behavior drift)**

- **FR-016**: This unit MUST NOT introduce any semantic check and MUST NOT
  change which programs are accepted or rejected. Every currently-passing
  test remains passing; every currently-failing test remains failing (with an
  improved message). `--combine` / multi-module builds and the `bcc`
  pipeline MUST keep working.

### Key Entities

- **DiagnosticEngine**: the single component, owned by the compiler driver,
  that renders and emits diagnostics. Inputs: a severity (error only in this
  unit), a `SourceLocation`, a message string, and an optional ordered note
  list. Output: one canonical `<file>:<line>:<col>: error: <message>` line to
  stderr. Both parser and (future) sema report through it.
- **CompileError** (existing): the parser's exception carrying a
  `SourceLocation`, the message, and the C++ throw-site coordinates. Its
  rendering is routed through the DiagnosticEngine; the C++ coordinates are
  emitted only under `--debug-compiler`.
- **Expected-error declaration**: a per-test artifact — either a
  `<testname>.expected` file next to the `.b` source, or a
  `// EXPECT-ERROR: <pattern>` line in the source — carrying the pattern the
  test's stderr must match.
- **Verbosity/debug flags**: `-v` (developer trace/dump output) and
  `--debug-compiler` (compiler-internal throw coordinates and raw verifier
  text). Neither affects which programs compile.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001** (quiet by default): `out=$(build/qcc --parse-only
  test_files/pass/func_simple.b 2>&1); test -z "$out"` succeeds and the exit
  code is 0.
- **SC-002** (single located error): Compiling a designated known-bad fixture
  with no extra flags produces on stderr exactly one line matching
  `^[^:]+\.b:[0-9]+:[0-9]+: error: `, and produces no line containing
  `Compiler Error in` and no raw LLVM verifier text.
- **SC-003** (correct location, spot-checkable): For at least three distinct
  known-bad fixtures, the `<line>:<col>` in the emitted error identifies the
  offending token in the source (verifiable by reading the fixture).
- **SC-004** (expected-error harness works): Adding an expected-message
  declaration to a `fail/` test makes it pass under `run_tests.sh`; changing
  that declaration to a non-matching pattern makes `run_tests.sh` fail that
  test.
- **SC-005** (coverage): At least 10 existing `test_files/fail/` tests carry
  expected-message declarations and pass in the new mode.
- **SC-006** (both build modes green): Gate A (LLVM build:
  `./run_tests.sh && ./test_codegen.sh`) and Gate B (parse-only build:
  `BUILD_DIR=build-parse ./run_tests.sh`) both exit 0.
- **SC-007** (no accept/reject drift): The set of passing tests and the set of
  failing tests is identical to the pre-U2 baseline (counts unchanged); only
  messages and success-silence differ.

## Assumptions

- The `SourceLocation {file, line, col}` added in U1 is available on
  `CompileError` and on AST nodes and is accurate after backtracking (U1's
  guarantee); this unit consumes it and does not re-derive locations.
- The lexer exposes an accessor for the current/most-recent token's source
  text usable for the "offending token" clause (per the workplan's reference
  to `getSymbolText()`); if the exact accessor differs, the plan selects the
  correct existing one without adding new lexer state.
- "Quiet by default" is measured on combined stdout+stderr for a
  `--parse-only` compile of a passing file; the LLVM build's `.ll` side
  effects are out of the silence check (done-condition #4 uses
  `--parse-only`).
- Removing `bcc`'s stderr grep-filter hack is **optional** in this unit
  (it becomes unnecessary once REQ-003 lands); if not removed here it is
  noted for a later unit and does not block U2.
- The expected-error harness runs the compiler with the same invocation
  `run_tests.sh` already uses per category (parse for `fail/`, codegen for
  `cgfail/`); the exact flag set is fixed in the plan to keep stderr matching
  deterministic.

## Out of Scope

- The semantic-analysis pass and any new type/ownership/concurrency checks
  (U3+).
- The `fail/sema/` audit programs `audit_01..audit_10` themselves (created in
  U4/U5/U7 and completed in U8); this unit only builds the harness they use.
- Multi-error reporting, parser error recovery, caret/snippet rendering,
  warnings, and `--json` diagnostics (deferred diagnostics epic).
- Any change to the legacy Bison/Flex path (`parser.yy`,
  `parse_helpers.cpp`).
