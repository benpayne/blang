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
├── bcc.cpp                    # BLang compiler driver CLI (orchestrates qcc -> llc -> cc pipeline)
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
├── QFunctionDefinition.cpp    # FunctionDefinition::Parse and parameter parsing (fn-style with extern fn, generics)
├── QReturnStatement.cpp       # ReturnStatement::Parse
├── QStatement.cpp             # Statement::Parse dispatcher (routes to if/while/for-in/return/break/continue/variable/expression)
├── QStructDefinition.cpp      # StructDefinition::Parse (with generic parameters)
├── QType.cpp                  # Type::Parse (with generic type arguments)
├── QVariableDefinition.cpp    # VariableDeclaration::Parse (with self parameter support)
├── QSpawnStatement.cpp        # SpawnStatement::Parse (spawn { } blocks)
├── QEventHandler.cpp          # EventHandler::Parse (on expr { } event handlers)
├── QTestBlock.cpp             # TestBlock::Parse (test "name" { } blocks)
├── QAssertStatement.cpp       # AssertStatement::Parse (assert expr; statements)
├── QQueryExpression.cpp        # Query/insert/update/delete expression parsing (table struct queries)
├── QParser.cpp                # Empty (placeholder)
├── parser.yy                  # Bison grammar (older approach, not used by qcc)
├── parser.h                   # Generated Bison header with token definitions
├── lexer.l                    # Flex lexer specification (older approach)
├── parse_helpers.h/cpp        # LLVM 18+ code generation helpers (legacy Bison/Flex approach, superseded by CodeGen)
├── SQLGen.h / SQLGen.cpp      # SQL generation from query AST nodes (SELECT/INSERT/UPDATE/DELETE, CREATE TABLE)
├── SchemaMigration.h / SchemaMigration.cpp # Schema migration engine (diff, CREATE/ALTER TABLE, preview/apply)
├── LexerTest.cpp              # Basic lexer test program
├── LexerTest2.cpp             # Advanced lexer test with position save/restore
├── test.b                     # Comprehensive BLang test source file
├── test_files/                # Test cases organized in pass/, fail/, xfail/ subdirectories
├── run_tests.sh               # Automated test runner script (runs qcc against pass/fail/xfail test categories)
├── test_codegen.sh            # End-to-end codegen test script (parse -> IR -> compile -> run)
├── docs/
│   └── language_design.md     # BLang language design specification
│   └── implementation_plan.md # Implementation task list and roadmap
├── runtime/
│   ├── blang_runtime.h/c     # Core runtime (ARC, thread pool, channels, async event loop)
│   ├── blang_json.h/c        # JSON encode/decode library (for @json annotation support)
│   └── blang_db.h/c          # Database abstraction layer (connection, query, result; optional SQLite backend)
├── .github/
│   └── workflows/ci.yml      # GitHub Actions CI (parse-only and with-llvm matrix)
├── install_deps.sh            # Cross-platform dependency installer
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
| `bcc`       | BLang compiler driver (user-facing)| bcc.cpp                                                                            |
| `qcc`       | Parser + IR generator (internal)   | qcc.cpp, FileLexer.cpp, LexerReader.cpp, QBlock.cpp, QBreakContinue.cpp, QEnumDefinition.cpp, QExpression.cpp, QForInStatement.cpp, QFunctionDefinition.cpp, QReturnStatement.cpp, QStatement.cpp, QStructDefinition.cpp, QType.cpp, QVariableDefinition.cpp, QSpawnStatement.cpp, QEventHandler.cpp, QTestBlock.cpp, QAssertStatement.cpp, CodeGen.cpp (when LLVM available) |
| `blang_json`| JSON runtime library (static)      | runtime/blang_json.c                                                               |
| `blang_db`  | Database runtime library (static)  | runtime/blang_db.c (optional SQLite via pkg-config)                                |
| `blang_sqlgen`| SQL gen + migrations library     | SQLGen.cpp, SchemaMigration.cpp                                                    |
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

### Using bcc (BLang Compiler Driver)

`bcc` is the user-facing compiler CLI. It orchestrates the full pipeline: `qcc` (parse + IR) → `llc` (compile) → `cc` (link).

