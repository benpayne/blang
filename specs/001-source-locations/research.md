# Research: Source Locations End to End (U1)

**Feature**: specs/001-source-locations/spec.md
**Date**: 2026-07-13

All unknowns from the Technical Context are resolved below. Code facts were
verified by reading the sources on branch `epic/blang-ast/u1-source-locations`
(base: master @ 1269b95).

## Verified code facts (inputs to the decisions)

- `Lexer` (FileLexer.h/.cpp) pre-tokenizes into `mSymbolList`
  (`SymbolInfo {symbol, symbolText}`) and replays it via `mCurrentPos`;
  the parser backtracks with `getCurrentPos()`/`setCurrentPos()`
  (FileLexer.cpp:292-304). `SymbolInfo` stores **no position**, so
  `getLineNumber()` returns the file read-ahead line, which is wrong for any
  replayed token.
- `charPos` is initialized once (FileLexer.cpp:11) and never incremented;
  `getLinePosition()` is dead.
- All character consumption funnels through `LexerReader::popChar()` /
  `popChar(int)` (LexerReader.cpp) — including keyword `match()`, symbol,
  string/char/number constants, comments, and whitespace skipping.
- `lineno` is incremented in exactly two places: whitespace `'\n'`
  (FileLexer.cpp:573) and `handleComment` (FileLexer.cpp:255). Newlines
  inside multi-line string/char constants do NOT increment it
  (FileLexer.cpp:103,175) — line numbers drift after such tokens today.
- The filename is used to open `LexerReader::mFile` (LexerReader.cpp:3-5)
  and then discarded; neither `Lexer` nor `LexerReader` retains it.
- `getSymbolInternal()` unconditionally prints `Symbol ...` to **stdout**
  for every token (FileLexer.cpp:321-324); `qcc.cpp` prints `import ...`,
  AST dumps (`cout << *def`, qcc.cpp:282), and `Completed parse`
  (qcc.cpp:681) to stdout.
- `CompileError` (CompilerHelpers.h) holds `Lexer&` + C++ `__FILE__`/
  `__LINE__`; `getMessage()` (qcc.cpp:27-33) reads
  `mLexer.getLineNumber()` at *report* time.
- AST bases: `Statement : virtual public RefCount` (Type.h:28) for all
  statements/expressions; `Symbol : virtual public RefCount` (Type.h:81)
  for definitions (FunctionDefinition, StructDefinition, EnumDefinition,
  ProtocolDefinition, VariableDefinition); `Type : virtual public RefCount`
  (Type.h:39). There is no single common AST base below `RefCount`.
- ~215 `COMPILE_ERROR`/`throw CompileError` sites across the parser.
- `BmodEmitter` (BmodEmitter.h) is the house precedent for a standalone
  AST-walking emitter class separate from the nodes.
- `qcc.cpp` flag parsing is a simple `argv` string-compare chain
  (qcc.cpp:438-470) — trivially extensible.

## R1 — Where per-token positions live

**Decision**: Extend `SymbolInfo` to `{symbol, symbolText, line, col}`.
Token positions are captured when the token is first scanned from the file
and stored in the symbol list; replay (`getSymbolInternal` list branch) and
backtracking (`setCurrentPos`) therefore return exact positions for free.
`Lexer` gains accessors that report the position of the token at the
current parse position (the token just returned / about to be returned),
plus the filename.

**Rationale**: The symbol list is the single point every token flows
through, in both scan and replay paths; storing position there is the only
design in which backtracking cannot desynchronize positions (spec FR-001,
User Story 2).

**Alternatives considered**: (a) Recompute positions on demand by re-reading
the file — O(n) per query, fragile with the streaming reader. (b) Keep a
parallel position vector — same content, worse locality/invariants than
widening `SymbolInfo`.

## R2 — Where line/column counting happens

**Decision**: Count in `LexerReader`: `popChar()` advances `col` by 1 per
character consumed; on consuming `'\n'` it increments `line` and resets
`col` to 1. `LexerReader` also retains the filename. `Lexer` snapshots
`reader.line/col` immediately before token recognition begins (after
whitespace/comment skipping, at the first character of the token) and
stores that snapshot into the new `SymbolInfo` fields.

