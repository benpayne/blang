# Contract: Expected-Error Test Harness (`run_tests.sh`)

Extends the negative-test judgement for `test_files/fail/` and
`test_files/cgfail/`. Backward compatible: tests without a declaration behave
exactly as before.

## Declaring an expected message

A negative test MAY declare one ERE pattern via either carrier:

1. **Companion file** — `<test.b>.expected` in the same directory
   (e.g. `test_files/fail/missing_brace.b.expected`). The pattern is the first
   non-empty line that does not start with `#`. **Precedence: wins if present.**
2. **Inline comment** — a line `// EXPECT-ERROR: <pattern>` anywhere in the
   `.b` source. `<pattern>` is everything after the marker, trimmed.

If both are present, the companion file wins (the inline comment is ignored).
If neither is present, no pattern applies.

## Judgement

For a negative test (category `fail` or `cgfail`):

1. Run the compiler; capture exit code and **stderr** (stderr is no longer
   discarded).
2. If exit code is 0 → **FAIL** (`expected rejection, but compiled OK`),
   regardless of any pattern.
3. Else if a pattern applies → **PASS** iff `printf '%s' "$stderr" | grep -Eq
   -- "$pattern"`; otherwise **FAIL** (`wrong diagnostic`), and in `--verbose`
   the harness prints the expected pattern and the actual stderr.
4. Else (no pattern) → **PASS** (exit-code-only, legacy behavior).

## Invocation determinism

- The compiler is invoked so that the asserted stderr is stable and matches
  what later `fail/sema/` tests assert (`build/qcc --parse-only <file>` for
  parse/sema errors). The exact flags are fixed in `tasks.md`; the harness
  must not pass `-v`/`--debug-compiler` (those would add non-canonical lines).
- `cgfail` tests (LLVM builds only) keep their codegen-triggering invocation;
  their patterns match the codegen-stage stderr. In parse-only builds `cgfail`
  auto-skips (unchanged).

## The canonical-format assertion

Any test may use the pattern `^[^:]+\.b:[0-9]+:[0-9]+: error: ` to assert only
that a located error was produced. Later units' `fail/sema/` audit programs
combine this with a message-specific substring.

## Coverage requirement (this unit)

At least 10 existing `test_files/fail/` tests gain a declaration and pass in
the new mode, in both build modes (SC-005). Patterns are derived from the
actual U2 compiler output to avoid drift.

## Self-check (SC-004)

Adding a declaration to a `fail/` test makes `run_tests.sh` pass it; mutating
the declared pattern to a non-matching string makes `run_tests.sh` fail that
test. (Performed during implementation/audit; the mutation is reverted.)
