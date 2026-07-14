# Research: Diagnostics Engine and Expected-Error Harness (U2)

Decisions that resolve the plan's open choices. Each is Decision / Rationale /
Alternatives. Grounded in the current tree (paths verified 2026-07-13).

## R1 — DiagnosticEngine shape and ownership

**Decision**: Add `DiagnosticEngine.h`/`.cpp` in the `QLang` namespace. A
`Diagnostic` value carries `{ Severity severity; SourceLocation location;
std::string message; std::vector<Note> notes; }` where `Note` is
`{ SourceLocation location; std::string message; }`. `enum class Severity {
Error }` (only `Error` used now; the enum exists so warnings/notes can be
added later without an API break). The engine exposes
`void report(const Diagnostic&)` and a convenience
`void error(const SourceLocation&, const std::string& message)`; it renders to
an injected `std::ostream&` (default `std::cerr`) and tracks a `bool
hasErrors()`. The compiler driver (`qcc.cpp` `main`) owns exactly one engine
instance and passes/uses it at the top-level parse-catch.

**Rationale**: Matches design.md decision 2 (one path, driver-owned) and
decision 5 (severity + location + notes present but unused features not
built). Injecting the ostream keeps it testable and lets `--combine` reuse one
engine across modules.

**Alternatives**: (a) A global function `reportError(loc,msg)` — rejected: no
place to grow severity/notes, and a global hides ownership. (b) Make
`CompileError` render itself — rejected: that keeps two renderers and is what
we are removing.

## R2 — CompileError rendering path

**Decision**: Keep `CompileError` exactly as U1 left it (carries
`SourceLocation` + throw-site `__FILE__/__LINE__`). Remove the located-message
formatting from `CompileError::getMessage()` in `qcc.cpp:28-37`; instead, the
top-level catch in `Module::Parse`/driver builds a `Diagnostic` from
`err.getLocation()` + `err.getMessage()` (message body only, no C++ coords)
and calls `engine.report(...)`. Under `--debug-compiler`, additionally emit a
second line with `err.getInternalFile()`/`getInternalLine()`.

**Rationale**: `CompileError` remains the parser's control-flow mechanism
(design seam: parser shape unchanged) while all rendering funnels through the
engine (FR-004). `getMessage()` becomes the raw message body; the located
prefix is the engine's job — one renderer.

**Alternatives**: Threading the engine into every `Parse` factory to report
inline — rejected: violates "parser shape stays" and multiplies the number of
report sites; the single top-level catch is sufficient because the parser
already surfaces exactly one `CompileError` per failed compile.

## R3 — Offending-token text

**Decision**: The lexer already exposes `getSymbolText()` (returns
`mMatchString`, `FileLexer.h:43`, `Lexer.h:19`). The "offending token" clause
is already embedded by existing `COMPILE_ERROR` call sites where relevant
(many messages already quote the token). U2 does **not** re-architect messages;
it guarantees (a) the located prefix, and (b) that when a message is a bare
generic like `"Unexpected token"`, the deepest-error retention (R4) surfaces a
more specific message where one exists. New messages this unit writes for the
ICE/verifier path do not need token text.

**Rationale**: Minimizes churn and honors FR-002 without rewording ~215
existing messages (out of scope; U3+ will refine specific messages). The
"omit cleanly when no token text" edge case is satisfied because the located
prefix is always produced from `SourceLocation`, independent of token text.

**Alternatives**: Systematically appending `getSymbolText()` to every message —
rejected: risks changing many message bodies (accept/reject unaffected but
noisy diffs and possible double-quoting), and the offending token is not
always the current lexer token after backtracking.

## R4 — Deepest-error retention in QStatement backtracking

**Decision**: In `QStatement.cpp:70-88`, the `default:` branch tries
`VariableDeclaration::Parse`, then on failure resets and tries
`Expression::Parse`. Today both failures are caught and replaced with
`COMPILE_ERROR(l, "Unexpected token")` at the reset position. Change: capture
the two candidate `CompileError`s and, on total failure, rethrow the one with
the **greater source position** (the parse attempt that consumed further —
compared by `SourceLocation` line then col). Only if neither carries a usable
location fall back to the generic `"Unexpected token"` located at the
statement's first token (not column 1).

**Rationale**: FR-006. "Deepest" = furthest-progressing parse, the standard
heuristic for recursive-descent error selection. Comparing by
`SourceLocation` is well-defined now that U1 stamps accurate positions and
`CompileError` snapshots them.

**Alternatives**: Track a global "max error" across the whole parse — rejected:
larger change, and the statement dispatcher is the one documented swallow
site (workplan §U2). A depth counter instead of source position — rejected:
source position is already available and is what the user sees.

## R5 — Quiet-by-default and the `-v` flag

