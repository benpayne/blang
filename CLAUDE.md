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
├── QLambdaExpression.cpp       # LambdaExpression::Parse (anonymous function expressions)
├── QParser.cpp                # Empty (placeholder)
├── parser.yy                  # Bison grammar (older approach, not used by qcc)
├── parser.h                   # Generated Bison header with token definitions
├── lexer.l                    # Flex lexer specification (older approach)
├── parse_helpers.h/cpp        # LLVM 18+ code generation helpers (legacy Bison/Flex approach, superseded by CodeGen)
├── BmodEmitter.h / BmodEmitter.cpp # .bmod interface file emitter (walks AST, emits pub declarations in BLang syntax)
├── ProjectConfig.h / ProjectConfig.cpp # blang.toml parser and project configuration (name, version, type, dependencies)
├── BuildCache.h / BuildCache.cpp # Content-addressable build cache (SHA-256 keyed, ~/.cache/blang/objects/)
├── sha256.h / sha256.c        # Standalone SHA-256 implementation (FIPS 180-4, no external deps)
├── FormatString.h / FormatString.cpp # Compile-time format string parser for print/println ({}, {:x}, {:.2f}, etc.)
├── SQLGen.h / SQLGen.cpp      # SQL generation from query AST nodes (SELECT/INSERT/UPDATE/DELETE, CREATE TABLE)
├── SchemaMigration.h / SchemaMigration.cpp # Schema migration engine (diff, CREATE/ALTER TABLE, preview/apply)
├── stdlib/
│   └── net.b                  # BLang networking stdlib (Socket, ServerSocket, Selector structs + impl methods)
├── LexerTest.cpp              # Basic lexer test program
├── LexerTest2.cpp             # Advanced lexer test with position save/restore
├── test.b                     # Comprehensive BLang test source file
├── test_files/                # Test cases organized in pass/, fail/, cgfail/, xfail/ subdirectories
├── test_build/                # Build system integration tests (lib + bin project pairs)
│   ├── mathlib/               # Test library project (blang.toml type=lib, pub fn add/multiply)
│   └── myapp/                 # Test binary project (blang.toml type=bin, depends on mathlib)
├── run_tests.sh               # Automated test runner script (runs qcc against pass/fail/cgfail/xfail test categories)
├── test_codegen.sh            # End-to-end codegen test script (parse -> IR -> compile -> link -> run)
├── docs/
│   └── language_design.md     # BLang language design specification
│   └── implementation_plan.md # Implementation task list and roadmap
├── runtime/
│   ├── blang_runtime.h/c     # Core runtime (ARC, thread pool, channels, async event loop)
│   ├── blang_string.h/c      # Safe string runtime (BlangString: refcounted, immutable, heap-allocated)
│   ├── blang_array.h/c       # Safe array runtime (BlangArray: refcounted, bounds-checked, growable)
│   ├── blang_json.h/c        # JSON encode/decode library (for @json annotation support)
│   ├── blang_net.h/c         # TCP socket + poll-based Selector runtime (listen, accept, connect, read, write, event loop)
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
| `bcc`       | BLang compiler driver (user-facing)| bcc.cpp, ProjectConfig.cpp, BuildCache.cpp, sha256.c                               |
| `qcc`       | Parser + IR generator (internal)   | qcc.cpp, FileLexer.cpp, LexerReader.cpp, QBlock.cpp, QBreakContinue.cpp, QEnumDefinition.cpp, QExpression.cpp, QForInStatement.cpp, QFunctionDefinition.cpp, QReturnStatement.cpp, QStatement.cpp, QStructDefinition.cpp, QType.cpp, QVariableDefinition.cpp, QSpawnStatement.cpp, QEventHandler.cpp, QTestBlock.cpp, QAssertStatement.cpp, QLambdaExpression.cpp, BmodEmitter.cpp, CodeGen.cpp (when LLVM available) |
| `blang_string`| String runtime library (static)  | runtime/blang_string.c                                                             |
| `blang_array`| Array runtime library (static)    | runtime/blang_array.c                                                              |
| `blang_json`| JSON runtime library (static)      | runtime/blang_json.c                                                               |
| `blang_net` | Networking runtime library (static)| runtime/blang_net.c (links blang_string)                                           |
| `blang_db`  | Database runtime library (static)  | runtime/blang_db.c (optional SQLite via pkg-config)                                |
| `blang_sha256`| SHA-256 hash library (static)   | sha256.c                                                                           |
| `blang_buildcache`| Build cache library (static) | BuildCache.cpp (links blang_sha256)                                                |
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