```bash
# Compile a BLang source file to an executable
bcc hello.b -o hello
./hello

# Emit LLVM IR only
bcc -S hello.b              # produces hello.ll

# Compile to object file only (no linking)
bcc -c hello.b              # produces hello.o

# Verbose output (shows each pipeline step)
bcc -v hello.b -o hello

# Link with external libraries
bcc hello.b -o hello -lm
```

**Requirements**: `bcc` expects `qcc` in the same directory and requires `llc-18` (or `llc`) and `cc` on PATH. Install `llvm-18-dev` for full pipeline support.

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
| SQLite3   | Optional, auto-detected via pkg-config | Database runtime backend for `blang_db` library |

The project is fully self-contained. `RefCount.h` provides intrusive reference counting (`RefCount` base class + `SmartPtr<T>` template) using `std::atomic` for thread safety. `logging.h` provides lightweight `LOG`, `TRACE_BEGIN`, `SET_LOG_CAT`, and `SET_LOG_LEVEL` macros.

## Architecture

### Compiler pipeline

```
bcc source.b -o program
 │
 ├─ 1. qcc source.b          → source.ll   (parse + LLVM IR generation)
 ├─ 2. llc -filetype=obj      → source.o    (IR to native object)
 └─ 3. cc source.o -o program → program     (link to executable)
```

1. **Compiler driver — bcc** (`bcc.cpp`): User-facing CLI that orchestrates the full compilation pipeline. Shells out to `qcc`, `llc`, and `cc`. Handles `-S` (emit IR), `-c` (compile only), `-o` (output name), `-v` (verbose), and linker flags (`-l`, `-L`).

2. **Parsing + IR generation — qcc** (`QLang` namespace in `Type.h`, `Expression.h`, `Q*.cpp`, `qcc.cpp`): Hand-written recursive-descent parser that builds an AST from source files. When built with LLVM, also generates LLVM IR via `CodeGen`.

3. **Code generation — CodeGen** (`QLang::CodeGen` in `CodeGen.h/cpp`): Walks the QLang AST and emits LLVM IR using modern LLVM 18+ APIs (`IRBuilder<>`, opaque pointers, `FunctionCallee`). The `CodeGen` class is a friend of all AST node classes and uses `dynamic_cast` to dispatch to type-specific generation methods. Conditionally compiled (`BLANG_HAS_LLVM`).

4. **Legacy code generation** (`BLang` namespace in `Scope.h`, `Symbol.h`, `parse_helpers.cpp`): Earlier procedural C-style API driven by the Bison/Flex parser. Superseded by `CodeGen` but retained for reference.

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
│   │   ├── StringInterpolation  # String interpolation "hello {name}"
│   │   ├── MatchExpression      # Pattern matching (match expr { ... })
│   │   ├── AwaitExpression      # await expr (async value retrieval)
│   │   ├── PipelineExpression   # expr |> fn(args) pipeline operator
│   │   ├── QueryFieldExpression # .field references in query contexts
│   │   ├── QueryExpression      # query T |> where {} |> order_by {} |> limit(n)
│   │   ├── InsertExpression     # insert T { field: value, ... }
│   │   ├── UpdateExpression     # update T |> where {} |> set { .field = value }
│   │   └── DeleteExpression     # delete T |> where {}
│   ├── WhileStatement           # while expr { }
│   ├── ForStatement             # C-style for(init; cond; step) — DEPRECATED, no longer parsed
│   ├── ForInStatement           # for x in expr { }, for { } (infinite)
│   ├── IfStatement
│   ├── ReturnStatement
│   ├── BreakStatement           # break; in loops
│   ├── ContinueStatement        # continue; in loops
│   ├── SpawnStatement           # spawn { ... } green thread block
│   ├── AssertStatement          # assert expr; with optional message
│   ├── EventHandler             # on expr { ... } event handler
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
├── Module                       # Top-level container of FunctionDefinitions
└── TestBlock                    # test "name" { ... } test block
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

- BLang source files: `.b` extension — `hello.b`, `test.b`
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

