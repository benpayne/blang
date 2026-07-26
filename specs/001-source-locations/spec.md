# Feature Specification: Source Locations End to End (U1)

**Feature Branch**: `epic/blang-ast/u1-source-locations`

**Created**: 2026-07-13

**Status**: Draft

**Epic**: blang-ast (docs/epics/blang-ast/) — Unit U1, covers **REQ-001**

**Input**: User description: "U1 — Source locations end to end. Every AST node
carries a source location (file, line, column). Store the source filename in
the lexer; make column tracking real (charPos is dead); add a SourceLocation
{file, line, col} to the AST base captured at parse time in every Parse
method; CompileError carries a SourceLocation instead of leaning on the live
lexer. Deliver a --dump-locations flag with two committed golden files."

## Context

BLang's AST carries no source positions today. The lexer tracks a line
counter only; `charPos` is initialized once and never incremented; the source
filename is discarded after the file is opened; `CompileError` formats the
compiler's own C++ `__FILE__:__LINE__` and reads the line number from the
*live lexer* at report time. Additionally, the lexer pre-tokenizes into a
symbol list that the parser replays with backtracking (`getCurrentPos`/
`setCurrentPos`), so the lexer's line counter reflects the file read-ahead
position — not the token the parser is actually looking at. Error line
numbers are therefore approximate today and become wrong whenever the parser
has backtracked.

This unit is the foundation of the blang-ast epic: U2 (diagnostics engine)
and U3+ (semantic analysis) consume the locations introduced here. Nothing in
this unit changes what programs are accepted or rejected.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Every AST node knows where it came from (Priority: P1)

As a compiler developer (and, downstream, as a BLang user receiving
diagnostics), every node the parser builds must record the source file, line,
and column of the token that begins the construct, so that any later phase
can point at the user's code exactly.

**Why this priority**: This is REQ-001 itself and the epic's foundation;
every diagnostic in U2–U8 depends on it.

**Independent Test**: Parse a known file with `--dump-locations` and compare
against a committed golden file; verify no node reports line 0 or column 0.

**Acceptance Scenarios**:

1. **Given** `test_files/pass/func_simple.b`, **When**
   `build/qcc --dump-locations test_files/pass/func_simple.b` is run,
   **Then** it prints one `<file>:<line>:<col> <NodeKind>` line per AST node
   in a deterministic order and the output is byte-identical to
   `test_files/golden/func_simple.locations` (diff exits 0).
2. **Given** `test_files/pass/match_basic.b`, **When** the same command is
   run against `test_files/golden/match_basic.locations`, **Then** diff
   exits 0.
3. **Given** either dump, **When** any line is inspected, **Then** its line
   number ≥ 1 and column number ≥ 1 (no unset/zero locations).
4. **Given** a construct that begins mid-line (e.g., an initializer
   expression after `=`), **When** its node is dumped, **Then** the column
   points at that construct's first token, not at column 1.

---

### User Story 2 - Locations are accurate despite parser backtracking (Priority: P2)

The parser speculatively parses and rewinds (symbol-list replay with
`getCurrentPos`/`setCurrentPos`). Locations captured on nodes and errors must
correspond to the token at the parser's *current* position, not to how far
the lexer has read ahead in the file.

**Why this priority**: Without this, locations exist but lie — worse than
none. This is the main correctness risk in the unit.

**Independent Test**: Golden files include constructs whose parse path
involves backtracking (e.g., statement dispatch in `match_basic.b`);
locations in the goldens are hand-verified against the source once, then
locked.

**Acceptance Scenarios**:

1. **Given** a construct reached only after the parser rewinds, **When** its
   node's location is dumped, **Then** the line/column match the construct's
   first token as counted by eye in the source file.
2. **Given** a parse error thrown after backtracking, **When** the error is
   reported, **Then** the line number reported corresponds to the offending
   token (token-accurate), not the lexer's read-ahead position.

---

### User Story 3 - Parse errors carry a location value (Priority: P3)

