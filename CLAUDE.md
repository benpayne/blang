# CLAUDE.md - BLang Compiler

## Project Overview

BLang is a compiled, native-performance language designed for clarity, safety, and LLM code generation. The compiler is written in C++17 and uses a hand-written recursive-descent parser (the "QLang" approach) to build an AST. LLVM 18+ code generation is integrated via the `CodeGen` class (`CodeGen.h/cpp`), which walks the QLang AST and emits LLVM IR. When built with LLVM (`llvm-18-dev` package), `qcc` parses source, generates IR, and writes `.ll` files. Without LLVM, it operates in parse-only mode. The project also includes an earlier Bison/Flex-based parser (`parser.yy`, `lexer.l`) and its associated code generation helpers (`parse_helpers.cpp`) which have been superseded.

## Language Design

The full language design specification is in **[docs/language_design.md](docs/language_design.md)**. Key design principles:

- **One right way** — every concept has exactly one syntax, no equivalent alternatives
- **Explicit over implicit** — no hidden conversions, no invisible control flow
- **Safe by default** — no null, no data races, no use-after-free at compile time
- **Native performance** — compiles to native code via LLVM, minimal runtime
- **LLM-optimized** — regular grammar, small keyword set, constrained output space

The language draws from C (performance, simplicity), Rust (ownership, Result types), Swift (protocols, ARC), Go (spawn/channels), and Python (readability) while enforcing a single canonical way to express each concept. See the design spec for full syntax, type system, concurrency model, and comparison with other languages.

## Repository Structure

```
/
├── qcc.cpp                    # Main compiler entry point (parses source files into AST, optionally generates LLVM IR)
├── CodeGen.h                  # LLVM code generation class (QLang::CodeGen) — walks AST, emits IR
├── CodeGen.cpp                # CodeGen implementation: functions, statements, expressions, type mapping
├── Type.h                     # Core type system, base classes: Statement, Type, Symbol, Scope, Module, FunctionDefinition, VariableDefinition, EnumDefinition, GenericParam
├── Expression.h               # Expression/statement AST nodes: Expression, WhileStatement, ForStatement, ForInStatement, IfStatement, ReturnStatement, Block, ArrayLiteralExpression, IndexExpression, MethodCallExpression, RangeExpression, StringInterpolation, etc.
├── CompilerHelpers.h          # CompileError exception class, COMPILE_ERROR macro, SmartPtr ostream operator
├── RefCount.h                 # Standalone RefCount base class and SmartPtr<T> template (replaces jhcommon)
├── logging.h                  # Lightweight logging macros (replaces jhcommon logging)
├── FileLexer.h / FileLexer.cpp # Hand-written lexer (Lexer and LexerReader classes)
├── LexerReader.cpp            # File reader implementation for the lexer
├── Lexer.h                    # Abstract lexer interface
├── Symbol.h                   # Symbol class (BLang namespace, legacy LLVM-based approach)
├── Scope.h                    # Scope class (BLang namespace, legacy LLVM-based approach with BasicBlock/Function)
├── QBlock.cpp                 # Block::Parse implementation
├── QBreakContinue.cpp         # BreakStatement::Parse and ContinueStatement::Parse
├── QEnumDefinition.cpp        # EnumDefinition::Parse (enum/sum types with variants and associated types)
├── QExpression.cpp            # Expression parsing (lvalue, rvalue, binary operations, constants, arrays, method calls, indexing, ranges)
├── QForInStatement.cpp        # ForInStatement::Parse (for-in loops, infinite loops)
├── QFunctionDefinition.cpp    # FunctionDefinition::Parse and parameter parsing (C-style and fn-style, generics)
├── QReturnStatement.cpp       # ReturnStatement::Parse
├── QStatement.cpp             # Statement::Parse dispatcher (routes to if/while/for/for-in/return/break/continue/variable/expression)
├── QStructDefinition.cpp      # StructDefinition::Parse (with generic parameters)
├── QType.cpp                  # Type::Parse (with generic type arguments)
├── QVariableDefinition.cpp    # VariableDeclaration::Parse (with self parameter support)
├── QParser.cpp                # Empty (placeholder)
├── parser.yy                  # Bison grammar (older approach, not used by qcc)
├── parser.h                   # Generated Bison header with token definitions
├── lexer.l                    # Flex lexer specification (older approach)
├── parse_helpers.h/cpp        # LLVM 18+ code generation helpers (legacy Bison/Flex approach, superseded by CodeGen)
├── LexerTest.cpp              # Basic lexer test program
├── LexerTest2.cpp             # Advanced lexer test with position save/restore
├── test.c                     # Comprehensive BLang test source file
├── test_files/                # Test cases organized in pass/, fail/, xfail/ subdirectories
├── run_tests.sh               # Automated test runner script (runs qcc against pass/fail/xfail test categories)
├── test_codegen.sh            # End-to-end codegen test script (parse -> IR -> compile -> run)
├── docs/
│   └── language_design.md     # BLang language design specification
├── CMakeLists.txt             # Build configuration (CMake 3.16+, C++17)
├── README.txt                 # Project goals
├── jhcommon/                  # Git submodule (legacy, no longer linked — kept for reference)
└── .gitmodules                # Submodule config for jhcommon
```