**Decision**: Add a `bool verbose` driven by `-v` (long form `--verbose`) in
`qcc.cpp` arg parsing. Gate behind `verbose`: the `cout << "Completed parse"`
line (`qcc.cpp:717`), any AST/token dump prints, and the lexer's
`setTraceEnabled(...)` (drive it from `verbose`, default `false` — it is
already `false` in the dump-locations path and effectively off, but make the
default explicit and `-v` the only enabler). `--dump-locations` output is
produced by `LocationDumper` after restoring the real `cout` buffer and is
**not** gated (FR-009). `bcc` passes no `-v` so it inherits silence.

**Rationale**: FR-007/008/009. The existing stdout-discard trick is only used
in dump-locations mode; the simplest correct fix is to not print the
developer lines at all unless `-v`.

**Alternatives**: Keep redirecting stdout to a null sink for normal compiles —
rejected: it hides genuine output too and is fragile; explicit gating is
clearer and is what the spec asks for.

## R6 — `--debug-compiler` flag

**Decision**: Add `bool debugCompiler` driven by `--debug-compiler`. When set:
(a) the diagnostic emission appends the C++ throw-site `file:line` line from
`CompileError`; (b) on LLVM verifier failure the raw verifier string is
printed after the ICE line. When unset, neither appears. The flag is
independent of `-v`.

**Rationale**: FR-003/010. Keeps compiler-internal detail reachable for
maintainers without ever leaking into normal output.

**Alternatives**: Fold it into `-v` — rejected: `-v` is user-facing verbosity
(traces/dumps); C++ internals are a different audience and the epic names a
separate `--debug-compiler` flag.

## R7 — LLVM verifier failure message

**Decision**: At the two `codegen.verify()` failure sites (`qcc.cpp:820-823`,
`885-887`), replace `cerr << "Module verification failed…"` with a concise
line via the engine/`cerr`: `internal compiler error: generated IR failed
verification; please report this bug` (no `file:line:col` prefix — it is not a
user source error), and print the raw verifier text only under
`--debug-compiler`. This is behind `BLANG_HAS_LLVM` (already is).

**Rationale**: FR-010. Constitution III: raw LLVM verifier output is never
normal user output. Full elimination of the *cause* (ill-typed IR) is U4+;
U2 fixes the *reporting shape* only.

**Alternatives**: Assert/abort — rejected: too blunt for a shipped compiler
and loses the bug-report ask; a clean ICE line with non-zero exit is enough.

## R8 — Expected-error harness in run_tests.sh

**Decision**: Extend `run_test()` for the `fail`/`cgfail` categories:
1. Capture stderr (stop discarding via `2>/dev/null`; keep stdout separate).
2. Resolve an expected pattern with precedence: a companion file
   `<path>.expected` (same dir, test filename + `.expected`) **wins**; else an
   inline `// EXPECT-ERROR: <pattern>` comment scanned from the `.b` source;
   else **none**.
3. Judgement: with a pattern → PASS iff `exit_code != 0` AND stderr matches
   the pattern (`grep -Eq`); without a pattern → PASS iff `exit_code != 0`
   (unchanged, FR-013).
4. The compiler is invoked per the category's existing mode. To make stderr
   deterministic and independent of LLVM, `fail`/`cgfail` negative runs use
   `--parse-only`-consistent invocation where the error is a parse/sema error;
   codegen-only failures (`cgfail`, LLVM builds) keep their current invocation.
   The exact flag set is fixed in tasks; the harness must match the same
   stderr the epic done-condition checks (`build/qcc --parse-only <file>` for
   sema tests in later units).

**Rationale**: FR-011..014. File-vs-inline precedence removes the "both
present" ambiguity (spec edge case). `grep -Eq` gives ERE patterns, enough for
the canonical format regex `^[^:]+\.b:[0-9]+:[0-9]+: error: ` and per-message
substrings.

**Alternatives**: Only `.expected` files — rejected: inline comments keep the
expectation next to the tiny fixture and are convenient for the audit
programs. Only inline comments — rejected: some patterns (multi-line/quoting)
are cleaner in a file; supporting both with clear precedence is low cost.

## R9 — Which ≥10 fail/ tests to annotate, and invocation determinism

**Decision**: Annotate a spread of existing `test_files/fail/` parse tests
whose messages are stable today (e.g. `missing_brace.b`, `missing_paren.b`,
`c_style_func.b`, `for_c_style.b`, `duplicate_func.b`, `undefined_func.b`,
`undefined_var.b`, `enum_missing_brace.b`, `struct_missing_brace.b`,
`test_missing_name.b`, plus a couple more) — at least 10. Each declaration
asserts, at minimum, the canonical located-error regex
`^[^:]+\.b:[0-9]+:[0-9]+: error: `; where the message body is stable, the
declaration also asserts a distinctive substring. Final selection is made
during implementation by running the U2 compiler and copying the actual
emitted message, so patterns cannot drift from reality.

**Rationale**: SC-005/FR-015. Asserting the format regex for all of them
guarantees REQ-002 coverage; adding body substrings where stable strengthens
the assertion without coupling to volatile wording. Deriving patterns from
real output prevents brittle guesses.

**Alternatives**: Hand-writing exact full-message equality — rejected: brittle;
substring/regex is the norm for compiler test suites and matches how the
epic's done-condition phrases its checks.