### Using bcc build (Project Build System)

`bcc build` reads `blang.toml` from the current directory, discovers `.b` source files, resolves dependencies, and builds the project.

```bash
# Build a project (requires blang.toml in current directory)
cd myproject/
bcc build

# For type = "lib": produces lib<name>.a + <name>.bmod
# For type = "bin": produces executable named after project

# Clean the build cache
bcc clean                     # removes ~/.cache/blang/
```

**blang.toml format**:
```toml
[project]
name = "myapp"
version = "0.1.0"
type = "bin"          # "lib" emits .a + .bmod; "bin" (default) emits executable

[deps]
mathlib = { path = "../mathlib" }
```

**Build flow**: Dependencies are built recursively in topological order. Each dependency's artifacts (.a + .bmod) are cached in `~/.cache/blang/objects/<sha256>/`. On cache hit, the build is skipped. `.bmod` files are passed to `qcc` for type resolution; `.a` files are linked into the final binary.

**No blang.toml**: When no `blang.toml` is present, `bcc` operates in single-file mode (`bcc hello.b -o hello`) — fully backward compatible.

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
│   │   ├── EnumConstructExpression # Enum variant construction (EnumName.variant(args))
│   │   ├── AwaitExpression      # await expr (async value retrieval)
│   │   ├── PipelineExpression   # expr |> fn(args) pipeline operator
│   │   ├── FunctionRefExpression # Named function used as value (callback)
│   │   ├── LambdaExpression    # fn(params) -> RetType { body } anonymous function
│   │   ├── IndirectCallExpression # Calling through fn-typed variable
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
│   └── FunctionType            # fn(ParamTypes) -> RetType function type
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

Tests are organized into four categories under `test_files/`:

- **`test_files/pass/`** (109 tests) — Should parse successfully. Includes:
  - Basic function tests: `func_simple.b`, `func_call.b`, `multi_func.b`, `empty_func.b`
  - Control flow: `if_simple.b`, `if_nested.b`, `while_simple.b`, `while_block.b`
  - Variables and expressions: `var_decl.b`, `var_infer.b`, `const_decl.b`, `arithmetic_stmt.b`, `assignment_stmt.b`, `binary_expr_return.b`, `comparison_expr.b`, `mixed_type_expr.b`
  - Returns: `return_var.b`, `return_call.b`
  - Literals and comments: `string_literals.b`, `comments.b`, `float_literal.b`, `bool_type.b`
  - Function styles: `fn_simple.b`, `fn_void.b`, `arrow_fn.b`
  - Structs and methods: `struct_basic.b`, `struct_literal.b`, `field_access.b`, `method_call.b`, `impl_basic.b`, `impl_protocol.b`
  - Protocols: `protocol_basic.b`, `protocol_conformance.b`
  - Enums: `enum_basic.b`, `enum_variants.b`, `enum_generic.b`, `enum_payload_construct.b`
  - Generics: `generic_fn.b`, `generic_struct.b`, `generic_constraint.b`, `generic_protocol.b`, `generic_type_args.b`
  - For-in loops: `for_in_range.b`, `for_in_var.b`, `for_infinite.b`
  - Arrays: `array_literal.b`, `array_index.b`
  - Pattern matching: `match_basic.b`, `match_wildcard.b`, `match_destructure.b`, `match_enum_variants.b`
  - Result/Option: `result_option.b`, `try_operator.b`
  - Modules: `import_basic.b`, `import_dotted.b`, `pub_function.b`, `pub_struct.b`, `pub_enum.b`, `visibility_basic.b`
  - Other: `break_continue.b`, `extern_unnamed.b`, `extern_func_call.b`
  - Ownership: `own_basic.b`, `shared_basic.b`, `sync_basic.b`, `ownership_all.b`, `own_move_valid.b`
  - Concurrency: `spawn_basic.b`, `spawn_nested.b`, `spawn_expr.b`, `chan_decl.b`, `wait_basic.b`, `wait_all_basic.b`
  - Async/await: `async_fn.b`, `async_fn_void.b`, `await_expr.b`, `event_handler.b`
  - Contracts: `requires_basic.b`, `ensures_basic.b`, `contract_combined.b`
  - Testing: `test_basic.b`, `test_assert.b`, `test_assert_message.b`, `test_multiple.b`, `assert_in_function.b`
  - Pipeline operator: `pipeline_basic.b`, `pipeline_chained.b`, `pipeline_with_args.b`
  - Annotations: `annotation_json.b`, `annotation_multiple.b`, `annotation_with_args.b`
  - Table structs and queries: `table_struct.b`, `query_basic.b`, `query_insert.b`, `query_update.b`, `query_delete.b`, `query_join.b`
  - FFI types: `cstring_extern.b`, `carray_extern.b`
  - Lambdas: `fn_type_param.b`, `fn_type_var.b`, `lambda_basic.b`, `lambda_capture.b`, `lambda_indirect_call.b`