## Build System

**CMake** (minimum version 3.16), **C++17** standard required.

The project has **no external dependencies** for the active build targets. The jhcommon git submodule was previously required but has been replaced with standalone `RefCount.h` and `logging.h` headers.

### Build targets

| Target       | Description                        | Key source files                                                                    |
|-------------|------------------------------------|-------------------------------------------------------------------------------------|
| `qcc`       | Main compiler                      | qcc.cpp, FileLexer.cpp, LexerReader.cpp, QBlock.cpp, QBreakContinue.cpp, QEnumDefinition.cpp, QExpression.cpp, QForInStatement.cpp, QFunctionDefinition.cpp, QReturnStatement.cpp, QStatement.cpp, QStructDefinition.cpp, QType.cpp, QVariableDefinition.cpp, CodeGen.cpp (when LLVM available) |
| `lexerTest` | Basic lexer tokenization test      | LexerTest.cpp, FileLexer.cpp, LexerReader.cpp                                     |
| `lexerTest2`| Advanced lexer test                | LexerTest2.cpp, FileLexer.cpp, LexerReader.cpp                                    |

### Building

```bash
mkdir build && cd build
cmake ..
make
```

To build with LLVM code generation (requires `llvm-18-dev` package):
```bash
cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
make
```

When LLVM is found, `qcc` gains the `BLANG_HAS_LLVM` define and will generate `.ll` IR files alongside parsing. Without LLVM, `qcc` operates in parse-only mode.

### Platform detection

Platform is auto-detected by CMake:
- macOS: defines `PLATFORM_DARWIN`
- Linux: defines `PLATFORM_LINUX`
- Windows: defines `PLATFORM_WINDOWS`

### Compile definitions

- `JH_VERBOSE_LOGGING` — Enables verbose logging and trace output (set on `qcc` target)
- `BLANG_HAS_LLVM` — Defined automatically when LLVM is found; enables code generation in `qcc`

## Dependencies

| Dependency | Status | Purpose |
|-----------|--------|---------|
| LLVM 18+  | Optional, auto-detected | Code generation via `CodeGen` class (requires `llvm-18-dev` package) |

The project is fully self-contained. `RefCount.h` provides intrusive reference counting (`RefCount` base class + `SmartPtr<T>` template) using `std::atomic` for thread safety. `logging.h` provides lightweight `LOG`, `TRACE_BEGIN`, `SET_LOG_CAT`, and `SET_LOG_LEVEL` macros.

## Architecture

### Compiler pipeline

1. **Parsing — QLang recursive-descent parser** (`QLang` namespace in `Type.h`, `Expression.h`, `Q*.cpp`, `qcc.cpp`): Hand-written parser that builds an AST from source files.

2. **Code generation — CodeGen** (`QLang::CodeGen` in `CodeGen.h/cpp`): Walks the QLang AST and emits LLVM IR using modern LLVM 18+ APIs (`IRBuilder<>`, opaque pointers, `FunctionCallee`). The `CodeGen` class is a friend of all AST node classes and uses `dynamic_cast` to dispatch to type-specific generation methods. Conditionally compiled (`BLANG_HAS_LLVM`).