- **`test_files/pass/`** (91 tests) — Should parse successfully. Includes:
  - Basic function tests: `func_simple.b`, `func_call.b`, `multi_func.b`, `empty_func.b`
  - Control flow: `if_simple.b`, `if_nested.b`, `while_simple.b`, `while_block.b`
  - Variables and expressions: `var_decl.b`, `var_infer.b`, `const_decl.b`, `arithmetic_stmt.b`, `assignment_stmt.b`, `binary_expr_return.b`, `comparison_expr.b`
  - Returns: `return_var.b`, `return_call.b`
  - Literals and comments: `string_literals.b`, `comments.b`, `float_literal.b`, `bool_type.b`
  - Function styles: `fn_simple.b`, `fn_void.b`, `arrow_fn.b`
  - Structs and methods: `struct_basic.b`, `struct_literal.b`, `field_access.b`, `method_call.b`, `impl_basic.b`, `impl_protocol.b`
  - Protocols: `protocol_basic.b`, `protocol_conformance.b`
  - Enums: `enum_basic.b`, `enum_variants.b`, `enum_generic.b`
  - Generics: `generic_fn.b`, `generic_struct.b`, `generic_constraint.b`, `generic_protocol.b`, `generic_type_args.b`
  - For-in loops: `for_in_range.b`, `for_in_var.b`, `for_infinite.b`
  - Arrays: `array_literal.b`, `array_index.b`
  - Pattern matching: `match_basic.b`, `match_wildcard.b`, `match_destructure.b`, `match_enum_variants.b`
  - Result/Option: `result_option.b`, `try_operator.b`
  - Modules: `import_basic.b`, `import_dotted.b`, `pub_function.b`, `pub_struct.b`, `pub_enum.b`, `visibility_basic.b`
  - Other: `break_continue.b`, `extern_unnamed.b`
  - Ownership: `own_basic.b`, `shared_basic.b`, `sync_basic.b`, `ownership_all.b`
  - Concurrency: `spawn_basic.b`, `spawn_nested.b`, `chan_decl.b`
  - Async/await: `async_fn.b`, `async_fn_void.b`, `await_expr.b`, `event_handler.b`
  - Contracts: `requires_basic.b`, `ensures_basic.b`, `contract_combined.b`
  - Testing: `test_basic.b`, `test_assert.b`, `test_assert_message.b`, `test_multiple.b`, `assert_in_function.b`
  - Pipeline operator: `pipeline_basic.b`, `pipeline_chained.b`, `pipeline_with_args.b`
  - Annotations: `annotation_json.b`, `annotation_multiple.b`, `annotation_with_args.b`
  - Table structs and queries: `table_struct.b`, `query_basic.b`, `query_insert.b`, `query_update.b`, `query_delete.b`, `query_join.b`
- **`test_files/fail/`** (33 negative tests) — Should fail to parse (exit non-zero): `bad_type.b`, `c_style_func.b`, `const_no_init.b`, `duplicate_func.b`, `enum_missing_brace.b`, `fn_missing_arrow_type.b`, `for_c_style.b`, `for_c_style_block.b`, `for_in_missing_in.b`, `generic_duplicate_param.b`, `generic_unknown_constraint.b`, `import_missing_name.b`, `import_missing_semi.b`, `match_missing_brace.b`, `missing_brace.b`, `missing_paren.b`, `protocol_missing_method.b`, `protocol_no_fn.b`, `struct_bad_field.b`, `struct_missing_brace.b`, `undefined_func.b`, `undefined_var.b`, `var_no_init.b`, `spawn_missing_brace.b`, `assert_missing_semi.b`, `test_missing_name.b`, `test_missing_body.b`, `requires_missing_expr.b`, `async_missing_fn.b`, `annotation_missing_name.b`, `table_missing_struct.b`, `query_missing_table.b`, `insert_missing_brace.b`
Legacy test files (kept for reference): `test.b`, `test_files/func_call1.b`, `test_files/func_call2.b`, `test_files/func_call3.b`, `test_files/if_call.b`, `test_files/codegen_simple.b`, `test_files/codegen_binexpr.b`, `test_files/codegen_features.b`, `test_files/multi_var_decl.b`

**Total: 124 tests** (91 pass + 33 fail/negative)

### Running tests

