# Data Model: Diagnostics Engine and Expected-Error Harness (U2)

This unit adds one small in-memory component and one test-declaration
convention. No persistent storage, no schema.

## Entities

### Severity (enum)

```
enum class Severity { Error };
```

- `Error` — the only severity emitted in U2. The enum exists so a future
  diagnostics epic can add `Warning`/`Note` without changing the engine API
  (design.md decision 5). No behavior keys off severity yet beyond rendering
  the literal `error:`.

### SourceLocation (existing, from U1)

`{ std::string file; uint32_t line; uint32_t col; }` — reused unchanged.
Invariants relied on: `line >= 1` and `col >= 1` for any real token; `file` is
the user's source path. Produced by the lexer (`getTokenLocation()`),
snapshotted onto `CompileError` at throw time.

### Note

```
struct Note { SourceLocation location; std::string message; };
```

- A secondary located remark attached to a diagnostic (e.g., a future
  "moved here" note for U6). **Defined but unused in U2** — the vector is
  always empty. Present so the API does not need to change later.

### Diagnostic

```
struct Diagnostic {
    Severity              severity;   // Error in U2
    SourceLocation        location;   // where the error points
    std::string           message;    // body, no location prefix, no C++ coords
    std::vector<Note>     notes;      // empty in U2
};
```

- `message` is the human sentence only — the `<file>:<line>:<col>: error: `
  prefix is added by the engine at render time, never stored in `message`.

### DiagnosticEngine

Owned by the compiler driver (`qcc.cpp` `main`), one instance per process
(shared across modules in `--combine`).

State:
- `std::ostream& mOut` — sink, default `std::cerr` (injectable for testing).
- `bool mHasErrors` — set true on first error reported.
- `bool mDebugCompiler` — when true, also render compiler-internal detail.

Operations:
- `report(const Diagnostic&)` — render one diagnostic (see contract
  `contracts/diagnostic-format.md`) and set `mHasErrors`.
- `error(const SourceLocation&, const std::string& message)` — convenience
  that builds an `Error` `Diagnostic` with empty notes and reports it.
- `reportCompileError(const CompileError&)` — adapter used by the top-level
  parse-catch: builds a `Diagnostic` from the error's location + message body;
  when `mDebugCompiler`, appends the throw-site `file:line`.
- `bool hasErrors() const`.

Render rules (full contract in `contracts/diagnostic-format.md`):
- Line 1 (always): `<location.file>:<location.line>:<location.col>: error: <message>`.
- Under `mDebugCompiler` only: an additional line exposing the C++ throw-site
  and (LLVM verifier path) the raw verifier text.
- Notes (none in U2) would render as subsequent `note:` lines — not exercised.

### Expected-error declaration (test convention)

Attached to a `test_files/fail/` or `test_files/cgfail/` test. Two carriers,
with fixed precedence:

1. **Companion file** `<test>.expected` — full path is the `.b` path with
   `.expected` appended (e.g. `test_files/fail/missing_brace.b.expected`) OR
   the basename-swap form fixed in tasks. Contains one ERE pattern (first
   non-empty, non-`#` line) matched against stderr with `grep -Eq`.
   **Takes precedence** when present.
2. **Inline comment** `// EXPECT-ERROR: <pattern>` — a line in the `.b` source;
   `<pattern>` (trimmed) is the ERE matched against stderr.

Absence of both → legacy exit-code-only judgement (backward compatible).

Semantics table:

| exit code | pattern present? | stderr matches? | verdict |
|-----------|------------------|-----------------|---------|
| 0 (parsed OK) | any | any | FAIL (expected rejection) |
| ≠ 0 | no | — | PASS |
| ≠ 0 | yes | yes | PASS |
| ≠ 0 | yes | no | FAIL (wrong diagnostic) |

## Relationships

- `CompileError` (parser control-flow) → adapted into a `Diagnostic` by
  `DiagnosticEngine::reportCompileError`. `CompileError` is not replaced.
- Parser and (future, U3+) semantic pass → both report through the single
  `DiagnosticEngine`. Only the parser path is wired in U2.
- `run_tests.sh` → reads the expected-error declaration and matches it against
  the `DiagnosticEngine`'s stderr output. The canonical format regex is the
  bridge: the engine produces it (FR-001), the harness asserts it (FR-014).

## Non-entities (explicitly not modeled in U2)

- Multi-diagnostic buffering / ordering, caret/snippet source rendering,
  warning severities, `--json` serialization — deferred (design.md decision 5).
- Any typed-AST or semantic result — U3+.
