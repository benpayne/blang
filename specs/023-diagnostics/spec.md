# Spec: Diagnostics — multi-error, warnings, `--json`, `-Werror`

**Epic**: 001-toolchain-and-stdlib · **Unit**: U1 · **Branch**: `epic/001-toolchain-and-stdlib/u1-diagnostics`
**Covers**: REQ-002 · **Speckit**: `diagnostics` · **Status**: Implemented (gates green; awaiting code review)
**Depends on**: U0 (merged @ 00106d6 — threads `--json` through U0's single flag/arg-loop path)
**Reviewed-by: architect** (Vera; audit self-completed by manager after two runtime
interruptions, disposition recorded). Verdict PASS-WITH-FINDINGS; 0 blocking.
Verified: codegen gated after the file loop (`qcc.cpp:954`, `!parseOnly`) so
Constitution III holds; conforms to `design-diagnostics.md` A/B/C/D + D1–D5;
reject-only Principle-II carve-out correctly invoked; unused-variable seed matches
pinned `unused.b`. Finding folded in: §C tightened — qcc arg loop is the single
authoritative flag site (gates are qcc-direct); bcc forwarding rides existing
qccCmd sites (U0 did not consolidate qccCmd; no new duplication, no U0 bypass).

## Problem (recon, file:line @ master 00106d6)

- `DiagnosticEngine` (`DiagnosticEngine.h/.cpp`) is a **sink, not a collector**:
  `report()` writes `<file>:<line>:<col>: error: <msg>` to `mOut` (std::cerr)
  immediately (`.cpp:6-23`) and sets `mHasErrors`. No `std::vector<Diagnostic>`
  store; `Severity` has a **single value** `Error` (`.h:18`); the renderer
  hardcodes `"error:"`; no `--json`; `Diagnostic` has no `code`.
- **Two error paths, different behavior:**
  - **Parser = first-error-fatal.** `COMPILE_ERROR` throws `CompileError`
    (`CompilerHelpers.h`); the **only** catch wraps the entire `Module::Parse`
    `while` loop (`qcc.cpp:324-331`) and does `return nullptr` — parsing halts at
    the first syntax error.
  - **Sema = multi-error within a module** already (reports at ~24 sites via
    `mDiag.error()` and continues, returning `!mReported`, `Sema.cpp:99+`). **But**
    the driver aborts at the first failing *file*: `Module::Parse` null →
    `return -1` (`qcc.cpp:860`); `Sema::analyze` false → `return -1`
    (`qcc.cpp:887`).
- Every AST node carries an accurate `SourceLocation` (blang-ast). Chatter
  (`cout << "Completed parse"` etc.) is diverted to a discard sink in non-verbose
  mode (`qcc.cpp:723-747`); diagnostics go to `std::cerr`, unaffected.

## Design (four sub-capabilities, dependency order — matches `design-diagnostics.md`)

### A. `DiagnosticEngine` → collector (design D1)
- Add `std::vector<Diagnostic> mDiags`; `report()` **buffers** (push_back)
  instead of printing. Add `void finish()` that renders **all** buffered
  diagnostics once, as either human text (default) or a JSON array (`--json`).
- `hasErrors()` becomes "any buffered diag with effective severity ≥ Error"
  (after `-Werror` promotion at `finish()`).
- `reportCompileError()` and `error()` keep their signatures; they build a
  `Diagnostic` and route through `report()` (now buffering). **Every existing
  call site is unchanged** — the change is localized to the engine.
- `finish()` is called **once**, by the qcc driver, after parse+sema of all
  files and before codegen. On the human path it prints the buffered lines to
  `std::cerr`; nothing else about existing output changes (a clean compile still
  prints nothing).

### B. Warnings + `-Werror` (design B)
- Extend `enum class Severity { Warning, Error }` (keep `Note` room). The
  renderer branches the label (`warning:` / `error:`). `mHasErrors`-equivalent
  gates on `severity >= Error`.
- Add `void warning( const SourceLocation&, const std::string& msg, const std::string& code )`.
- `-Werror` (a `DiagnosticEngine` bool set by the driver): at `finish()`,
  effective severity of every `Warning` is promoted to `Error`, so `hasErrors()`
  returns true and the process exits non-zero. Without `-Werror`, warnings render
  but exit stays 0.
- **Seed warning = unused local variable** (matches the pinned fixture
  `test_files/fail/warn/unused.b`). Detection lives in **Sema** (all build
  modes): for each `VariableDeclaration` in a function body whose declared name
  is **never referenced** by any later `VariableExpression` in that function,
  emit `warning: unused variable '<name>'` (code `unused-variable`). Scope: local
  declarations only (not parameters, not `self`, not globals); a variable written
  but never read still counts as unused (conservative: reference = any
  `VariableExpression` naming it, read or write target). This is a genuine lint
  the existing Sema walk already has the data for (it resolves every
  `VariableExpression`). **Reject-only carve-out (Constitution II)**: the warning
  is proven by the committed `fail/warn/unused.b` fixture + its `.expected`
  pattern and the `-Werror` promotion test — no `codegen_*.b` needed (matches the
  U1 carve-out in `evaluation.md`).

### C. `--json` (design C, D3, D5)
- **`--json`/`-Werror` are qcc-frontend flags; the qcc arg loop is the single
  authoritative site.** A `--json` (and `-Werror`) case in the qcc arg loop sets
  `diagnostics.setJson(true)` / `setWerror(true)`. This is what every U1 gate
  exercises (`./build/qcc --json …`, `./build/qcc -Werror …`) — the gated
  done-condition never routes through bcc. **No new llc/link work** — U0's
  `emitObject`/`appendRuntimeLibs` are untouched (diagnostics have zero llc/link
  relevance).
- **bcc-side threading (UX, non-gated):** the flags are added to bcc's `Options`
  struct (the U0-established bcc flag seam) and forwarded onto the existing
  `qccCmd` construction so `bcc --json foo.b` / `bcc -Werror foo.b` work on the
  user path. NOTE (architect clarification): U0 consolidated the llc/link
  duplication and established `Options` as the flag seam, but did **not**
  consolidate the per-path `qccCmd` construction — so the flags ride the existing
  qcc-invocation sites consistently; this introduces no new duplication and does
  not bypass U0 (there is no U0 helper for qccCmd to route through). The qcc arg
  loop remains the one place the flag's *behavior* is defined.
- At `finish()` with json set, emit a single JSON array to `mOut`; each element
  `{ "severity": "error"|"warning", "file": <str>, "line": <int>, "col": <int>,
  "message": <str>, "code": <str> }`. Strings are JSON-escaped (`"`, `\`,
  control chars). **Covers both syntax and semantic diagnostics** because both
  now buffer into the same collector (D5). In `--json` mode nothing else is
  written to `mOut`, so the array is the sole content of the stream (the human
  lines are not also emitted).
- Add a `code` field to `Diagnostic` (default per class): parser/`CompileError`
  → `"syntax"`; Sema errors → `"sema"` (a small number of high-value classes may
  carry a more specific slug, e.g. `unknown-field`); the unused-variable warning
  → `"unused-variable"`. Non-empty for every diagnostic, satisfying the
  overview's `severity/file/line/col/message/code` schema (the `evaluation.md`
  python assert requires the first five; `code` is always present too).

### D. Multi-error parsing — panic-mode recovery (design D2, D4 — the hard part)
- **Top-level declaration resync (primary).** Move the `try`/`catch` in
  `Module::Parse` (`qcc.cpp:53-331`) to wrap **each iteration** of the top-level
  `while` loop (one iteration = one declaration: fn/struct/enum/impl/protocol/
  test/on/import/extern). On `CompileError`: `report()` it (buffers), then
  **resync** — skip tokens until the next top-level starter
  (`fn`/`async`/`extern`(TYPE_MODIFIER)/`struct`/`enum`/`impl`/`protocol`/`test`/
  `on`/`import`/`pub`/`table`/`@`) or EOF. Then continue the loop. The malformed
  declaration is **skipped whole** (no half-built node is added to the module),
  so the partial AST stays well-formed. `Module::Parse` returns the partial `mod`
  (non-null) even after recovered errors.
- **Statement-level resync (secondary, within a function body).** In
  `Block::Parse`, wrap each statement parse in try/catch; on `CompileError`:
  `report()` then skip to the next `;` or the block-closing `}` (balanced),
  omitting the bad statement. This yields multi-error inside one function body.
  If (during implementation) this proves to cascade phantom errors beyond the
  cap/dedupe mitigation, statement-level resync is descoped and raised as an
  **Open Question** to the manager (top-level resync alone satisfies the
  done-condition); it is **not** silently cut.
- **Cap + dedupe (design D4).** `report()` drops a diagnostic that duplicates an
  already-buffered one by `(file,line,col,code)`; total buffered diagnostics are
  capped (e.g. ≤ 50) with a final `note: too many errors` if exceeded. Prevents
  recovery from spewing phantom cascades.
- **Driver early-return fix (`qcc.cpp:860, 887`).** Replace the two per-file
  `return -1`s with a `hadError` accumulation: a null `Module::Parse` (only on
  catastrophic/unrecoverable input) sets `hadError` and `continue`s; a false
  `Sema::analyze` sets `hadError` and continues. After the file loop, call
  `gDiag->finish()` **once**, then `if ( hadError || gDiag->hasErrors() ) return
  <nonzero>;` **before** codegen. Codegen runs only when there are zero effective
  errors (Constitution III — never codegen a rejected program). `finish()` is
  also called on the success path so warnings render.

## Threads through U0's single path (architect coherence check)
- `--json` is added in **one** place in the qcc arg loop and **one** place in
  bcc's `Options` struct + qcc-invocation forwarding — the seam U0 established.
  It does **not** touch `emitObject`/`appendRuntimeLibs` (no llc/link relevance).
  No re-duplication; no bypass.

## Requirements traceability

| REQ-002 clause | Covered by |
|----------------|-----------|
| multi-error (one compile reports all) | D (top-level + stmt resync; driver early-return fix) |
| `warning` severity + `-Werror` | B (Severity::Warning, unused-variable seed, promotion) |
| `--json` structured output (syntax + semantic) | A (collector) + C (json render) |

## Test plan / done condition (contributes to epic done-condition #2)

1. **Multi-error fixture** `test_files/fail/multi/three_errors.b` with ≥ 3
   independent errors (3 malformed top-level declarations) →
   `./build/qcc test_files/fail/multi/three_errors.b 2>&1 | grep -Ec ':[0-9]+:[0-9]+: (error|warning):'`
   ≥ 3, from **one** invocation. Committed with a `.expected` (canonical
   `file:line:col: error:` regex per `run_tests.sh`).
2. **`--json` schema-valid** `./build/qcc --json test_files/fail/multi/three_errors.b 2>&1 | python3 -c 'import json,sys; d=json.load(sys.stdin); assert len(d)>=3 and all(k in d[0] for k in ("severity","file","line","col","message","code"))'`
   exits 0.
3. **Both-paths `--json`**: a committed fixture with **both** a syntax error and
   a semantic error → its `--json` array contains objects of both classes
   (`"code":"syntax"` and a sema code), proving the collector is the single sink
   for both (design D5 / risk).
4. **Warning + `-Werror`**: `test_files/fail/warn/unused.b` (an unused local) →
   `./build/qcc test_files/fail/warn/unused.b` prints `warning:` and exits **0**;
   `./build/qcc -Werror test_files/fail/warn/unused.b` exits **non-zero**.
   Committed with `.expected`.
5. **No cascade regression**: the existing `fail/` and `fail/sema/` fixtures
   still produce their canonical single located diagnostic (recovery/dedupe does
   not multiply their expected errors); `run_tests.sh` expected-error patterns
   still match.
6. **Suites green, unchanged**: `./run_tests.sh` (LLVM 195/0 + parse-only 190/0
   plus the new fixtures), `./test_codegen.sh` 107/0, `ctest` 54/54,
   `--leak-check` 0 leaks. New negative fixtures raise the run_tests counts;
   codegen count unchanged (U1 is reject-only per the carve-out).

## Risks
- **Cascading phantom errors** from over-eager recovery → bounded resync points +
  cap + dedupe by `(file,line,col,code)` (D4). The `run_tests.sh` expected-error
  patterns on existing single-error fixtures are the regression guard.
- **Two control paths** (syntax vs semantic) → the collector sits under both;
  tested by fixture #3 (both error kinds in one `--json` run).
- **Partial-AST safety**: resync skips **whole** malformed units (declaration or
  statement), so no half-built node reaches Sema/codegen; codegen is gated behind
  zero-errors. Full `test_codegen.sh` is the guard that valid programs are
  unaffected.
- **`--json` stream purity**: chatter is already diverted to a discard sink
  (`qcc.cpp:723`); in `--json` mode `finish()` writes only the array, so
  `2>&1` parses. Verified by test #2.
- **Statement-level resync scope**: if it cascades beyond mitigation, it is
  descoped via an Open Question (top-level resync satisfies the gate), never
  silently cut.