```bash
# Automated test runner (recommended)
./run_tests.sh              # Run all tests (requires qcc already built)
./run_tests.sh --build      # Build first, then run tests
./run_tests.sh --verbose    # Show detailed output for failures

# Manual testing
cd build
./lexerTest ../test.b
./lexerTest2 ../test.b
./qcc ../test.b
./qcc ../test_files/pass/fn_simple.b
```

The `run_tests.sh` script runs `qcc` against all test files in `test_files/pass/`, `test_files/fail/`, and `test_files/xfail/`, checking exit codes against expectations and printing a color-coded summary.

## Supported Language Features (BLang source)

- **Function definitions** — all functions use the `fn` keyword:
  - `fn add(int a, int b) -> int { ... }` (omitting `->` means void return)
  - Generic functions: `fn identity<T>(T value) -> T { ... }`
  - Protocol-constrained generics: `fn sort<T: Comparable>(Array<T> items) -> Array<T> { ... }`
- Extern function declarations use `extern fn` syntax:
  - Named parameters: `extern fn printf(string fmt, ...) -> int;`
  - Unnamed parameters: `extern fn printf(string, ...) -> int;`
  - Mixed: `extern fn mixed(int a, string, int c) -> int;`
- Variadic function support (`...` ellipsis in parameter lists)
- Variable declarations with optional initialization
- **Struct definitions** with optional generic parameters: `struct Box<T> { T value; }`
- **Enum/sum type definitions** with variants and associated types: `enum Option<T> { some(T), none }`
- **Protocol definitions** with optional generic parameters: `protocol Container<T> { ... }`
- **Protocol conformance checking**: `impl Protocol for Struct` verifies all required methods are implemented
- **Method calls**: `obj.method(args)` syntax
- **Field access**: `obj.field` syntax
- **Array literals**: `[1, 2, 3]` syntax
- **Array/collection indexing**: `arr[index]` syntax
- **Range expressions**: `0..10` syntax
- Control flow: `if`/`else`, `while`, `for..in`, `break`, `continue`
  - No parentheses on `if` or `while` conditions: `if x > 0 { }`, `while running { }`
  - For-in loops: `for x in collection { ... }`, `for i in 0..10 { ... }`
  - Key-value iteration: `for key, value in map { ... }`
  - Infinite loops: `for { ... }`
  - No C-style `for(;;)` — use `for..in` with ranges or `while` instead
- **Pattern matching** with `match`:
  - Literal patterns: `match x { 1 { ... } 2 { ... } }`
  - Wildcard pattern: `_ { ... }` (default/catch-all)
  - Destructuring with bindings: `ok(value) { ... }`, `some(x) { ... }`
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`), bitwise (`&`, `|`, `^`, `<<`, `>>`)
- **`?` try operator**: `expr?` postfix operator for error propagation (unwraps Result/Option, returns early on error)
- Assignment operators: `=`, `+=`, `-=`, `*=`, `/=`
- **Import statements**: `import std;`, `import std.io;` (dotted module paths)
- **Visibility modifier**: `pub fn`, `pub struct`, `pub enum` for public declarations
- **Duplicate function detection**: redeclaring a function in the same scope is a compile error
- Function calls with arguments
- Return statements
- Block scoping with `{` `}`
- Single-line (`//`) and multi-line (`/* */`) comments
- **Literals**: integers, floats/doubles (`3.14`, `0.001`), strings, characters
- **Types**: `int`, `float`, `double`, `char`, `string`, `bool`, `long`, `short`, `void`, generic types (`Array<int>`, `Map<string, int>`)
- **Ownership qualifiers** — `own`, `shared`, `sync` on variable declarations
- **Spawn blocks** — `spawn { ... }` for green thread creation
- **Async functions** — `async fn name() { }` for event-loop scheduled functions
- **Await expressions** — `await expr` for async value retrieval
- **Event handlers** — `on expr { ... }` for event-driven programming
- **Contract clauses** — `requires expr` and `ensures expr` on function declarations
- **Test blocks** — `test "name" { ... }` for built-in unit testing
- **Assert statements** — `assert expr;` and `assert expr, "message";`
- **Pipeline operator** — `expr |> fn(args)` desugars to `fn(expr, args)`; supports chaining
- **Annotations** — `@name` and `@name("arg")` on struct, enum, and function declarations
- **Table structs** — `table struct T { ... }` marks structs as database-backed tables
- **Query expressions** — `query T |> where { .field == value } |> order_by { .field } |> limit(n) |> first`
- **Insert expressions** — `insert T { field: value, ... }`
- **Update expressions** — `update T |> where { ... } |> set { .field = value }`
- **Delete expressions** — `delete T |> where { ... }`
- **Boolean constants** — `true` and `false` as first-class constant expressions