`CompileError` carries a `SourceLocation` (file, line, column) captured at
throw time, so error reporting no longer needs to interrogate the live lexer
and so U2 can format `<file>:<line>:<col>: error: <message>` without further
plumbing.

**Why this priority**: Plumbing for U2; user-visible formatting itself is
U2's scope, not U1's.

**Acceptance Scenarios**:

1. **Given** a file that fails to parse, **When** the error is reported,
   **Then** the reported line number is the offending token's line (the
   existing message shape may remain; its line number becomes
   token-accurate).
2. **Given** all 41 `test_files/fail/` tests, **When** the suite runs,
   **Then** each still exits non-zero (rejection behavior unchanged).

---

### Edge Cases

- **First token of the file**: location must be `1:1` for a construct
  starting at the top of the file.
- **Tabs in source**: a tab advances the column by exactly 1 (columns count
  characters, not visual width). Golden files lock this convention.
- **Multi-line constructs** (a function spanning many lines): the node's
  location is its *first* token; child nodes carry their own locations.
- **Multi-line tokens** (block comments, multi-line strings): tokens after
  the comment/string must have correct line AND column (column resets per
  line inside the token scan).
- **Error at end of file** (e.g., missing `}`): the error location must be a
  valid position at/near EOF, never 0:0.
- **Multiple input files** on one command line (including `--combine` and
  `.bmod` consumption): each node's location names the file it was actually
  parsed from.
- **Nodes synthesized without a fresh token** (e.g., implicit/desugared
  nodes): they must inherit a real location from the construct that produced
  them — never 0:0.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The lexer MUST track, for every token it produces, the source
  file name, 1-based line number, and 1-based column number of the token's
  first character, and MUST report these accurately for the token at the
  parser's current position even after position save/restore (backtracking).
