# Calculator — expression interpreter example

A small arithmetic expression interpreter written in BLang. It reads an
expression string, tokenizes it, and evaluates it with a classic
recursive-descent parser, returning a `Result<int, string>` so errors
(division by zero, unbalanced parentheses, unexpected input) are reported
rather than crashing.

```
expr   = term (("+" | "-") term)*
term   = factor (("*" | "/") factor)*
factor = NUMBER | "(" expr ")" | "-" factor
```

Standard precedence and left-associativity; supports `+ - * /`, parentheses,
unary minus, multi-digit integers, and arbitrary whitespace.

## What it exercises

This example was written to shake out real language/compiler bugs (and it did —
see below). It uses:

- **Enums with payloads** — `Token` (`num(int)`, `plus`, …) built and matched.
- **Arrays of enums** — the token stream is an `Array<Token>`.
- **`Result<T, E>` + the `?` operator** — every parse step returns a `Result`
  and propagates errors with `?`.
- **Pattern matching** — `match` on tokens and on results.
- **A mutable struct threaded by reference** — a small `Parser { toks, pos }`
  whose `pos` advances through the recursive calls (structs are heap references,
  so writes persist for the caller).
- **Forward references / mutual recursion** — `main` and the mutually recursive
  `parse_expr → parse_term → parse_factor` appear before their callees.
- **String/char scanning** — `s[i]`, char ranges (`'0'..'9'`), char arithmetic.
- **Short-circuit `&&`** — the tokenizer relies on `i < n && s[i] == …` not
  indexing past the end.

## Build and run

```bash
# from this directory (requires bcc built with LLVM)
../../build/bcc build
./calculator
```

Expected output:

```
1 + 2 * 3 = 7
(1 + 2) * 3 = 9
10 - 2 - 3 = 5
-5 + 8 = 3
2 * (3 + 4) - 1 = 13
100 / 5 / 2 = 10
1 / 0 -> error: division by zero
2 + -> error: expected number, unary minus, or paren
(1 + 2 -> error: expected closing paren
```

## Tests

The `test` blocks are colocated in `main.b`. Run them with the built-in test
runner:

```bash
../../build/bcc test
```

Each test runs in isolation (a failing `assert` reports and the suite
continues), with a per-test PASS/FAIL line and a final summary. `bcc test`
exits nonzero if any test fails.

`./test_calculator.sh` automates the whole thing: build, run the demo,
spot-check results, and run `bcc test`.

## Bugs this example surfaced (and fixed)

- **`&&` / `||` did not short-circuit** — both operands were always evaluated,
  so a guard like `i < n && s[i] == c` would index out of bounds. Now compiled
  with proper branching.
- **Refcounted payloads (e.g. `Array`) carried through `Result`/`Option` were
  freed too early** — a local array returned via `Result.ok(a)` was released by
  its origin scope while the enum still referenced it (use-after-free on
  unwrap). The enum now retains its heap payload.
