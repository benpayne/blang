# Tasks: Source Locations End to End (U1)

**Input**: Design documents from `specs/001-source-locations/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md,
contracts/dump-locations-cli.md, quickstart.md

**Tests**: This unit's tests ARE the two committed golden-location files
plus the regression gates (spec FR-007/FR-010); no new pass/fail/codegen
tests are required because accepted/rejected behavior is unchanged
(plan.md Constitution Check, Principle II).

**Organization**: Grouped by the spec's user stories. US1 = nodes carry
locations (+dump/goldens), US2 = backtracking accuracy, US3 =
CompileError carries a location.

## Format: `[ID] [P?] [Story] Description`

## Phase 1: Setup

- [ ] T001 Create `SourceLocation.h` at repo root: `struct SourceLocation
      { std::string file; uint32_t line = 0; uint32_t col = 0; }` per
      data-model.md; PascalCase header, tabs, no RefCount.
- [ ] T002 Add `test_files/golden/` directory (empty placeholder is fine
      until T017 commits goldens).

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: accurate per-token positions in the lexer — every user story
depends on this.

**⚠️ CRITICAL**: no story work until this phase is complete.

- [ ] T003 `FileLexer.h` + `LexerReader.cpp`: LexerReader retains
      `mFileName` and counts `mLine` (starts 1, incremented when `popChar`
      consumes `'\n'`) and `mCol` (starts 1, +1 per consumed char, reset
      to 1 after newline); add getters. `popChar(int)` loops through the
      counting path. (research R2)
- [ ] T004 `FileLexer.h` + `FileLexer.cpp`: widen `SymbolInfo` to
      `{symbol, symbolText, line, col}`; in `getSymbolFromFile()` snapshot
      reader line/col at the start of token recognition (after
      whitespace/comment skipping, before the first matching char is
      consumed) and store the snapshot when pushing into `mSymbolList`
      (`getSymbolInternal`). (research R1/R2 — note the whitespace loop
      structure: the snapshot must be of the token's first character, so
      capture at each loop iteration before attempting matches)
- [ ] T005 `FileLexer.h` + `FileLexer.cpp`: add
      `Lexer::getTokenLocation()` returning `SourceLocation{file, line,
      col}` of the token at the current parse position (the next token
      the parser will consume; consistent across `peekSymbol`/`getSymbol`
      and correct after `setCurrentPos`); add `getFileName()`; make
      `getLineNumber()`/`getLinePosition()` delegate to the current token
      record. Wire the filename from `qcc.cpp` (which owns the path
      string) into LexerReader/Lexer construction.
- [ ] T006 `FileLexer.h` + `FileLexer.cpp`: add
      `Lexer::setTraceEnabled(bool)` (default preserves today's output)
      gating the per-token `std::cout` echo in `getSymbolInternal()`
      (FileLexer.cpp:321-324). Default-mode output must be unchanged.
      (research R7)

**Checkpoint**: lexer unit-testable by hand — `getTokenLocation()` is
accurate after backtracking.

## Phase 3: User Story 1 — Every AST node knows where it came from (P1) 🎯 MVP

**Goal**: SourceLocation on every parsed node; `--dump-locations`;
committed goldens.

**Independent Test**: golden diffs exit 0; zero `line 0`/`col 0` entries
(quickstart.md steps 1–4).

- [ ] T007 [US1] `Type.h`: include `SourceLocation.h`; add protected
      `SourceLocation mLocation` + public `setLocation()`/`getLocation()`
      to `Statement` (Type.h:28), `Symbol` (Type.h:81), and `Type`
      (Type.h:39). House style: `m`-prefix, tabs. (research R3)
- [ ] T008 [US1] Stamp locations in every `Parse` factory across the
      parser: capture `SourceLocation loc = l.getTokenLocation();` at
      entry and `setLocation(loc)` on every node constructed. Files:
      QBlock.cpp, QBreakContinue.cpp, QEnumDefinition.cpp,
      QExpression.cpp, QForInStatement.cpp, QFunctionDefinition.cpp,
      QReturnStatement.cpp, QStatement.cpp, QStructDefinition.cpp,
      QType.cpp, QVariableDefinition.cpp, QSpawnStatement.cpp,
      QEventHandler.cpp, QTestBlock.cpp, QAssertStatement.cpp,
      QQueryExpression.cpp, QLambdaExpression.cpp, QMatchExpression.cpp,
      QFieldAccessExpression.cpp, QImplBlock.cpp (and any other Q*.cpp
      with a Parse). Rules: node location = construct's first token;
      binary-operation nodes take the first operand's first token;
      desugared/synthesized nodes inherit the originating construct's
      location — no node may keep line/col 0. (research R4, spec FR-003/004)
- [ ] T009 [P] [US1] Create `LocationDumper.h`/`LocationDumper.cpp`
      (BmodEmitter precedent, QLang namespace): pre-order walker over
      parsed module(s) printing `<file>:<line>:<col> <NodeKind>` per node
      to a `std::ostream`; children in source order; NodeKind =
      `typeid`-demangled class name (`abi::__cxa_demangle`) with
      `QLang::` stripped; `dynamic_cast` dispatch covering every node
      class in Expression.h/Type.h (unknown nodes still print their kind,
      recursion just stops). (research R6; contract
      contracts/dump-locations-cli.md)
- [ ] T010 [US1] `qcc.cpp`: add `--dump-locations` flag (argv chain at
      qcc.cpp:438-470) + `--help` text; implies parse-only; suppresses
      non-dump stdout for the run (call `setTraceEnabled(false)`, skip
      `import ...`/AST dump/`Completed parse` prints); invoke
      LocationDumper per input file in command-line order; exit 0 on
      success. Works identically without LLVM (no `BLANG_HAS_LLVM`
      guard). (research R7, spec FR-006/FR-009)
- [ ] T011 [US1] `CMakeLists.txt`: add `LocationDumper.cpp` to the `qcc`
      target sources (both LLVM and non-LLVM configurations).
- [ ] T012 [US1] Generate the two dumps, hand-verify EVERY line's
      line/col by eye against `test_files/pass/func_simple.b` and
      `test_files/pass/match_basic.b` (≥ 5 distinct node kinds — spec
      SC-005), then commit `test_files/golden/func_simple.locations` and
      `test_files/golden/match_basic.locations`. (research R8)

**Checkpoint**: quickstart steps 1–4 pass (golden diffs, zero-location
grep, determinism, build-mode parity).

## Phase 4: User Story 2 — Locations accurate despite backtracking (P2)

**Goal**: prove replay/backtracking positions are exact.

**Independent Test**: match_basic golden contains backtrack-heavy
constructs whose hand-verified positions match.

- [ ] T013 [US2] Verify by hand (and fix if wrong): positions of nodes
      reached through parser rewind paths — statement dispatch
      (QStatement.cpp backtracking, `setCurrentPos`) and match arms in
      `test_files/pass/match_basic.b` — match the by-eye source count in
      the committed golden. Any mismatch is a bug in T004/T005 snapshot
      logic; fix there, regenerate goldens, re-verify. (spec US2)
- [ ] T014 [US2] Confirm `getLineNumber()` (used by error reporting) is
      token-accurate after backtracking: force a parse error after a
      rewind (e.g., `test_files/fail/match_missing_brace.b`) and check
      the reported line matches the offending token by eye. Record the
      observation in the PR description.

**Checkpoint**: backtracking accuracy demonstrated and locked by goldens.

## Phase 5: User Story 3 — Parse errors carry a location value (P3)

**Goal**: `CompileError` snapshots location at throw; reporting stops
reading the live lexer.

**Independent Test**: fail-suite behavior unchanged (41 non-zero exits);
reported lines token-accurate.

- [ ] T015 [US3] `CompilerHelpers.h`: `CompileError` gains
      `SourceLocation mLocation` + `getLocation()`; `COMPILE_ERROR(l,
      message)` macro captures `(l).getTokenLocation()` at throw; remove
      the `Lexer&` member from the reporting path (keep C++
      `__FILE__`/`__LINE__` fields for U2's `--debug-compiler`). All
      ~215 call sites compile unchanged. (research R5, spec FR-005)
- [ ] T016 [US3] `qcc.cpp:27-33`: `CompileError::getMessage()` reads the
      stored `mLocation` (token-accurate line) instead of
      `mLexer.getLineNumber()`; keep the existing message shape
      (reformatting is U2). Verify a few `fail/` fixtures by eye.

**Checkpoint**: all three stories complete.

## Phase 6: Polish & Gates

- [ ] T017 [P] Update `CLAUDE.md`: add `--dump-locations` to the qcc CLI
      features paragraph; note per-token location tracking + golden files
      in the testing section. (constitution Principle I)
- [ ] T018 Self-review the diff for house style (tabs, Allman,
      `m`-prefix, spaces inside parens) and for accidental behavior
      changes (default-mode stdout must be unchanged — research R7).
- [ ] T019 Run the per-unit gates (evaluation.md) and fix failures:
      ```bash
      # Gate A — LLVM build; full suites
      cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
      cmake --build build -j"$(nproc)"
      ./run_tests.sh
      ./test_codegen.sh

      # Gate B — parse-only build; parse suite
      cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF
      cmake --build build-parse -j"$(nproc)"
      BUILD_DIR=build-parse ./run_tests.sh
      ```
- [ ] T020 Run the U1-specific checks (evaluation.md §Unit-specific
      checks) and fix failures:
      ```bash
      ./build/qcc --dump-locations test_files/pass/func_simple.b \
        | diff - test_files/golden/func_simple.locations
      ./build/qcc --dump-locations test_files/pass/match_basic.b \
        | diff - test_files/golden/match_basic.locations
      # zero-location check
      for f in func_simple match_basic; do
        ./build/qcc --dump-locations test_files/pass/$f.b \
          | grep -E ':0:[0-9]+ |:[0-9]+:0 ' && exit 1; done; true
      # determinism + build-mode parity
      diff <(./build/qcc --dump-locations test_files/pass/match_basic.b) \
           <(./build/qcc --dump-locations test_files/pass/match_basic.b)
      diff <(./build/qcc --dump-locations test_files/pass/match_basic.b) \
           <(./build-parse/qcc --dump-locations test_files/pass/match_basic.b)
      ```

## Dependencies & Execution Order

- Phase 1 (T001–T002) → Phase 2 (T003–T006, strictly ordered T003→T004→
  T005; T006 independent after T004) → user stories.
- US1 (T007→T008; T009 [P] parallel with T007/T008; T010 after
  T006+T009; T011 after T009; T012 last) — MVP.
- US2 (T013–T014) needs US1's goldens; US3 (T015–T016) needs only
  Phase 2 (can run parallel to US1/US2 — different files, except
  qcc.cpp shared by T010/T016: serialize those two).
- Phase 6 (T017–T020) after all stories; T017 [P] anytime after T010.

## Parallel Opportunities

- T009 (new files) alongside T007/T008.
- T015/T016 (CompilerHelpers.h, qcc.cpp error path) alongside T013/T014.
- T017 (CLAUDE.md) alongside any late task.

## Implementation Strategy

MVP = Phases 1–3 (US1): locations exist, dump works, goldens lock the
convention. US2 is verification hardening on top; US3 is a small,
mostly-independent slice. Gates (T019/T020) are the exit criteria the
reviewer re-runs independently in Phase 4 of the unit lifecycle.