- **FR-002**: The lexer MUST expose the current file/line/column to the
  parser at any point during parsing (a query answering "where is the token
  I'm about to consume / just consumed").
- **FR-003**: Every AST node (every class deriving from the statement/
  expression/definition base) MUST carry a `SourceLocation` value —
  file, line, column — populated at parse time in every `Parse` factory
  method, set to the location of the construct's first token.
- **FR-004**: No parsed AST node may carry an unset location: line ≥ 1 and
  column ≥ 1 for every node reachable from a successfully parsed module.
  Synthesized/desugared nodes inherit the location of their source
  construct.
- **FR-005**: `CompileError` MUST carry a `SourceLocation` captured at throw
  time. Reporting a `CompileError` MUST NOT require access to the live
  lexer. The compiler's own C++ `__FILE__`/`__LINE__` may still be captured
  internally but the user-visible message format otherwise stays as-is
  (reformatting is U2's scope); the line number it shows becomes
  token-accurate.
- **FR-006**: `qcc` MUST gain a `--dump-locations` flag that, after a
  successful parse, prints to stdout exactly one line per AST node in the
  form `<file>:<line>:<col> <NodeKind>`, in a deterministic order (a fixed
  traversal of the AST, stable across runs and rebuilds), then exits 0
  without generating IR. `<file>` is the path as given on the command line;
  `<NodeKind>` is a stable, human-readable node-class name.
- **FR-007**: Two golden files MUST be committed —
  `test_files/golden/func_simple.locations` and
  `test_files/golden/match_basic.locations` — such that
  `build/qcc --dump-locations test_files/pass/func_simple.b | diff - test_files/golden/func_simple.locations`
  and the `match_basic` equivalent both exit 0.
- **FR-008**: The parser's external shape MUST NOT change: `Parse(Lexer&,
  Scope*)` factory signatures may gain location capture internally but the
  recursive-descent structure, `SmartPtr` ownership, and `QLang` namespace
  remain (design.md seam constraint). Accepted/rejected program behavior is
  unchanged.
- **FR-009**: The feature MUST work identically in both build modes (LLVM
  and parse-only); `--dump-locations` has no LLVM dependency.
- **FR-010**: All existing suites MUST remain green: `./run_tests.sh` and
  `./test_codegen.sh` in the LLVM build; `BUILD_DIR=build-parse
  ./run_tests.sh` in the parse-only build.

### Key Entities

- **SourceLocation**: value type of {file, line, column}. File identifies
  the source file the token came from (as named on the command line); line
  and column are 1-based positions of a token's first character.
- **Token position record**: per-token file/line/column stored with each
  tokenized symbol so replay/backtracking returns correct positions.
- **AST node**: any parser-built object; now carries exactly one
  SourceLocation (its first token's).
- **CompileError**: parse-failure carrier; now holds a SourceLocation
  snapshot taken when thrown.
- **Location dump**: deterministic text rendering of the AST's locations,
  one node per line, used for golden-file regression locking.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Both golden-file diffs exit 0 on a clean build:
  `build/qcc --dump-locations test_files/pass/func_simple.b | diff - test_files/golden/func_simple.locations`
  and the `match_basic` equivalent.
- **SC-002**: Zero nodes in either dump report line 0 or column 0
  (mechanically checkable: `grep -E ':0:|:0 ' <dump>` finds nothing).
- **SC-003**: Full suites green in both build modes: `./run_tests.sh` (162
  parse tests) and `./test_codegen.sh` (63 E2E tests) with the LLVM build;
  `BUILD_DIR=build-parse ./run_tests.sh` with the parse-only build — same
  pass counts as the launch baseline.
- **SC-004**: Location dump output is identical across two consecutive runs
  of the same binary on the same input (determinism).
- **SC-005**: Every AST node class in the parser's class hierarchy exposes a
  location; a spot-check of ≥ 5 node kinds across both golden files shows
  line AND column matching a by-eye count of the source.

## Assumptions

- **Column convention**: columns are 1-based and count characters from the
  start of the line; a tab counts as one column. The golden files lock this
  convention; changing it later is a golden-file update, not a breaking
  change.
- **Node location = first token**: a node's location is the position of the
  first token of its construct (e.g., `if` keyword for an IfStatement, the
  left operand's first token for a binary operation). Golden files lock the
  per-node-kind outcome.
- **Dump order**: "deterministic order" is a fixed pre-order (parent before
  children, children in source order) traversal of the parsed module —
  chosen because it is stable and human-checkable. The golden files are the
  arbiter.
- **Dump scope**: `--dump-locations` dumps the AST of each `.b` input file
  in command-line order. The golden tests exercise single-file invocations;
  multi-file behavior just follows the same per-file dump rule.
- **`<NodeKind>` naming**: the existing AST class names (e.g.,
  `FunctionDefinition`, `IfStatement`, `ConstInteger`) are the stable node
  kind names.
- **User-visible error format is NOT re-specified here**: U2 owns
  `<file>:<line>:<col>: error: <message>`. U1 only guarantees the location
  *data* exists and is accurate; the existing error message shape may
  persist through U1 as long as fail-suite behavior (non-zero exit) is
  unchanged.
- **Existing `fail/` tests assert exit codes only** (expected-message mode
  arrives in U2), so a token-accurate line number appearing in an error
  message cannot break the suite.
- **Documentation**: `CLAUDE.md` CLI-features text gains `--dump-locations`
  (constitution Principle I); `docs/language_design.md` is untouched (no
  language semantics change).
- **Golden files are hand-verified once** at creation (columns counted by
  eye against the source) and then serve as regression locks.

## Out of Scope

- Reformatting user-visible error messages (`<file>:<line>:<col>: error:`)
  — U2.
- The DiagnosticEngine, error-message content, quiet-by-default output — U2.
- Any semantic checking — U3+.
- End location / span tracking (only the start location is required).
- Locations for the legacy Bison/Flex path (`parser.yy`, `lexer.l`,
  `parse_helpers.cpp`) — dead code per the epic's non-goals.
