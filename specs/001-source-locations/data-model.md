# Data Model: Source Locations End to End (U1)

**Feature**: specs/001-source-locations/spec.md
**Date**: 2026-07-13

## Entities

### SourceLocation (new value type — `SourceLocation.h`)

| Field | Type | Meaning | Validity |
|-------|------|---------|----------|
| file | std::string | Source file path as given on the qcc command line | non-empty for parsed nodes |
| line | uint32_t | 1-based line of the token's first character | ≥ 1 on every parsed node (0 = "unset", must never escape the parser) |
| col | uint32_t | 1-based column of the token's first character; every consumed character (incl. tab, `\r`) counts 1 | ≥ 1 on every parsed node |

Plain value type. No RefCount, freely copyable. Default-constructed state
(0,0) exists only as "not yet stamped"; FR-004 forbids it on any node
reachable from a successfully parsed module.

### Token position record (extension of `Lexer::SymbolInfo`, FileLexer.h)

| Field | Type | Notes |
|-------|------|-------|
| symbol | int | existing |
| symbolText | std::string | existing |
| line | uint32_t | NEW — line of token's first character, captured at scan time |
| col | uint32_t | NEW — column of token's first character, captured at scan time |

Invariant: positions are recorded exactly once (when the token is scanned
from the file) and are immutable thereafter; replay and backtracking
(`setCurrentPos`) read them back unchanged. This is what makes locations
backtracking-proof (research R1).

### Reader counters (extension of `LexerReader`)

| Field | Type | Notes |
|-------|------|-------|
| mFileName | std::string | NEW — retained from constructor |
| mLine | uint32_t | NEW — starts 1; `popChar()` of `'\n'` increments and resets mCol |
| mCol | uint32_t | NEW — starts 1; every `popChar()` advances by 1 |

Invariant: counters reflect the position of the **next unconsumed
character**. All consumption goes through `popChar`/`popChar(int)`
(research R2), so multi-line strings and block comments are counted
correctly.

### Lexer position API (extension of `Lexer`)

| Member | Meaning |
|--------|---------|
| `getTokenLocation()` | SourceLocation of the token at the current parse position — the token the parser is about to consume (or just peeked). Defined against `mCurrentPos`/`mSymbolList`, NOT the file read-ahead. |
| `getFileName()` | The source file this lexer is scanning. |
| `setTraceEnabled(bool)` | NEW — gates the per-token stdout echo (default: current behavior). See research R7. |
| `getLineNumber()` / `getLinePosition()` | Retained; become token-accurate (delegate to the current token's record). |

### AST node location (extension of `Statement`, `Symbol`, `Type` bases in Type.h)

| Member | Meaning |
|--------|---------|
| `mLocation` : SourceLocation | Location of the construct's **first token**. Stamped at parse time by every `Parse` factory (capture-at-entry, research R4). |
| `setLocation(const SourceLocation&)` / `getLocation()` | Public accessors used by parser, dumper, and downstream units (U2 diagnostics, U3 sema). |

Relationships: every class deriving from `Statement` (all statements and
expressions in Expression.h), from `Symbol` (FunctionDefinition,
VariableDefinition, StructDefinition, EnumDefinition, ProtocolDefinition),
and `Type`/`FunctionType` carries exactly one SourceLocation. Desugared/
synthesized nodes inherit the location of their originating construct.

### CompileError (modified — CompilerHelpers.h)

| Field | Type | Notes |
|-------|------|-------|
| mLocation | SourceLocation | NEW — snapshot of `l.getTokenLocation()` taken by `COMPILE_ERROR` at throw time |
| mMessage | std::string | existing |
| mFilename / mLineno | const char* / int | existing C++ `__FILE__`/`__LINE__`; retained for U2's `--debug-compiler`, not part of the location contract |
| ~~mLexer~~ | Lexer& | REMOVED from the reporting path — reporting must not read the live lexer (FR-005) |

State transition: constructed (location frozen) → propagated → reported.
The reported line number is `mLocation.line` (token-accurate).

### Location dump line (output contract of `--dump-locations`)

```
<file>:<line>:<col> <NodeKind>\n
```

- `<file>` — path exactly as given on the command line.
- `<line>`, `<col>` — decimal, no padding, both ≥ 1.
- `<NodeKind>` — node's C++ class name, `QLang::` prefix stripped
  (e.g. `FunctionDefinition`, `IfStatement`, `ConstInteger`).
- Order — pre-order traversal: parent before children, children in source
  order; per input file, files in command-line order.
- The dump is the **entire** stdout of a `--dump-locations` run
  (research R7); exit 0 on successful parse.

### Golden files (new test assets)

| File | Locks |
|------|-------|
| `test_files/golden/func_simple.locations` | dump of `test_files/pass/func_simple.b` |
| `test_files/golden/match_basic.locations` | dump of `test_files/pass/match_basic.b` (backtracking-heavy constructs) |

Byte-exact regression locks for: position convention (1-based, tab = 1),
node-location = first-token rule, pre-order dump order, NodeKind names.
Hand-verified once at creation (research R8).
