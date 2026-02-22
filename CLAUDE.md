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
├── Type.h                     # Core type system, base classes: Statement, Type, Symbol, Scope, Module, FunctionDefinition, VariableDefinition
├── Expression.h               # Expression/statement AST nodes: Expression, WhileStatement, ForStatement, IfStatement, ReturnStatement, Block, constants, etc.
├── CompilerHelpers.h          # CompileError exception class, COMPILE_ERROR macro, SmartPtr ostream operator
├── RefCount.h                 # Standalone RefCount base class and SmartPtr<T> template (replaces jhcommon)
├── logging.h                  # Lightweight logging macros (replaces jhcommon logging)
├── FileLexer.h / FileLexer.cpp # Hand-written lexer (Lexer and LexerReader classes)
├── LexerReader.cpp            # File reader implementation for the lexer
├── Lexer.h                    # Abstract lexer interface
├── Symbol.h                   # Symbol class (BLang namespace, legacy LLVM-based approach)
├── Scope.h                    # Scope class (BLang namespace, legacy LLVM-based approach with BasicBlock/Function)
├── QBlock.cpp                 # Block::Parse implementation
├── QExpression.cpp            # Expression parsing (lvalue, rvalue, binary operations, constants)
├── QFunctionDefinition.cpp    # FunctionDefinition::Parse and parameter parsing
├── QReturnStatement.cpp       # ReturnStatement::Parse
├── QStatement.cpp             # Statement::Parse dispatcher (routes to if/while/for/return/variable/expression)
├── QType.cpp                  # Type::Parse
├── QVariableDefinition.cpp    # VariableDeclaration::Parse
├── QParser.cpp                # Empty (placeholder)
├── parser.yy                  # Bison grammar (older approach, not used by qcc)
├── parser.h                   # Generated Bison header with token definitions
├── lexer.l                    # Flex lexer specification (older approach)
├── parse_helpers.h/cpp        # LLVM 18+ code generation helpers (legacy Bison/Flex approach, superseded by CodeGen)
├── LexerTest.cpp              # Basic lexer test program
├── LexerTest2.cpp             # Advanced lexer test with position save/restore
├── test.c                     # Comprehensive BLang test source file
├── test_files/                # Individual test cases (func_call*.c, if_call.c, codegen_simple.c)
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
| `qcc`       | Main compiler                      | qcc.cpp, FileLexer.cpp, LexerReader.cpp, Q*.cpp, CodeGen.cpp (when LLVM available) |
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
│   │   └── OperationsExpression # Binary operations (+, -, etc.)
│   ├── WhileStatement
│   ├── ForStatement
│   ├── IfStatement
│   ├── ReturnStatement
│   ├── VariableDeclaration
│   └── Block                    # { ... } with its own Scope
├── Type                         # Type representation ("int", "char", "string")
├── Symbol                       # Named entity base (abstract)
│   ├── FunctionDefinition       # Function with params, return type, body
│   └── VariableDefinition       # Variable with type
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

- `test.c` — Comprehensive test covering functions, variables, control flow, operators, comments, string/char literals.
- `test_files/func_call1.c` — Function with arithmetic and calls.
- `test_files/func_call2.c` — Function with return statement.
- `test_files/func_call3.c` — Printf with void main.
- `test_files/if_call.c` — If statement with printf.

### Running tests

```bash
cd build
./lexerTest ../test.c
./lexerTest2 ../test.c
./qcc ../test.c
./qcc ../test_files/func_call1.c
```

There is no automated test harness or CI. Tests are run manually.

## Supported Language Features (BLang source)

- Function definitions with parameters and return types (`int`, `char`, `string`, `void`)
- Variable declarations with optional initialization
- Control flow: `if`/`else`, `while`, `for`
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`), bitwise (`&`, `|`, `^`, `<<`, `>>`)
- Assignment operators: `=`, `+=`, `-=`, `*=`, `/=`
- Function calls with arguments
- Return statements
- Block scoping with `{` `}`
- Single-line (`//`) and multi-line (`/* */`) comments
- String and character literals

## Project Status

This is an active work-in-progress. The recursive-descent parser can parse BLang source into an AST, and when built with LLVM, the `CodeGen` class generates LLVM IR for the parsed AST. The codegen currently supports: function definitions, variable declarations with constant initialization, return statements, if/else, while, for loops, function calls, and constant expressions (int, float, string, char). Binary expressions and assignment statements are not yet parseable. The long-term goals (from README.txt) include integrated threading, eventing, garbage collection, FPGA synthesis support, and networking in the standard library.

## Known Issues and Limitations

- No CI/CD pipeline.
- No automated test framework.
- Binary expressions (e.g., `a + b` as statements) are not fully supported by the parser — causes "Failed to find terminal" errors in `func_call1.c`, `func_call2.c`.
- Legacy LLVM code path (`parse_helpers.cpp`) uses `Type::getInt32Ty` as a default pointee type for opaque pointer loads — should be wired to the symbol table's stored type for full correctness.