3. **Legacy code generation** (`BLang` namespace in `Scope.h`, `Symbol.h`, `parse_helpers.cpp`): Earlier procedural C-style API driven by the Bison/Flex parser. Superseded by `CodeGen` but retained for reference.

### Key class hierarchy (QLang namespace)

```
RefCount
├── Statement                    # Base for all statements
│   ├── Expression               # Base for all expressions
│   │   ├── ConstExpression      # Literal values
│   │   │   ├── ConstInteger
│   │   │   ├── ConstFloat
│   │   │   ├── ConstString
│   │   │   └── ConstChar
│   │   ├── VariableExpression   # Variable reference
│   │   ├── CallExpression       # Function call
│   │   ├── AssignmentExpression # Assignment (=, +=, etc.)
│   │   ├── OperationsExpression # Binary operations (+, -, etc.)
│   │   ├── UnaryExpression      # Unary operations (-, !, ~)
│   │   ├── FieldAccessExpression # Struct field access (obj.field)
│   │   ├── MethodCallExpression # Method call (obj.method(args))
│   │   ├── IndexExpression      # Array/collection indexing (expr[index])
│   │   ├── ArrayLiteralExpression # Array literal [expr, ...]
│   │   ├── RangeExpression      # Range expression (start..end)
│   │   ├── StringInterpolation  # String interpolation "hello \(name)"
│   │   └── MatchExpression      # Pattern matching (match expr { ... })
│   ├── WhileStatement
│   ├── ForStatement             # C-style for(init; cond; step)
│   ├── ForInStatement           # for x in expr { }, for { } (infinite)
│   ├── IfStatement
│   ├── ReturnStatement
│   ├── BreakStatement           # break; in loops
│   ├── ContinueStatement        # continue; in loops
│   ├── VariableDeclaration
│   └── Block                    # { ... } with its own Scope
├── Type                         # Type representation with optional generic type params
├── Symbol                       # Named entity base (abstract)
│   ├── FunctionDefinition       # Function with params, return type, body, optional generic params
│   ├── VariableDefinition       # Variable with type
│   ├── StructDefinition         # Struct with fields, methods, optional generic params
│   ├── EnumDefinition           # Enum/sum type with variants and associated types
│   └── ProtocolDefinition       # Protocol (interface) with required methods, optional generic params
├── Scope                        # Symbol table with parent chain
└── Module                       # Top-level container of FunctionDefinitions
```

### Parsing pattern

Every AST node class has a `static Parse(Lexer &l, Scope *scope)` factory method. Parsing is recursive-descent: `Module::Parse` calls `FunctionDefinition::Parse`, which calls `Block::Parse`, which calls `Statement::Parse`, which dispatches to the appropriate subclass based on the next token.

### Memory management

All AST nodes inherit from `RefCount` (defined in `RefCount.h`). Ownership is managed through `SmartPtr<T>` — an intrusive reference-counted smart pointer using `std::atomic<int>` for thread safety. Raw `new` is used in `Parse` methods; the returned pointer is immediately stored in a `SmartPtr`.

### Error handling

- `CompileError` exception class carries the error message, source file, and line number.
- `COMPILE_ERROR(lexer, message)` macro throws a `CompileError` with `__FILE__` and `__LINE__`.
- The top-level `Module::Parse` has a try/catch that prints the error and returns `nullptr`.

## Code Conventions

### Naming

- **Classes**: PascalCase (`FunctionDefinition`, `VariableExpression`)
- **Member variables**: camelCase with `m` prefix (`mReturnType`, `mParameters`, `mScope`)
- **Local variables**: camelCase (`statement`, `sym`, `scope`)
- **Enum values**: `kScope_Global` style (QLang) or `SCOPE_GLOBAL` style (BLang)
- **Constants/macros**: UPPER_SNAKE_CASE (`COMPILE_ERROR`, `NUM_SYMBOLS`)

### C++ standard

- Use C++17 features where appropriate
- Use `nullptr` instead of `NULL`
- Prefer range-based `for` loops over iterator-based loops

### Formatting

- Tabs for indentation
- Spaces inside parentheses: `if ( sym != '(' )`
- Spaces around operators: `sym = l.getSymbol()`
- Braces on their own line for function/class bodies
- Allman brace style for control flow blocks

