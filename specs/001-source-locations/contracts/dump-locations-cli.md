# Contract: `qcc --dump-locations` (U1)

## Invocation

```
qcc --dump-locations <file.b> [<file2.b> ...]
```

- Available in BOTH build modes (LLVM and `BLANG_ENABLE_LLVM=OFF`); no LLVM
  dependency.
- Implies parse-only: no `.ll`/`.o`/`.bmod` output, no codegen.
- Listed in `--help` output.

## Output (stdout)

Exactly one line per AST node of each successfully parsed input file, and
nothing else:

```
<file>:<line>:<col> <NodeKind>
```

Grammar of a line (locked by golden files):

- `<file>` — the input path verbatim as passed on the command line.
- `<line>` — decimal integer ≥ 1 (1-based).
- `<col>` — decimal integer ≥ 1 (1-based; each consumed source character
  counts one column, tabs included).
- single space separator.
- `<NodeKind>` — the node's class name without namespace
  (`FunctionDefinition`, `Block`, `ReturnStatement`, `ConstInteger`, ...).

Order: deterministic pre-order — parent before children, children in
source order; multiple input files dump in command-line order.

## Exit status

- 0 — all inputs parsed; dump complete.
- non-zero — parse error; error text goes to stderr (existing error path),
  stdout contains only the dump lines emitted before the failing file.

## Determinism

Two runs of the same binary on the same input produce byte-identical
stdout. No timestamps, pointers, or environment-dependent content.

## Acceptance commands (from the unit's done-when; run from repo root)

```bash
build/qcc --dump-locations test_files/pass/func_simple.b \
  | diff - test_files/golden/func_simple.locations          # exits 0

build/qcc --dump-locations test_files/pass/match_basic.b \
  | diff - test_files/golden/match_basic.locations           # exits 0

# no unset locations in either dump:
build/qcc --dump-locations test_files/pass/func_simple.b | grep -E ':0:[0-9]+ |:[0-9]+:0 ' && exit 1 || true
build/qcc --dump-locations test_files/pass/match_basic.b  | grep -E ':0:[0-9]+ |:[0-9]+:0 ' && exit 1 || true
```

## Internal API contracts consumed by later units

- `Lexer::getTokenLocation() -> SourceLocation` — position of the token at
  the current parse position; accurate after `setCurrentPos` backtracking.
- `Statement::getLocation()`, `Symbol::getLocation()`, `Type::getLocation()`
  — stamped on every parsed node; line/col ≥ 1.
- `CompileError::getLocation() -> SourceLocation` — frozen at throw time;
  reporting does not touch the lexer. (U2 formats
  `<file>:<line>:<col>: error: <message>` from exactly this value.)