- **`test_files/fail/`** (40 negative tests) — Should fail to parse (exit non-zero): `annotation_missing_name.b`, `assert_missing_semi.b`, `async_missing_fn.b`, `bad_type.b`, `carray_in_fn.b`, `carray_in_var.b`, `const_no_init.b`, `cstring_in_fn.b`, `cstring_in_var.b`, `c_style_func.b`, `duplicate_func.b`, `enum_missing_brace.b`, `fn_missing_arrow_type.b`, `for_c_style.b`, `for_c_style_block.b`, `for_in_missing_in.b`, `generic_duplicate_param.b`, `generic_unknown_constraint.b`, `import_missing_name.b`, `import_missing_semi.b`, `insert_missing_brace.b`, `json_generic_struct.b`, `match_missing_brace.b`, `missing_brace.b`, `missing_paren.b`, `protocol_missing_method.b`, `protocol_no_fn.b`, `query_missing_table.b`, `requires_missing_expr.b`, `spawn_missing_brace.b`, `struct_bad_field.b`, `struct_missing_brace.b`, `table_missing_struct.b`, `test_missing_body.b`, `test_missing_name.b`, `undefined_func.b`, `undefined_var.b`, `var_no_init.b`, `wait_all_missing_semi.b`, `wait_missing_semi.b`
- **`test_files/cgfail/`** (4 codegen-fail tests) — Should fail at code generation (only run when built with LLVM, skipped in parse-only mode): `json_unsupported_field.b`, `own_move_in_loop.b`, `own_spawn_capture.b`, `own_use_after_move.b`
- **`test_files/codegen_*.b`** (45 end-to-end tests) — Full pipeline tests (parse → IR → compile → link → run) in `test_files/`: `codegen_array.b`, `codegen_array_methods.b`, `codegen_assert.b`, `codegen_async.b`, `codegen_async_multi.b`, `codegen_binexpr.b`, `codegen_break_continue.b`, `codegen_comprehensive.b`, `codegen_contracts.b`, `codegen_enum_payload.b`, `codegen_features.b`, `codegen_forin.b`, `codegen_generic_fn.b`, `codegen_generic_struct.b`, `codegen_http_blang.b`, `codegen_json.b`, `codegen_json_nested.b`, `codegen_json_roundtrip.b`, `codegen_json_types.b`, `codegen_lambda.b`, `codegen_lambda_callback.b`, `codegen_lambda_named_ref.b`, `codegen_ownership.b`, `codegen_ownership_move.b`, `codegen_phase2.b`, `codegen_pipeline.b`, `codegen_result_type.b`, `codegen_selector.b`, `codegen_shared_spawn.b`, `codegen_simple.b`, `codegen_spawn.b`, `codegen_spawn_threaded.b`, `codegen_string.b`, `codegen_string_interp.b`, `codegen_string_methods.b`, `codegen_sync_locking.b`, `codegen_sync_spawn.b`, `codegen_tcp_echo.b`, `codegen_try_operator.b`, `codegen_type_coercion.b`, `codegen_wait.b`, `codegen_wait_all.b`
Legacy test files (kept for reference): `test.b`, `test_files/func_call1.b`, `test_files/func_call2.b`, `test_files/func_call3.b`, `test_files/if_call.b`, `test_files/multi_var_decl.b`

