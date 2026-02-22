# Spec: Multi-Variable Declaration Support

## Problem Statement

The BLang parser does not correctly handle C-style multi-variable declarations
such as:

```c
int b = 4, c;
int x = 42, y = 10;
int a, b, c;
```

This syntax is present in the primary test file (`test.c` line 26: `int b = 4, c;`)
and is a standard C feature. The parser currently **hangs infinitely** when
encountering this construct.

## Root Cause Analysis

The hang involves three interacting pieces:

### 1. Comma not consumed in `VariableDeclaration::Parse` (primary bug)

`QVariableDefinition.cpp:67` — The `do...while` loop checks
`l.peekSymbol() == ','` to decide whether to iterate again, but **never
consumes the comma**. On the next iteration, `l.getSymbol()` (line 42) reads
the comma character instead of the expected variable name, causing
`sym == Lexer::SYMBOL` to fail, which throws `CompileError`.

```cpp
// Current code (broken)
} while ( l.peekSymbol() == ',' );
//         ^^^^ peeks but doesn't consume
```

### 2. Catch-and-reset in `Statement::Parse` rewinds to before `int`

`QStatement.cpp:47-53` — When `VariableDeclaration::Parse` throws, the catch
block resets the lexer position to before the entire declaration and falls
through to `Expression::Parse`. Since `int` is a `BUILTIN_TYPE` (not a
`SYMBOL` or constant), `Expression::Parse` returns `nullptr`.

### 3. `Block::Parse` loops forever

`QBlock.cpp:30-34` — The block loop `while ( l.peekSymbol() != '}' )` calls
`Statement::Parse` repeatedly. Since the lexer position was reset and
`Statement::Parse` returned `nullptr` without advancing the lexer, the peek
still sees `int`, never reaches `}`, and the loop repeats infinitely.

## Scope of Fix

### In scope

- Consume the comma between variables in multi-var declarations
- Handle all combinations:
  - `int a, b, c;` — multiple uninitialized
  - `int a = 1, b;` — mixed initialized/uninitialized
  - `int a = 1, b = 2, c = 3;` — all initialized
  - `int a, b = 2;` — uninitialized then initialized

### Out of scope (future work)

- Complex initializer expressions (e.g., `int x = foo(), y;` where the
  initializer is a function call) — this hits a separate issue where
  `Expression::Parse` consumes the terminal character for call/variable
  expressions but not for constants. A broader `Expression::Parse` refactor
  is needed to address this consistently. For now, multi-var declarations
  with constant initializers are the target.
- Pointer/array declarations (`int *a, b;`, `int a[10], b;`)
- Declaration expressions in for-loops (`for (int i = 0, j = 0; ...)`)

## Detailed Design

### Fix 1: Consume the comma in the do-while loop

In `QVariableDefinition.cpp`, consume the comma when the while-condition is
true before re-entering the loop body:

```cpp
// Option A: Change do-while to do-while with comma consumption
do {
    VariableDeclaration::DeclData data;
    int sym = l.getSymbol();

    if ( t != nullptr && sym == Lexer::SYMBOL )
    {
        data.mVaribale = new VariableDefinition( t, l.getSymbolText() );
    }
    else
    {
        COMPILE_ERROR( l, "Failed parse varible" );
    }

    s->addSymbol( data.mVaribale );

    if ( l.peekSymbol() == '=' )
    {
        sym = l.getSymbol();
        data.mInitialValue = Expression::Parse( l, s );
        if ( data.mInitialValue == nullptr )
        {
            COMPILE_ERROR( l, "Failed parse value" );
        }
    }
    def->mVariables.push_back( data );
} while ( l.peekSymbol() == ',' && l.getSymbol() == ',' );
//                                  ^^^^^^^^^^^^^^^^^^^^^^^^
//                                  consume the comma
```

**Alternative (clearer):**

```cpp
    def->mVariables.push_back( data );

    if ( l.peekSymbol() != ',' )
        break;

    l.getSymbol(); // consume ','
} while ( true );
```

### Fix 2: Semicolon handling

The semicolon after the full declaration (`int a = 1, b;` ← this `;`) is
**not** consumed by `VariableDeclaration::Parse`. This currently works by
accident because `Block::Parse` → `Statement::Parse` has a `case ';'` that
consumes bare semicolons on the next iteration. This is acceptable for now
but should be documented.

No code change needed — the existing pattern handles it.

### Fix 3: Add test coverage

- Move `test_files/xfail/multi_var_decl.c` to a passing test (or create new
  test files)
- Verify `test.c` parses fully (it contains `int b = 4, c;` on line 26)
- Add new test cases for the combinations listed in "Scope of Fix"

## Test Plan

| Test case | Input | Expected |
|-----------|-------|----------|
| Two vars, first initialized | `int x = 42, y;` | Parse succeeds, two variables in scope |
| Two vars, both initialized | `int a = 1, b = 2;` | Parse succeeds |
| Three vars, none initialized | `int a, b, c;` | Parse succeeds |
| Mixed initialization | `int a, b = 5, c;` | Parse succeeds |
| Single var (regression) | `int x = 5;` | Still works |
| Full test.c | `test.c` (line 26) | "Completed parse" with no hang |

## Files Modified

| File | Change |
|------|--------|
| `QVariableDefinition.cpp` | Consume comma in do-while loop |
| `test_files/xfail/multi_var_decl.c` | Remove (no longer expected to fail) |
| `test_files/multi_var_decl.c` | New — expanded test cases |