## Project Status

This is an active work-in-progress. The recursive-descent parser can parse BLang source into an AST, and when built with LLVM, the `CodeGen` class generates LLVM IR for the parsed AST.

**Parser features**: BLang `fn`-style function declarations (C-style syntax rejected), `extern fn` declarations, struct definitions with generic parameters, enum/sum type definitions with variants and associated types, protocol definitions with generic parameters and conformance checking, generic functions with protocol constraints, for-in loops (range iteration, collection iteration, infinite loops), array literals and indexing, method calls, field access, range expressions, pattern matching with wildcards and destructuring bindings, `?` try operator for error propagation, `import` statements with dotted paths, `pub` visibility modifier, duplicate function detection, float/double literals, break/continue, extern declarations with unnamed parameters, multi-file compilation, ownership qualifiers (`own`, `shared`, `sync`), spawn blocks, async/await, event handlers (`on`), contract clauses (`requires`/`ensures`), test blocks, assert statements, pipeline operator (`|>`), annotations (`@name("arg")`), table structs (`table struct`), query/insert/update/delete expressions with pipeline steps, and boolean constants (`true`/`false`).

**Lexer keywords**: `fn`, `bool`, `struct`, `impl`, `self`, `protocol`, `match`, `import`, `pub`, `break`, `continue`, `enum`, `in`, `own`, `shared`, `sync`, `spawn`, `chan`, `async`, `await`, `on`, `requires`, `ensures`, `test`, `assert`, `table`, `query`, `insert`, `update`, `delete`. Additional tokens: `->` (arrow), `..` (range), `_` (wildcard), `?` (try operator), `|>` (pipeline), `@` (annotation), `true`/`false` (boolean constants), float constants.

**CLI features**: `qcc` supports `--parse-only`, `-S`/`--emit-ir`, `-c`/`--emit-obj`, `-o`/`--output`, `--help` flags and multiple input files. `bcc test` subcommand discovers and runs test files. `bcc migrate` subcommand supports `--preview`, `--apply`, and `--generate` modes for schema migrations.

**Runtime libraries**: `blang_json` (C library for JSON encode/decode), `blang_db` (database abstraction with optional SQLite backend). SQL generation (`SQLGen`) converts query AST nodes to parameterized SQL. Schema migration (`SchemaMigration`) diffs table struct definitions against stored schema and generates CREATE/ALTER TABLE statements.

**Codegen features** (requires LLVM): function definitions, extern function declarations, variadic function calls, variable declarations with initialization, return statements, if/else (no parentheses), while (no parentheses), for-in loops, function calls, binary expressions (arithmetic, comparison, logical, bitwise with correct operator precedence), assignment expressions (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `^=`), and constant expressions (int, float, string, char). The full pipeline (parse → LLVM IR → native binary) is tested end-to-end.

The long-term goals (from README.txt) include integrated threading, eventing, garbage collection, FPGA synthesis support, and networking in the standard library.

## Known Issues and Limitations

- Codegen for structs, methods, generics, Result/Option, match, and `?` operator requires LLVM and is not yet implemented.
- Codegen for ownership (move semantics, ARC, auto-locking), spawn/chan (green threads, channels), async/await (event loop, coroutines), contracts (runtime checks), and test blocks (test runner) requires LLVM and runtime library, not yet implemented.
- Multi-module codegen (linking symbols across files) is not yet implemented.
- Generic type instantiation (monomorphization or type erasure) is not yet implemented.
- Legacy LLVM code path (`parse_helpers.cpp`) uses `Type::getInt32Ty` as a default pointee type for opaque pointer loads — should be wired to the symbol table's stored type for full correctness.