**Total: 158 pass/fail tests** (109 pass + 41 fail + 8 cgfail) **+ 45 codegen E2E tests**

### Running tests

```bash
# Automated test runner (recommended)
./run_tests.sh              # Run all tests (requires qcc already built)
./run_tests.sh --build      # Build first, then run tests
./run_tests.sh --verbose    # Show detailed output for failures

# End-to-end codegen tests (requires LLVM build)
./test_codegen.sh           # Run all codegen_*.b tests (parse -> IR -> compile -> link -> run)
./test_codegen.sh test_files/codegen_simple.b   # Run a single codegen test (shows IR)
./test_codegen.sh --verbose # Show IR output for all tests

# Manual testing
cd build
./lexerTest ../test.b
./lexerTest2 ../test.b
./qcc ../test.b
./qcc ../test_files/pass/fn_simple.b
```

The `run_tests.sh` script runs `qcc` against all test files in `test_files/pass/`, `test_files/fail/`, `test_files/cgfail/`, and `test_files/xfail/`, checking exit codes against expectations and printing a color-coded summary. The `cgfail/` tests are only run when qcc is built with LLVM; they are automatically skipped in parse-only builds.

The `test_codegen.sh` script runs the full compilation pipeline (qcc → llc → cc → run) for each `test_files/codegen_*.b` file and checks that the resulting binary exits with code 0. It automatically links the runtime library (`libblang_runtime.a`) and JSON library (`libblang_json.a`) when available.

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
- **Types**: `int`, `float`, `double`, `char`, `string`, `bool`, `long`, `short`, `void`, generic types (`Array<int>`, `Map<string, int>`), function types (`fn(int, string) -> bool`), FFI types (`cstring`, `carray<T>` — restricted to `extern fn` declarations only)
- **Lambda expressions** — `fn(int x) -> int { return x * 2; }` anonymous functions as expressions, with automatic closure capture of outer variables
- **Function types** — `fn(ParamTypes) -> RetType` as a first-class type for parameters and variables; ABI is a `{fn_ptr, ctx_ptr}` callback pair
- **Indirect calls** — calling through function-typed variables: `callback(args)`
- **Named function references** — passing named functions as callback values: `apply(double_it, 5)` wraps via a thunk
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
- **Builtin print/println** — `print("hello {}", name)` and `println("{:.2f}", pi)` with compile-time format string validation, positional `{}` placeholders, format specifiers (`{:x}`, `{:X}`, `{:o}`, `{:b}`, `{:.Nf}`, `{:e}`), and `Printable` protocol support for user-defined types
- **Printable protocol** — builtin protocol with `to_string(self) -> string`; structs implementing it can be used in `{}` print placeholders
- **`@format` annotation** — validates call-site format strings against arguments at compile time for user-defined format functions

## Project Status

This is an active work-in-progress. The recursive-descent parser can parse BLang source into an AST, and when built with LLVM, the `CodeGen` class generates LLVM IR for the parsed AST.

**Parser features**: BLang `fn`-style function declarations (C-style syntax rejected), `extern fn` declarations, struct definitions with generic parameters, enum/sum type definitions with variants and associated types, enum variant construction (`EnumName.variant(args)`), protocol definitions with generic parameters and conformance checking, generic functions with protocol constraints, generic type arguments on struct literals and function calls, for-in loops (range iteration, collection iteration, infinite loops), array literals and indexing, method calls, field access, range expressions, pattern matching with wildcards and destructuring bindings, `?` try operator for error propagation, `import` statements with dotted paths, `pub` visibility modifier, duplicate function detection, float/double literals, break/continue, extern declarations with unnamed parameters, multi-file compilation, `.bmod` interface file emission (`--emit-bmod`) and consumption (two-phase parsing with flat symbol merge), ownership qualifiers (`own`, `shared`, `sync`) on variable declarations and function parameters, spawn blocks, async/await, event handlers (`on`), contract clauses (`requires`/`ensures`), test blocks, assert statements, pipeline operator (`|>`), annotations (`@name("arg")`) with compile-time validation (e.g., `@json` rejected on generic structs), table structs (`table struct`), query/insert/update/delete expressions with pipeline steps, boolean constants (`true`/`false`), FFI types (`cstring`, `carray<T>`) restricted to `extern fn` declarations with compile-time enforcement, function types (`fn(ParamTypes) -> RetType`) as first-class types, lambda expressions (`fn(params) -> RetType { body }`) with automatic closure capture, indirect calls through fn-typed variables, and named function references as values.