### File naming

- Headers: PascalCase (`.h`) — `Type.h`, `Expression.h`, `FileLexer.h`
- Implementation files for QLang parser: `Q` prefix + PascalCase (`.cpp`) — `QBlock.cpp`, `QExpression.cpp`
- Other implementation files: PascalCase (`.cpp`) — `FileLexer.cpp`, `LexerReader.cpp`

### Namespaces

- `QLang` — Active parser/AST classes
- `BLang` — Legacy LLVM-based code path
- Source files use `using namespace QLang;` and `using namespace std;`

## Testing

### Test programs

- `lexerTest` / `lexerTest2` — Run against test source files to verify tokenization.
- `qcc <filename>` — Parses the given file and prints the AST. Outputs "Completed parse" on success.

### Test source files

Tests are organized into three categories under `test_files/`:

- **`test_files/pass/`** (51 tests) — Should parse successfully. Includes:
  - Basic function tests: `func_simple.c`, `func_call.c`, `multi_func.c`, `empty_func.c`
  - Control flow: `if_simple.c`, `if_nested.c`, `while_simple.c`, `while_block.c`, `for_simple.c`, `for_block.c`
  - Variables and expressions: `var_decl.c`, `var_infer.c`, `const_decl.c`, `arithmetic_stmt.c`, `assignment_stmt.c`, `binary_expr_return.c`, `comparison_expr.c`
  - Returns: `return_var.c`, `return_call.c`
  - Literals and comments: `string_literals.c`, `comments.c`, `float_literal.c`, `bool_type.c`
  - Function styles: `fn_simple.c`, `fn_void.c`, `arrow_fn.c`
  - Structs and methods: `struct_basic.c`, `struct_literal.c`, `field_access.c`, `method_call.c`, `impl_basic.c`, `impl_protocol.c`
  - Protocols: `protocol_basic.c`
  - Enums: `enum_basic.c`, `enum_variants.c`, `enum_generic.c`
  - Generics: `generic_fn.c`, `generic_struct.c`, `generic_constraint.c`, `generic_protocol.c`, `generic_type_args.c`
  - For-in loops: `for_in_range.c`, `for_in_var.c`, `for_infinite.c`
  - Arrays: `array_literal.c`, `array_index.c`
  - Pattern matching: `match_basic.c`, `match_wildcard.c`, `match_destructure.c`
  - Other: `break_continue.c`, `extern_unnamed.c`
- **`test_files/fail/`** (14 negative tests) — Should fail to parse (exit non-zero): `bad_type.c`, `const_no_init.c`, `duplicate_func.c`, `enum_missing_brace.c`, `fn_missing_arrow_type.c`, `for_in_missing_in.c`, `missing_brace.c`, `missing_paren.c`, `protocol_no_fn.c`, `struct_bad_field.c`, `struct_missing_brace.c`, `undefined_func.c`, `undefined_var.c`, `var_no_init.c`
- **`test_files/xfail/`** (1 expected failure) — Known-broken features: `extern_func_call.c`

Legacy test files (kept for backward compatibility): `test.c`, `test_files/func_call1.c`, `test_files/func_call2.c`, `test_files/func_call3.c`, `test_files/if_call.c`, `test_files/codegen_simple.c`, `test_files/codegen_binexpr.c`, `test_files/codegen_features.c`, `test_files/multi_var_decl.c`

**Total: 66 tests** (51 pass + 14 fail/negative + 1 xfail)

### Running tests

```bash
# Automated test runner (recommended)
./run_tests.sh              # Run all tests (requires qcc already built)
./run_tests.sh --build      # Build first, then run tests
./run_tests.sh --verbose    # Show detailed output for failures

# Manual testing
cd build
./lexerTest ../test.c
./lexerTest2 ../test.c
./qcc ../test.c
./qcc ../test_files/pass/fn_simple.c
```

The `run_tests.sh` script runs `qcc` against all test files in `test_files/pass/`, `test_files/fail/`, and `test_files/xfail/`, checking exit codes against expectations and printing a color-coded summary.

## Supported Language Features (BLang source)