**Rationale**: Every consumption path (keywords, symbols, string/char/
number constants, comments, whitespace) already funnels through
`popChar`; counting there covers multi-line strings and block comments
automatically — fixing the existing drift where newlines inside string
constants are never counted (spec Edge Case "multi-line tokens").
Snapshot-at-token-start gives the token's first character, which is the
location contract for nodes (spec Assumption "node location = first
token").

**Alternatives considered**: Keep counting in `Lexer` at each site that
inspects `'\n'` — misses multi-line constants (the existing bug) and must
be replicated in ~6 scan helpers; rejected as error-prone.

**Convention locked**: line and column are 1-based; every consumed
character (including tab and `'\r'`) advances the column by exactly 1.
Golden files are the arbiter of this convention (spec Assumptions).

## R3 — SourceLocation type and which AST bases carry it

**Decision**: A new lightweight header `SourceLocation.h` defines
`struct SourceLocation { std::string file; uint32_t line = 0; uint32_t
col = 0; }` (value type, no RefCount). `Statement` (Type.h:28) and
`Symbol` (Type.h:81) each gain a protected `SourceLocation mLocation` with
public `setLocation(...)`/`getLocation()`. `Type` nodes also gain it (types
are parsed constructs and later units will diagnose on them). Every
`Parse` factory stamps the node with the lexer's current token location
captured **at entry**, before consuming the construct's first token's
successors.

**Rationale**: There is no common AST base below `RefCount`, and
introducing one would restructure the hierarchy — forbidden by the seam
constraint (parser shape stays). Duplicating one small member across the
two (plus `Type`) bases is the minimal-footprint way to satisfy "every
node type in Expression.h and Type.h" (spec FR-003).

**Alternatives considered**: (a) New common base `AstNode` between
RefCount and Statement/Symbol — restructures the hierarchy, violates the
design.md seam. (b) Put location in `RefCount` — pollutes a generic
utility class used outside the AST. (c) Side-table keyed by node pointer —
lifetime hazards with SmartPtr, and downstream units want `node->getLocation()`
directly.

## R4 — How `Parse` methods capture the location

**Decision**: Each `Parse` factory captures
`SourceLocation loc = l.getTokenLocation();` (position of the *next* token
to be consumed) as its first action, and calls `node->setLocation(loc)` on
the node(s) it constructs. Sub-expressions parsed by helper routines stamp
their own nodes the same way; nodes synthesized during desugaring inherit
the location of the construct that produced them (spec FR-004). Binary
operation nodes take the location of their first operand's first token
(spec Assumption).

**Rationale**: Capture-at-entry is mechanical, uniform across ~30 Q*.cpp
files, and immune to how much lookahead/backtracking the method performs
afterward — the value is already snapshotted.

**Alternatives considered**: Stamp in node constructors by passing
`Lexer&` down — touches every constructor signature (larger, riskier diff)
for no accuracy gain.

## R5 — `CompileError` carries a location

**Decision**: `CompileError` gains a `SourceLocation mLocation` populated
by the `COMPILE_ERROR(l, message)` macro via `l.getTokenLocation()` at
throw time; the `Lexer&` member is dropped from the reporting path (the
reference may remain only if something else needs it — target: remove).
The C++ `__FILE__`/`__LINE__` members stay (U2 will expose them only
under `--debug-compiler`). `getMessage()` keeps its current shape but
reads the line from the stored location — token-accurate, no live-lexer
dependency (spec FR-005). All ~215 throw sites keep the same macro
invocation; only the macro body changes.

**Rationale**: Snapshot-at-throw is exactly what "stop leaning on the
live lexer" means; keeping the macro signature makes the 215 sites a
zero-touch migration.

**Alternatives considered**: Rewriting each site to pass explicit
locations — enormous diff, U1 gains nothing; specific sites can be
refined when U2 reworks messages.

## R6 — `--dump-locations` traversal and NodeKind naming

**Decision**: New `LocationDumper` class (LocationDumper.h/.cpp) following
the `BmodEmitter` precedent: a standalone walker, `dynamic_cast`-dispatched
like CodeGen, that visits the parsed module(s) in **pre-order** (parent
before children, children in source order) and prints
`<file>:<line>:<col> <NodeKind>` per node to stdout. `<NodeKind>` is the
node's C++ class name obtained from `typeid` demangled via
`abi::__cxa_demangle` with the `QLang::` prefix stripped.

**Rationale**: A standalone walker matches house precedent and keeps the
dump out of the node classes. `typeid` naming means *every* node prints a
correct, stable kind name even for a node class the walker's child
recursion doesn't know yet — coverage failures degrade to missing
children, never to wrong names. Names are stable on the Itanium C++ ABI
(GCC/Clang on Linux/macOS — the project's platforms), and the golden
files pin them.

**Alternatives considered**: (a) `virtual getNodeKind()` on every node
class — ~50 mechanical overrides, and a missed override silently reports
the base class name; rejected as more edits for less safety. (b) Reuse
`operator<<` AST printing — not one-line-per-node, not location-bearing.

## R7 — Keeping the dump byte-clean (golden diffs) without doing U2's job

**Decision**: `--dump-locations` implies parse-only and produces *only*
dump lines on stdout. To achieve that in U1: the lexer's per-token echo
(FileLexer.cpp:321-324) becomes conditional on a trace flag
(`Lexer::setTraceEnabled(bool)`, default **unchanged** = on), and qcc's
informational prints (`import ...`, AST dump, `Completed parse`) are
skipped when `--dump-locations` is active. Default-mode output is
otherwise untouched; U2 owns quiet-by-default and `-v` gating globally.

**Rationale**: The unit's own done-when ("prints one `<file>:<line>:<col>
<NodeKind>` line per AST node", golden `diff` exits 0) is unmeetable if
token spew interleaves with dump lines on stdout. This is the minimal
carve-out; flipping defaults stays in U2 (spec Out of Scope). Existing
tests assert exit codes, not output, so no suite impact either way.

**Alternatives considered**: (a) Bake the spew into the golden files —
locks garbage into goldens and guarantees a golden churn in U2. (b) Print
the dump to a file instead of stdout — contradicts the epic workplan's
literal done-when command (`qcc --dump-locations file | diff - golden`).

## R8 — Golden file creation and verification

**Decision**: Golden files `test_files/golden/func_simple.locations` and
`test_files/golden/match_basic.locations` are generated by the new flag,
then **hand-verified once** — every line's line/col checked by eye against
the `.b` source (per spec SC-005, ≥ 5 node kinds spot-checked; in practice
all lines of both files) — and committed. `run_tests.sh` is NOT modified
in U1 (harness changes are U2); the golden diffs run as the unit's gate
commands and as future units' regression locks.

**Rationale**: Golden-generation-then-verify is the only honest way to
bootstrap; the hand-verification requirement guards against locking in a
wrong convention. Keeping run_tests.sh untouched keeps U1's diff minimal
and leaves harness design decisions consolidated in U2.

**Alternatives considered**: Wiring the golden diff into run_tests.sh now —
overlaps U2's harness redesign; the evaluation.md per-unit gate already
runs the diffs explicitly.

## Risks carried into implementation

- **Line-number drift fix changes error text**: files with multi-line
  strings/comments now report *more accurate* line numbers in parse
  errors. `fail/` tests assert exit codes only, so suites stay green.
- **Peek vs. get position semantics**: `peekSymbol()` pre-reads a token
  (`mLastSym`); `getTokenLocation()` must be defined against the token at
  `mCurrentPos` (the next token the parser will consume), and the
  implementation must keep peek/get consistent. Covered by golden files
  containing backtrack-heavy constructs (match arms, statement dispatch).
- **`--combine`/multi-file**: each Lexer instance carries its own
  filename; nodes from different files report their own file (spec Edge
  Case). Dump emits per input file in command-line order.