**Lexer keywords**: `fn`, `bool`, `struct`, `impl`, `self`, `protocol`, `match`, `import`, `pub`, `break`, `continue`, `enum`, `in`, `own`, `shared`, `sync`, `spawn`, `chan`, `async`, `await`, `on`, `requires`, `ensures`, `test`, `assert`, `table`, `query`, `insert`, `update`, `delete`, `cstring`, `carray`. Additional tokens: `->` (arrow), `..` (range), `_` (wildcard), `?` (try operator), `|>` (pipeline), `@` (annotation), `true`/`false` (boolean constants), float constants.

**CLI features**: `qcc` supports `--parse-only`, `-S`/`--emit-ir`, `-c`/`--emit-obj`, `-o`/`--output`, `--emit-bmod <file>`, `--combine` (compile multiple `.b` files into a single `.ll` with shared scope), `--help` flags and multiple input files (`.b` and `.bmod`). `bcc build` reads `blang.toml`, resolves dependencies, and builds projects (lib→.a+.bmod, bin→executable) with content-addressable caching. `bcc clean` removes the build cache. `bcc test` subcommand discovers and runs test files. `bcc migrate` subcommand supports `--preview`, `--apply`, and `--generate` modes for schema migrations.

**Runtime libraries**: `blang_string` (safe immutable string type — `BlangString` with refcounting, heap allocation, and bounds-checked access), `blang_array` (safe generic array type — `BlangArray` with refcounting, bounds-checked access, and dynamic growth), `blang_json` (C library for JSON encode/decode), `blang_net` (TCP sockets + poll-based Selector event loop — listen/accept/connect/read/write/close, event-driven I/O with fd-based handle table), `blang_db` (database abstraction with optional SQLite backend).

**Stdlib**: `stdlib/net.b` defines BLang-native types — `Socket`, `ServerSocket` (with methods via `impl FileOps`), `Selector` (with `on_accept`, `on_data`, `run`, `shutdown` methods using lambda callbacks), `HttpServer`/`HttpRequest`/`HttpResponse` (wrapping the C HTTP runtime), and BLang-native HTTP utility functions: `build_http_response()` (response building via string interpolation), `parse_http_request_line()` (Buffer-based request line parsing into `HttpRequestLine` struct), `parse_http_headers_from_buffer()` (Buffer-based header parsing into `HttpParsedHeaders` struct with parallel key/value arrays), `extract_http_body()` (extracts body after `\r\n\r\n` separator), `http_get_buffered()` (BLang-native HTTP GET client using Buffer I/O), and `http_status_text()` (status code to reason phrase). These pure-BLang functions complement the C-backed HTTP types. Compiled into user programs via `qcc --combine stdlib/net.b user.b`; `bcc` auto-includes stdlib when available. `stdlib/collections.b` defines `Map<K,V>` (key-value store using parallel arrays with `has`, `get`, `set` methods). SQL generation (`SQLGen`) converts query AST nodes to parameterized SQL. Schema migration (`SchemaMigration`) diffs table struct definitions against stored schema and generates CREATE/ALTER TABLE statements.