- **Function definitions** — two styles supported during transition:
  - C-style: `int add(int a, int b) { ... }`
  - BLang `fn`-style: `fn add(int a, int b) -> int { ... }` (omitting `->` means void return)
  - Generic functions: `fn identity<T>(T value) -> T { ... }`
  - Protocol-constrained generics: `fn sort<T: Comparable>(Array<T> items) -> Array<T> { ... }`
- Extern function declarations (`extern int printf(string fmt, ...);`) for calling C library functions
  - Named parameters: `extern int printf(string fmt, ...);`
  - Unnamed parameters: `extern int printf(string, ...);`
  - Mixed: `extern int mixed(int a, string, int c);`
- Variadic function support (`...` ellipsis in parameter lists)
- Variable declarations with optional initialization
- **Struct definitions** with optional generic parameters: `struct Box<T> { T value; }`
- **Enum/sum type definitions** with variants and associated types: `enum Option<T> { some(T), none }`
- **Protocol definitions** with optional generic parameters: `protocol Container<T> { ... }`
- **Method calls**: `obj.method(args)` syntax
- **Field access**: `obj.field` syntax
- **Array literals**: `[1, 2, 3]` syntax
- **Array/collection indexing**: `arr[index]` syntax
- **Range expressions**: `0..10` syntax
- Control flow: `if`/`else`, `while`, `for`, `break`, `continue`
  - C-style for: `for (int i = 0; i < 10; i = i + 1) { ... }`
  - For-in loops: `for x in collection { ... }`, `for i in 0..10 { ... }`
  - Key-value iteration: `for key, value in map { ... }`
  - Infinite loops: `for { ... }`
- **Pattern matching** with `match`:
  - Literal patterns: `match x { 1 => { ... }, 2 => { ... } }`
  - Wildcard pattern: `_ => { ... }` (default/catch-all)
  - Destructuring with bindings: `ok(value) => { ... }`, `some(x) => { ... }`
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`), bitwise (`&`, `|`, `^`, `<<`, `>>`)
- Assignment operators: `=`, `+=`, `-=`, `*=`, `/=`
- Function calls with arguments
- Return statements
- Block scoping with `{` `}`
- Single-line (`//`) and multi-line (`/* */`) comments
- **Literals**: integers, floats/doubles (`3.14`, `0.001`), strings, characters
- **Types**: `int`, `float`, `double`, `char`, `string`, `bool`, `long`, `short`, `void`, generic types (`Array<int>`, `Map<string, int>`)

## Project Status

This is an active work-in-progress. The recursive-descent parser can parse BLang source into an AST, and when built with LLVM, the `CodeGen` class generates LLVM IR for the parsed AST.

**Parser features**: Both C-style and BLang `fn`-style function declarations, struct definitions with generic parameters, enum/sum type definitions with variants and associated types, protocol definitions with generic parameters, generic functions with protocol constraints, for-in loops (range iteration, collection iteration, infinite loops), array literals and indexing, method calls, field access, range expressions, pattern matching with wildcards and destructuring bindings, float/double literals, break/continue, and extern declarations with unnamed parameters.

**Lexer keywords**: `fn`, `bool`, `struct`, `impl`, `self`, `protocol`, `match`, `import`, `pub`, `break`, `continue`, `enum`, `in`. Additional tokens: `->` (arrow), `..` (range), `_` (wildcard), float constants.

**Codegen features** (requires LLVM): function definitions, extern function declarations, variadic function calls, variable declarations with initialization, return statements, if/else, while, for loops, function calls, binary expressions (arithmetic, comparison, logical, bitwise with correct operator precedence), assignment expressions (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `^=`), and constant expressions (int, float, string, char). The full pipeline (parse → LLVM IR → native binary) is tested end-to-end.

The long-term goals (from README.txt) include integrated threading, eventing, garbage collection, FPGA synthesis support, and networking in the standard library.

## Known Issues and Limitations

- No CI/CD pipeline.
- No automated test framework (but `run_tests.sh` provides a basic test runner with pass/fail/xfail categories).
- Legacy LLVM code path (`parse_helpers.cpp`) uses `Type::getInt32Ty` as a default pointee type for opaque pointer loads — should be wired to the symbol table's stored type for full correctness.
