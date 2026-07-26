# Contract: Diagnostic Format

The user-facing rendering contract for compile errors. Enforced by the epic
done-condition regex and by U2's expected-error tests.

## Canonical error line

Exactly one line per rejected compile (single-error design), written to
**stderr**:

```
<file>:<line>:<col>: error: <message>
```

- `<file>` — the user's source file path as passed on the command line (the
  file in which the error occurred, even under `--combine`). Contains no `:`
  before the first field boundary that would break the regex (paths with `:`
  are out of scope; test paths are plain).
- `<line>`, `<col>` — 1-based integers ≥ 1, from the offending token's
  `SourceLocation`.
- literal `: error: ` separator (lowercase `error`, matching the done
  condition regex `^[^:]+\.b:[0-9]+:[0-9]+: error: `).
- `<message>` — a single-line human sentence with no trailing location and no
  compiler-internal C++ coordinates. Where an offending token is meaningful,
  the message may quote the token text; when not (EOF/structural), it omits
  that clause.

### MUST NOT (default mode)

- No `Compiler Error in <cpp-file>:<cpp-line>` line.
- No ` at line: N` suffix.
- No raw LLVM verifier / IR text.
- No per-token `Symbol …` trace, no `Completed parse`, no AST dump.

## Debug mode (`--debug-compiler`)

In addition to the canonical line (which is unchanged and still first), the
compiler MAY emit compiler-internal detail:

```
<file>:<line>:<col>: error: <message>
[debug] thrown at <cpp-file>:<cpp-line>
```

and, on the LLVM verifier path, the raw verifier text after the ICE line
(see below). Debug output is opt-in and never present without the flag.

## Internal compiler error (LLVM verifier failure; LLVM builds only)

When generated IR fails `verify()`:

```
internal compiler error: generated IR failed verification; please report this bug
```

- No `file:line:col` prefix (not a user-source error).
- Exit code is non-zero.
- Raw verifier text is shown only under `--debug-compiler`.

## Verification hooks

- `SC-002`: a designated bad fixture yields exactly one line matching
  `^[^:]+\.b:[0-9]+:[0-9]+: error: ` and no `Compiler Error in` line.
- `run_tests.sh` expected-error mode asserts this regex (and per-test
  substrings) for annotated `fail/` tests.
