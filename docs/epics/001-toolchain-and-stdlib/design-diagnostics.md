# Design: Diagnostics (multi-error, warnings, `--json`) — Unit U1

**Epic**: [overview.md](overview.md) · **Requirement**: REQ-002

Architecture-level design. The implementer's speckit spec fills in the exact
recovery points and JSON schema; the architect audits it against this.

## Current state (recon, file:line)

- `DiagnosticEngine` (`DiagnosticEngine.h/.cpp`) is a **formatter/sink, not a
  collector**: `report()` writes one `<file>:<line>:<col>: error: <msg>` line to
  a stream immediately (`.cpp:6-23`) and flips `mHasErrors`. No
  `std::vector<Diagnostic>` store. `Severity` enum has a **single value**
  `Error` (`.h:18`); the renderer hardcodes `"error:"` and unconditionally sets
  `mHasErrors=true` (`.cpp:22`). A `Note` struct exists but `notes` is always
  empty. No `--json`.
- **Two error paths, different behavior:**
  - **Parser = first-error-fatal**: `COMPILE_ERROR` throws `CompileError`
    (`CompilerHelpers.h:38`); the only catch is `Module::Parse` (`qcc.cpp:324`)
    which renders one diagnostic and `return nullptr` — parsing halts at the
    first syntax error.
  - **Sema = already multi-error within a module**: `Sema::analyze` reports at
    ~20 sites and continues (`Sema.cpp` various), returning `!mReported`. **But**
    the driver aborts at the first failing *file*: `if (!Sema::analyze(...))
    return -1;` inside the per-file loop (`qcc.cpp:887`).
- Every AST node carries a `SourceLocation` (blang-ast); locations are accurate.

## Design

Three sub-capabilities, in dependency order:

**A. DiagnosticEngine → collector.** Add `std::vector<Diagnostic> mDiags`;
`report()` buffers instead of printing; add `finish()` that renders either human
text or JSON. Every call site already routes through `report()`, so this is
localized. `hasErrors()` becomes "any buffered diag with severity ≥ Error."

**B. Warnings + `-Werror`.** Extend `Severity` (`Warning`, `Error`; keep room for
`Note`); branch the label in the renderer; gate `mHasErrors` on `severity ≥
Error`. `-Werror` promotes warnings to errors at `finish()`. Seed at least one
real warning (candidate: unused variable, or unreachable code after `return` —
a genuine lint the AST+Sema can detect).

**C. `--json`.** A flag in both arg loops (`qcc.cpp:507`, bcc option struct). At
`finish()`, emit a JSON array of `{severity,file,line,col,message,code}` (a
stable `code` per diagnostic class enables tooling/LLM consumption). Must cover
**both** syntax and semantic diagnostics — which flow through different control
paths, so the collector must sit under both.

**D. Multi-error parsing (the hard part).** Panic-mode recovery: at chosen
statement/declaration boundaries, replace the throw with report-then-**resync**
(skip tokens to the next `;`, `}`, or top-level `fn`), so `Module::Parse` no
longer `return nullptr`s on the first error. Plus fix the per-file early-return
(`qcc.cpp:887`) so Sema failures accumulate across all files before a non-zero
exit. Recovery is added at a **bounded** set of resync points (statement,
block, top-level decl) — not every production — to keep it tractable and avoid
cascading phantom errors (cap/dedupe duplicate diagnostics).

## Key decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | Collector, not sink | Prerequisite for `--json` and for not-aborting-on-first-error |
| D2 | Panic-mode recovery at bounded resync points (`;`/`}`/`fn`) | Full grammar recovery is huge; bounded resync gets ~all real multi-error value cheaply |
| D3 | `code` field on every diagnostic (stable slug) | Machine-readable output serves the LLM-codegen goal; enables `#[allow]`-style tooling later |
| D4 | Cap + dedupe cascading errors | Recovery can spew phantom errors; cap (e.g. ≤ 50) + dedupe by (loc, code) |
| D5 | `--json` covers syntax AND semantic diagnostics | They use different paths; the collector must be the single sink for both |

## Area done-gate (contributes to epic done-condition #2)
- A committed `test_files/fail/multi/…` fixture with ≥ 3 independent errors →
  `qcc` reports ≥ 3 `file:line:col:` lines in one run.
- `qcc --json <that fixture>` output parses as JSON and has ≥ 3 objects with the
  schema keys.
- A `warning:` fixture emits a warning (exit 0 without `-Werror`); with
  `-Werror` it exits non-zero.

## Risks
- **Cascading phantom errors** from over-eager recovery → mitigated by bounded
  resync + cap/dedupe (D4).
- **Two control paths** for syntax vs semantic diagnostics → the collector must
  be threaded under both, and `--json` tested against a program with *both* a
  syntax and a semantic error is worthwhile.