**Codegen features** (requires LLVM): function definitions, extern function declarations, variadic function calls, variable declarations with initialization, return statements, if/else (no parentheses), while (no parentheses), for-in loops, function calls, binary expressions (arithmetic, comparison, logical, bitwise with correct operator precedence and automatic integer type coercion for mixed-width operands), assignment expressions (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `^=`), constant expressions (int, float, string, char), break/continue in all loop types (while, for, for-in, infinite), safe string type (`BlangString` — literals via `__blang_string_create_static`, concatenation via `__blang_string_concat`, comparison via `__blang_string_equals`, interpolation via `__blang_string_concat_many` with per-type to_string helpers, refcount tracking at scope boundaries), safe array type (`BlangArray` — literals via `__blang_array_create`/`__blang_array_push`, bounds-checked indexing via `__blang_array_get`/`__blang_array_set`, for-in iteration, refcount tracking at scope boundaries), `cstring`/`carray<T>` FFI types with automatic conversion at extern fn boundaries (string→cstring extracts `.data` field, cstring→string wraps via `__blang_string_create`), builtin method calls on string (`.length`, `.is_empty`, `.to_upper()`, `.to_lower()`, `.trim()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.index_of()`, `.substring()`, `.replace()`) and Array (`.length`, `.is_empty`, `.push()`, `.pop()`, `.clear()`), pipeline operator (`|>` desugaring), ARC retain/release at scope boundaries for shared/sync variables, runtime init/shutdown in `main()` for concurrency features, spawn blocks with closure extraction (captured variables packed into context struct, dispatched to thread pool via `__blang_spawn`, `own` variables cannot be captured across spawn boundaries), channel variable declarations (`chan<T>` → `__blang_chan_create`), async function wrappers (body extracted to `void*(void*)`, called via `__blang_async_call`), await expressions (`__blang_await` + `__blang_task_destroy`), event handler callback extraction, database query/insert/update/delete codegen (SQL generated at compile time via `SQLGen`, emits `__blang_db_query`/`__blang_db_exec` calls), enum/sum type tagged union layout (`{i32 tag, [N x i8] payload}` with variant construction and match-based payload extraction), generic struct/function monomorphization (stamps out concrete versions like `Box_int` with mangled names), ownership move semantics enforcement (use-after-move and move-in-loop detection), `@json` annotation codegen (generates `StructName_to_json`/`StructName_from_json` functions supporting all primitive types, nested `@json` structs, with compile-time errors for unsupported field types), `?` try operator codegen (resolves operand's enum type, extracts tag, branches on ok/some vs err/none, unwraps payload on success, propagates error via early return on failure), multi-module type sharing (`registerExternalTypes` shares struct/enum definitions across CodeGen instances for cross-module type resolution), combined compilation mode (`--combine` feeds multiple modules into a single CodeGen instance with shared scope, deduplicating extern/method declarations for stdlib integration), builtin `print`/`println` with compile-time format string validation (positional `{}` placeholders, format specifiers `{:x}`,`{:X}`,`{:o}`,`{:b}`,`{:.Nf}`,`{:e}`, Printable protocol for user-defined types, `@format` annotation for call-site validation on user-defined functions), and lambda/callback codegen (anonymous functions with closure capture via context struct + malloc, function types as `{fn_ptr, ctx_ptr}` pairs with parameter expansion, indirect calls through fn-typed variables, named function references via thunk wrappers with null context). The full pipeline (parse → LLVM IR → native binary) is tested end-to-end with 45 codegen E2E tests.

The long-term goals (from README.txt) include integrated threading, eventing, garbage collection, FPGA synthesis support, and networking in the standard library.

## Known Issues and Limitations

- Channel send/receive operations lack parser syntax — only `chan<T>` variable declarations are codegen'd.
- Async function return value boxing/unboxing is simplified (heap-allocated, not yet optimized).
- Event handler registration requires runtime event loop API (currently executes inline as a fallback).
- Database query codegen uses NULL connection pointer — needs global DB connection management.
- Multi-module codegen shares type definitions (structs/enums) across modules. Cross-module function linking works via `.bmod` interface files and `bcc build` (functions from deps are auto-declared as extern and linked from `.a` archives).
- Generic functions from dependencies: `.bmod` includes the signature but monomorphization requires the body. Consumer gets a linker error if they try to instantiate a generic from a dep. Full support requires shipping function bodies or pre-instantiated variants.
- Git dependencies (`deps.foo = { git = "...", tag = "v1.0" }`) are parsed in blang.toml but not yet fetched — only local path deps work currently.
- Generic monomorphization is basic — supports struct and function instantiation but not generic protocols or deeply nested generics.
- Nested `@json` structs use an encode/decode round-trip (serialize inner struct to string, decode to JSON tree) — works correctly but adds overhead; direct subtree construction would be more efficient.
- Legacy LLVM code path (`parse_helpers.cpp`) uses `Type::getInt32Ty` as a default pointee type for opaque pointer loads — should be wired to the symbol table's stored type for full correctness.
