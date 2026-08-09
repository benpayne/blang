# CLAUDE.md - BLang Compiler

## Project Overview

BLang is a compiled, native-performance language designed for clarity, safety, and LLM code generation. The compiler is written in C++17 and uses a hand-written recursive-descent parser (the "QLang" approach) to build an AST. LLVM 18+ code generation is integrated via the `CodeGen` class (`CodeGen.h/cpp`), which walks the QLang AST and emits LLVM IR. When built with LLVM (`llvm-18-dev` package), `qcc` parses source, generates IR, and writes `.ll` files. Without LLVM, it operates in parse-only mode.  An earlier Bison/Flex-based parser and its helpers were removed in the LSP tech-debt cleanup (preserved in git history).

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
├── Sema.h / Sema.cpp          # Semantic-analysis pass (QLang::Sema) — runs between Module::Parse and CodeGen in ALL build modes (no BLANG_HAS_LLVM guard, so --parse-only is parse + sema). Resolves struct field/method references, annotates every determinable Expression with its resolved Type (the typed AST codegen reads), and reports located errors via the DiagnosticEngine. U4 added core type checking to this pass (all build modes): return-type match, valueless `return;` in a non-void function, incompatible initializers, call arity and argument-type compatibility, and invalid arithmetic operands — integer width promotion is the only implicit conversion. The corresponding silent codegen coercions were deleted (return-type fabrication via `getNullValue`/`CreateIntToPtr`, the dropped-initializer store) and the codegen expression-dispatch fallback now raises a loud internal compiler error instead of a silent nullptr
├── CodeGen.h                  # LLVM code generation class (QLang::CodeGen) — walks AST, emits IR
├── CodeGen.cpp                # CodeGen implementation: functions, statements, expressions, type mapping
├── Type.h                     # Core type system, base classes: Statement, Type, Symbol, Scope, Module, FunctionDefinition, VariableDefinition, EnumDefinition, GenericParam
├── Expression.h               # Expression/statement AST nodes: Expression, WhileStatement, ForStatement, ForInStatement, IfStatement, ReturnStatement, Block, ArrayLiteralExpression, IndexExpression, MethodCallExpression, RangeExpression, StringInterpolation, etc.
├── CompilerHelpers.h          # CompileError exception class, COMPILE_ERROR macro, SmartPtr ostream operator
├── RefCount.h                 # Standalone RefCount base class and SmartPtr<T> template (replaces jhcommon)
├── logging.h                  # Lightweight logging macros (replaces jhcommon logging)
├── FileLexer.h / FileLexer.cpp # Hand-written lexer (Lexer and LexerReader classes)
├── LexerReader.cpp            # File reader implementation for the lexer
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
├── BmodEmitter.h / BmodEmitter.cpp # .bmod interface file emitter (walks AST, emits pub declarations in BLang syntax)
├── ProjectConfig.h / ProjectConfig.cpp # blang.toml parser and project configuration (name, version, type, dependencies)
├── BuildCache.h / BuildCache.cpp # Content-addressable build cache (SHA-256 keyed, ~/.cache/blang/objects/)
├── sha256.h / sha256.c        # Standalone SHA-256 implementation (FIPS 180-4, no external deps)
├── FormatString.h / FormatString.cpp # Compile-time format string parser for print/println ({}, {:x}, {:.2f}, etc.)
├── SQLGen.h / SQLGen.cpp      # SQL generation from query AST nodes (SELECT/INSERT/UPDATE/DELETE, CREATE TABLE)
├── SchemaMigration.h / SchemaMigration.cpp # Schema migration engine (diff, CREATE/ALTER TABLE, preview/apply)
├── stdlib/
│   ├── net.b                  # BLang networking stdlib (Socket, ServerSocket, Selector, HttpServer + routing)
│   ├── fs.b                   # BLang file I/O stdlib (File, FileInfo structs + open/read/write/stat/mkdir/list_dir)
│   ├── timer.b                # Timers + event loop (timer.every/after, run/stop) for `on` handlers
│   └── io.b                   # Shared FileOps protocol documentation (currently duplicated in net.b and fs.b)
├── LexerTest.cpp              # Basic lexer test program
├── LexerTest2.cpp             # Advanced lexer test with position save/restore
├── test.b                     # Comprehensive BLang test source file
├── test_files/                # Test cases organized in pass/, fail/, cgfail/, xfail/ subdirectories
├── test_build/                # Build system integration tests (lib + bin project pairs)
│   ├── mathlib/               # Test library project (blang.toml type=lib, pub fn add/multiply)
│   ├── myapp/                 # Test binary project (blang.toml type=bin, depends on mathlib)
│   └── timerapp/              # Test binary project (blang.toml type=bin, imports stdlib timer)
├── examples/                  # Real example programs (each with a README + integration test script; written to shake out compiler/stdlib bugs)
│   ├── todo_app/              # REST API + SQLite todo app (table struct, query/first, @json, HTTP stdlib, bcc migrate)
│   ├── worker_pool/           # Concurrency example (spawn + channels): parallel prime counting
│   ├── calculator/            # Expression interpreter (enums, match, ?, Result) with colocated `bcc test` suite
│   ├── kv/                    # File-backed key-value CLI (sys.args, cli flags, env, fs, Option) with colocated `bcc test` suite
│   └── wordfreq/              # Generic-collections example (Map/Set/sort over strings, inference) — validation program for the generic-ARC unit
├── run_tests.sh               # Automated test runner script (runs qcc against pass/fail/cgfail/xfail test categories)
├── test_codegen.sh            # End-to-end codegen test script (parse -> IR -> compile -> link -> run)
├── test_lsp_nvim.sh           # Headless-Neovim LSP smoke (runs ci/nvim_smoke.lua; yellow SKIP without nvim)
├── ci/
│   └── nvim_smoke.lua         # Real-client smoke: vim.lsp.start(blangd) on an error fixture, asserts diagnostics arrive
├── editors/
│   ├── README.md              # Editor setup: Neovim config, VS Code note, generic stdio-client instructions
│   └── blang-vscode/          # TextMate grammar (syntax highlighting only)
├── test_lsp.sh                # LSP golden-transcript test script (drives blangd via tools/lsp_client.py, cmp vs test_files/lsp/*.expected.out; --update-goldens/--selfcheck)
├── tools/
│   └── lsp_client.py          # Golden-transcript LSP driver (python3 stdlib; strict framing — any stray stdout/stderr byte fails; sort_keys re-serialization; ${ROOT} scrubbing)
├── lsp/                       # blangd language server: Json.{h,cpp} (insertion-ordered JSON), Transport.{h,cpp} (Content-Length framing), Compile.{h,cpp} (reparse pipeline), Server.{h,cpp} (JSON-RPC dispatch), StringLexerReader.h, DocumentStore.h, blangd_main.cpp, JsonTest.cpp
├── docs/
│   └── language_design.md     # BLang language design specification
│   └── implementation_plan.md # Implementation task list and roadmap
├── runtime/
│   ├── blang_runtime.h/c     # Core runtime (ARC, thread pool, channels, async event loop)
│   ├── blang_string.h/c      # Safe string runtime (BlangString: refcounted, immutable, heap-allocated)
│   ├── blang_array.h/c       # Safe array runtime (BlangArray: refcounted, bounds-checked, growable)
│   ├── blang_json.h/c        # JSON encode/decode library (for @json annotation support)
│   ├── blang_net.h/c         # TCP socket + poll-based Selector + global event loop with timerfds (listen, accept, connect, read, write, timers)
│   ├── blang_fs.h/c          # File I/O + directory operations (open/close/read/write/seek/flush, stat/remove/mkdir/list_dir)
│   └── blang_db.h/c          # Database abstraction layer (connection, query, result; optional SQLite backend)
├── .github/
│   └── workflows/ci.yml      # GitHub Actions CI: parse-suite matrix (parse-only + with-llvm) plus executing jobs — golden-codegen, bcc-test, sanitizers (ASan/UBSan build + fatal leak-check), runtime-units (ctest in build + build-asan), fuzz (bounded libFuzzer), demos, lsp (blangd golden transcripts + jsonTest + inverted selfcheck + determinism + every fail/sema fixture re-verified via LSP publishDiagnostics + headless-Neovim real-client smoke) (see docs/ci.md)
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
| `blang_fs`  | Filesystem runtime library (static)| runtime/blang_fs.c (links blang_string, blang_buffer, blang_array)                 |
| `blang_db`  | Database runtime library (static)  | runtime/blang_db.c (optional SQLite via pkg-config)                                |
| `blang_sha256`| SHA-256 hash library (static)   | sha256.c                                                                           |
| `blang_buildcache`| Build cache library (static) | BuildCache.cpp (links blang_sha256)                                                |
| `blang_sqlgen`| SQL gen + migrations library     | SQLGen.cpp, SchemaMigration.cpp                                                    |
| `blangd`    | BLang language server (LSP over stdio) | lsp/blangd_main.cpp, lsp/Server.cpp, lsp/Compile.cpp, lsp/Json.cpp, lsp/Transport.cpp + BLANG_FRONTEND_SOURCES (never LLVM) |
| `jsonTest`  | LSP protocol-layer unit test       | lsp/JsonTest.cpp, lsp/Json.cpp, lsp/Transport.cpp (CTest: lsp_json)                |
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
httplib = { git = "https://github.com/x/httplib", tag = "v1.0" }  # or rev = "<commit>"

[database]                         # optional — default connection for queries/migrations
driver = "sqlite"                  # "sqlite" (tested) or "postgres" (requires libpq)
url = "app.db"                     # connection string; "env:DATABASE_URL" resolves at runtime

[database.analytics]               # optional named connection for @db("analytics") routing
driver = "postgres"
url = "env:ANALYTICS_URL"
```

The default `[database]` connection is opened in `main()` at startup and used by
`query`/`insert`/`update`/`delete` on tables without a `@db("name")` annotation;
`[database.<name>]` connections back `@db("name")` routing. With no `[database]`
section, the runtime falls back to the `BLANG_DATABASE_URL` environment variable
on first use (driver from `BLANG_DATABASE_DRIVER`, default sqlite).

**Build flow**: Dependencies are built recursively in topological order. Each dependency's artifacts (.a + .bmod) are cached in `~/.cache/blang/objects/<sha256>/`. On cache hit, the build is skipped. `.bmod` files are passed to `qcc` for type resolution; `.a` files are linked into the final binary.

**Stdlib in projects**: a binary project's `import <name>;` statements are resolved to `stdlib/<name>.b` and combined into the program (single combined `.ll`), so `import timer;` / `import net;` work in `bcc build` just as in single-file mode. Library projects do **not** embed stdlib (doing so would duplicate symbols when a downstream binary imports the same module); a lib that uses stdlib should leave the bodies to the final binary.

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
 ├─ 1. qcc source.b          → source.ll   (parse + sema + LLVM IR generation)
 ├─ 2. llc -filetype=obj      → source.o    (IR to native object)
 └─ 3. cc source.o -o program → program     (link to executable)
```

Inside step 1 the compiler runs `Lexer ─→ Parser (stamps SourceLocation) ─→ Sema ─→ CodeGen`.
The **Sema** pass (`Sema.h/cpp`) runs in **every** build mode — the LLVM build, the
non-LLVM (`BLANG_ENABLE_LLVM=OFF`) build, and every `--parse-only` compile — so
semantic errors are caught without LLVM installed. It resolves struct field/method
references (unknown field/method → a located `file:line:col: error:` diagnostic, in all
build modes) and annotates each determinable expression with its resolved `Type`; codegen
reads those annotations instead of re-deriving. Variable/function resolution stays in the
parser (already located); sema owns member resolution and does not double-report.

1. **Compiler driver — bcc** (`bcc.cpp`): User-facing CLI that orchestrates the full compilation pipeline. Shells out to `qcc`, `llc`, and `cc`. Handles `-S` (emit IR), `-c` (compile only), `-o` (output name), `-v` (verbose), and linker flags (`-l`, `-L`).

2. **Parsing + IR generation — qcc** (`QLang` namespace in `Type.h`, `Expression.h`, `Q*.cpp`, `qcc.cpp`): Hand-written recursive-descent parser that builds an AST from source files. When built with LLVM, also generates LLVM IR via `CodeGen`.

3. **Code generation — CodeGen** (`QLang::CodeGen` in `CodeGen.h/cpp`): Walks the QLang AST and emits LLVM IR using modern LLVM 18+ APIs (`IRBuilder<>`, opaque pointers, `FunctionCallee`). The `CodeGen` class is a friend of all AST node classes and uses `dynamic_cast` to dispatch to type-specific generation methods. Conditionally compiled (`BLANG_HAS_LLVM`).

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

- Every AST node carries a `SourceLocation {file, line, col}` (`SourceLocation.h`),
  stamped at parse time by each `Parse` factory. The lexer freezes each token's
  1-based line/column into its symbol-replay list (`FileLexer.*`,
  `LexerReader.cpp` counts in `popChar`), so `Lexer::getTokenLocation()` is
  accurate even after parser backtracking. `--dump-locations` renders these.
- `CompileError` exception class carries the offending token's `SourceLocation`
  (snapshotted at throw time via `COMPILE_ERROR`) plus the compiler-internal
  `__FILE__`/`__LINE__` (retained for a future `--debug-compiler` mode). Error
  reporting no longer reads the live lexer.
- `COMPILE_ERROR(lexer, message)` macro throws a `CompileError` with the token
  location and `__FILE__`/`__LINE__`.
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
- Source files use `using namespace QLang;` and `using namespace std;`

## Testing

### Test programs

- `lexerTest` / `lexerTest2` — Run against test source files to verify tokenization.
- `qcc <filename>` — Parses the given file and prints the AST. Outputs "Completed parse" on success.

### Test source files

Tests are organized into four categories under `test_files/`:

- **`test_files/pass/`** (113 tests) — Should parse successfully. Includes:
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
- **`test_files/fail/`** (41 negative tests) — Should fail to parse (exit non-zero): `annotation_missing_name.b`, `assert_missing_semi.b`, `async_missing_fn.b`, `bad_type.b`, `carray_in_fn.b`, `carray_in_var.b`, `const_no_init.b`, `cstring_in_fn.b`, `cstring_in_var.b`, `c_style_func.b`, `duplicate_func.b`, `enum_missing_brace.b`, `fn_missing_arrow_type.b`, `for_c_style.b`, `for_c_style_block.b`, `for_in_missing_in.b`, `generic_duplicate_param.b`, `generic_unknown_constraint.b`, `import_missing_name.b`, `import_missing_semi.b`, `insert_missing_brace.b`, `json_generic_struct.b`, `match_missing_brace.b`, `missing_brace.b`, `missing_paren.b`, `protocol_missing_method.b`, `protocol_no_fn.b`, `query_missing_table.b`, `requires_missing_expr.b`, `spawn_missing_brace.b`, `struct_bad_field.b`, `struct_missing_brace.b`, `table_missing_struct.b`, `test_missing_body.b`, `test_missing_name.b`, `undefined_func.b`, `undefined_var.b`, `var_no_init.b`, `wait_all_missing_semi.b`, `wait_missing_semi.b`
- **`test_files/fail/sema/`** (semantic-resolution negative tests) — Rejected by the semantic pass (or the parser, for var/func) with a located error, in **both** build modes. One fixture per resolution class: `undefined_variable.b`, `undefined_function.b`, `unknown_field.b`, `unknown_method.b`, each with a companion `<test>.b.expected` pattern. `run_tests.sh` additionally asserts the canonical `^[^:]+\.b:[0-9]+:[0-9]+: error: ` regex for **every** file under `fail/sema/` (the per-file check the epic done-condition requires). This category now holds **42** fixtures (epic blang-ast done-condition #3: >=25; modules-v2-exports U1 added five: `bodyless_function`, `bodyless_method`, `bodyless_init`, `reserved_name_fn`, `reserved_name_method`). It includes the ten numbered audit programs `audit_01.b`..`audit_10.b` — return/initializer/arity type errors (U4: 01-05), unknown field (U4: 10), generic-constraint-not-satisfied + non-exhaustive-match (U5: 08-09), shared-field-mutation + unguarded-spawn-capture (U7: 06-07) — the four resolution fixtures (U3), the relocated ownership fixtures `own_use_after_move`/`own_move_in_loop`/`own_spawn_capture`/`own_indirect_move` (U6, moved out of `cgfail/`), and per-diagnostic type-checking coverage (arg-type/arg-count/bad-operand/initializer/return/unknown-member/non-exhaustive). Every file is rejected in `--parse-only` (semantic stage) in both build modes with a canonical `file:line:col: error:` line matching its `.expected` pattern.
- **`test_files/cgfail/`** (5 codegen-fail tests) — Should fail at code generation (only run when built with LLVM, skipped in parse-only mode), e.g. `json_unsupported_field.b` and the `print_*` format checks. (The ownership `own_*` move tests moved to `fail/sema/` in U6 — they are now enforced by the semantic pass in all build modes.)
- **`test_files/codegen_*.b`** (63 end-to-end tests) — Full pipeline tests (parse → IR → compile → link → run) in `test_files/`, including `codegen_array.b`, `codegen_array_methods.b`, `codegen_assert.b`, `codegen_async.b`, `codegen_async_multi.b`, `codegen_binexpr.b`, `codegen_break_continue.b`, `codegen_comprehensive.b`, `codegen_contracts.b`, `codegen_enum_payload.b`, `codegen_features.b`, `codegen_file_io.b`, `codegen_forin.b`, `codegen_fs_convenience.b`, `codegen_generic_fn.b`, `codegen_generic_struct.b`, `codegen_http_blang.b`, `codegen_json.b`, `codegen_json_nested.b`, `codegen_json_roundtrip.b`, `codegen_json_types.b`, `codegen_lambda.b`, `codegen_lambda_callback.b`, `codegen_lambda_named_ref.b`, `codegen_ownership.b`, `codegen_ownership_move.b`, `codegen_phase2.b`, `codegen_pipeline.b`, `codegen_result_type.b`, `codegen_selector.b`, `codegen_shared_spawn.b`, `codegen_simple.b`, `codegen_spawn.b`, `codegen_spawn_threaded.b`, `codegen_string.b`, `codegen_string_interp.b`, `codegen_string_methods.b`, `codegen_sync_locking.b`, `codegen_sync_spawn.b`, `codegen_tcp_echo.b`, `codegen_try_operator.b`, `codegen_type_coercion.b`, `codegen_wait.b`, `codegen_wait_all.b`
Legacy test files (kept for reference): `test.b`, `test_files/func_call1.b`, `test_files/func_call2.b`, `test_files/func_call3.b`, `test_files/if_call.b`, `test_files/multi_var_decl.b`

**Total: 221 parse/sema tests** in the LLVM build / 214 in the parse-only build (the counts the suite itself reports; the earlier "195" figure had drifted) **+ 156 codegen E2E tests** (functional-hardening epic raised the codegen matrix from 85 to 107: aggregate/field ARC (`codegen_arc_*.b`), operator (`codegen_op_*.b`), interaction (`codegen_ix_*.b`), and stdlib-via-`bcc` tests)

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

# LSP golden-transcript tests (blangd built in build/ or BUILD_DIR)
./test_lsp.sh               # Drive blangd through test_files/lsp/*.lsp.jsonl fixtures, compare transcripts to goldens
./test_lsp.sh --update-goldens   # Regenerate transcript goldens
./test_lsp.sh --selfcheck   # Teeth proof: corrupted golden must go red (prints SELFCHECK: OK, exits non-zero)

# Manual testing
cd build
./lexerTest ../test.b
./lexerTest2 ../test.b
./qcc ../test.b
./qcc ../test_files/pass/fn_simple.b
```

The `run_tests.sh` script runs `qcc` against all test files in `test_files/pass/`, `test_files/fail/`, `test_files/cgfail/`, and `test_files/xfail/`, checking exit codes against expectations and printing a color-coded summary. The `cgfail/` tests are only run when qcc is built with LLVM; they are automatically skipped in parse-only builds.

The `test_codegen.sh` script runs the full compilation pipeline (qcc → llc → cc → run) for each `test_files/codegen_*.b` file. It **compares the compiled binary's captured stdout against a committed golden** `test_files/<name>.expected.out` (exact match, only a single trailing newline normalized) and fails on mismatch — not just an exit-code check (test-validation U1/U2). Non-deterministic tests (network/threading) are listed in `test_files/codegen_quarantine.txt`, which must diff-equal `docs/epics/test-validation/approved_quarantine.txt`; quarantined tests still run for exit code. `--update-goldens` regenerates goldens for deterministic tests; `--selfcheck` corrupts a real golden internally and asserts the suite goes red (teeth proof, exits non-zero printing `SELFCHECK: OK`); `--leak-check` runs each binary under ASan/LSan and is **fatal on any leak** (test-validation U4) — and when `build-asan/` archives exist it links the **ASan-instrumented runtime** (`ASAN_BUILD_DIR` overrides), so use-after-free reads *inside* runtime code (`__blang_rc_release`/`retain`) are trapped, not just leaks (plain archives only intercept malloc/free). Default (non-leak) runs execute each binary with `MALLOC_PERTURB_`/`MALLOC_CHECK_` so freed-memory reads fail loudly instead of passing on allocator luck. It automatically links the runtime library (`libblang_runtime.a`) and JSON library (`libblang_json.a`) when available.

The runtime C libraries have dependency-free unit tests registered with CTest (test-validation U5): `ctest --test-dir build` (and `ctest --test-dir build-asan` to run them under AddressSanitizer) exercises `blang_array/string/buffer/net/fs/json` core operations and bounds/error paths. A sanitizer build of the compiler is opt-in via `cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined`. A bounded libFuzzer target `fuzz_parse` (opt-in `-DBLANG_FUZZ=ON`, clang-only, dedicated `build-fuzz` dir) fuzzes the lexer+parser with a committed seed corpus under `test_files/fuzz/corpus/` (test-validation U6).

### Continuous Integration

`.github/workflows/ci.yml` runs, on every push/PR, jobs that each **execute** a real check (never a grep of ci.yml): `parse-suite` (parse-only + with-llvm matrix, `run_tests.sh`), `golden-codegen` (`test_codegen.sh` golden compare + golden-floor/quarantine anti-widening + `--selfcheck` teeth), `bcc-test` (fixture pass/fail/`--filter` assertions), `sanitizers` (`build-asan` build + `BUILD_DIR=build-asan ./run_tests.sh` + fatal injected-leak leg + clean `Leaks: 0` leg), `runtime-units` (`ctest` count ≥30 in `build` and `build-asan`), `fuzz` (corpus replay + bounded `-max_total_time=60` campaign), and `demos` (`make -C demos run`). Configuring these as GitHub branch-protection "required" checks is a documented **manual manager step** — see [docs/ci.md](docs/ci.md).

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
  - **Recursive enums**: a variant payload may name an enum — including the enclosing one (`enum Expr { num(int), add(Expr, Expr) }`). Enum-typed payloads are **boxed** (stored as a pointer to a heap child allocated via `__blang_rc_alloc_dtor` with a generated `__enum_<Name>_box_dtor`), so recursive sum types have finite layout and real ASTs are expressible. Construction from an existing owner (variable/field source) retains the copied value's payloads via a generated `__enum_<Name>_payload_retain`; fresh temps transfer ownership. Match bindings unbox to a by-value borrow. Enum variable reassignment releases the old value's payloads; returning an enum variable retains its payloads for the copy (non-generic enums). Leak-clean under valgrind and `--leak-check` (`codegen_enum_recursive.b`)
  - **Multi-binding destructuring**: `pattern(a, b, ...)` binds one variable per variant associated type at sequential payload offsets (`move(x, y)`, `color(r, g, b)`); binding more values than the variant carries is a located Sema error
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
  - **Match as expression**: in expression position each arm holds a single expression (`int a = match s { circle(r) { r * 3 } _ { 0 } };`) — arm types must agree (Sema-unified, located error on mismatch), the match is annotated with the unified type, and exhaustiveness is required (enum: all variants or `_`; non-enum subject: `_` mandatory). Codegen stores each arm's value into an entry-block result slot with the return-statement ARC policy (retain borrowed variable/field/index sources, take ownership of fresh temps) and yields the loaded value as a tracked temporary
- Expressions: arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`), bitwise (`&`, `|`, `^`, `<<`, `>>`)
- **Built-in `Option<T>` / `Result<T,E>`**: available without any user definition — `Option.some(x)`/`Option.none`, `Result.ok(x)`/`Result.err(e)`, consumed via `match` and `?`. A user-defined `Option`/`Result` shadows the built-in.
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
- **Channels** — `chan<T>` typed channels with `.send(value)`, `.close()`, and `.recv() -> Option<T>` (`some(value)` on success, `none` when closed and empty) operations (buffered, thread-safe; usable across `spawn` boundaries). `T` must be a **value type** (`int`, `long`, `bool`, `float`, …): channels transfer elements by raw byte copy, so a refcounted heap element (`string`/`Array`/`Buffer`/struct) is rejected by `Sema` with a located error in all build modes (a byte copy cannot own a reference count). `.recv()` yields the built-in `Option<T>`, so a `match` that ignores the closed/empty `none` case is a non-exhaustive-match compile error.
- **Async functions** — `async fn name() { }` for event-loop scheduled functions
- **Await expressions** — `await expr` for async value retrieval
- **Event handlers** — `on expr { ... }` registers the body on the global event loop keyed by the fd `expr` yields (e.g. `on timer.every(1000) { ... }`). Registration does **not** fire the handler; the program enters the loop explicitly with `timer.run()`, which blocks until stopped (`timer.stop()`) or until no event sources remain (e.g. all one-shot timers have fired). Keeping the loop entry explicit means code before `run()` runs to completion first — nothing in the `main` body is silently preempted by a timer. `timer.after(ms)` (one-shot), `timer.cancel(source)` (remove one timer) are also available. Timer + socket fds share one poll loop.
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
- **Builtin `to_json`** — `to_json(value)` serializes a `@json`-annotated struct to a JSON string (compile-time dispatch to `StructName_to_json`; compile error if not `@json`). Composes for auto-JSON HTTP responses: `net.http_json(to_json(user))`
- **`@format` annotation** — validates call-site format strings against arguments at compile time for user-defined format functions

## Project Status

This is an active work-in-progress. The recursive-descent parser can parse BLang source into an AST, and when built with LLVM, the `CodeGen` class generates LLVM IR for the parsed AST.

**Parser features**: BLang `fn`-style function declarations (C-style syntax rejected), `extern fn` declarations, struct definitions with generic parameters, enum/sum type definitions with variants and associated types, enum variant construction (`EnumName.variant(args)`), protocol definitions with generic parameters and conformance checking, generic functions with protocol constraints, generic type arguments on struct literals and function calls, for-in loops (range iteration, collection iteration, infinite loops), array literals and indexing, method calls, field access, range expressions, pattern matching with wildcards and destructuring bindings, `?` try operator for error propagation, `import` statements with dotted paths, `pub` visibility modifier, duplicate function detection, float/double literals, break/continue, extern declarations with unnamed parameters, multi-file compilation, `.bmod` interface file emission (`--emit-bmod`) and consumption (two-phase parsing with flat symbol merge), ownership qualifiers (`own`, `shared`, `sync`) on variable declarations and function parameters, spawn blocks, async/await, event handlers (`on`), contract clauses (`requires`/`ensures`), test blocks, assert statements, pipeline operator (`|>`), annotations (`@name("arg")`) with compile-time validation (e.g., `@json` rejected on generic structs), table structs (`table struct`), query/insert/update/delete expressions with pipeline steps, boolean constants (`true`/`false`), FFI types (`cstring`, `carray<T>`) restricted to `extern fn` declarations with compile-time enforcement, function types (`fn(ParamTypes) -> RetType`) as first-class types, lambda expressions (`fn(params) -> RetType { body }`) with automatic closure capture, indirect calls through fn-typed variables, and named function references as values.

**Lexer keywords**: `fn`, `bool`, `struct`, `impl`, `self`, `protocol`, `match`, `import`, `pub`, `break`, `continue`, `enum`, `in`, `own`, `shared`, `sync`, `spawn`, `chan`, `async`, `await`, `on`, `requires`, `ensures`, `test`, `assert`, `table`, `query`, `insert`, `update`, `delete`, `cstring`, `carray`. Additional tokens: `->` (arrow), `..` (range), `_` (wildcard), `?` (try operator), `|>` (pipeline), `@` (annotation), `true`/`false` (boolean constants), float constants.

**CLI features**: `qcc` supports `--parse-only`, `-S`/`--emit-ir`, `-c`/`--emit-obj`, `-o`/`--output`, `--emit-bmod <file>`, `--combine` (compile multiple `.b` files into a single `.ll` with shared scope), `--dump-locations` (print one `<file>:<line>:<col> <NodeKind>` line per AST node in deterministic pre-order, then exit; parse-only, no LLVM dependency; locked by the golden files under `test_files/golden/` — one `<name>.locations` per `test_files/pass/<name>.b` fixture (func_simple, match_basic, method_call, enum_variants), enforced by a dedicated leg in `run_tests.sh`; member accesses are located at the member-name token and enum variants carry their own `EnumVariant` location lines), `-v`/`--verbose` (emit parse-progress/trace output; quiet by default), `--debug-compiler` (append compiler-internal detail to errors — the C++ throw-site of a `CompileError`, and the raw LLVM verifier text on an IR-verification ICE), `--help` flags and multiple input files (`.b` and `.bmod`). Compiles are **quiet by default**: a clean compile prints nothing on stdout/stderr; user-facing errors are single located diagnostics formatted `<file>:<line>:<col>: error: <message>` (routed through one `DiagnosticEngine`), never the compiler's own C++ coordinates or raw LLVM IR. `run_tests.sh` supports an **expected-error mode** for `fail/`/`cgfail/` tests: a companion `<test>.b.expected` file (or an inline `// EXPECT-ERROR: <pattern>` comment) declares an ERE the compiler's stderr must match — the test passes only if the compiler exits non-zero AND the pattern matches (tests with no declaration keep exit-code-only judgement). `bcc build` reads `blang.toml`, resolves dependencies, and builds projects (lib→.a+.bmod, bin→executable) with content-addressable caching. `bcc clean` removes the build cache. `bcc test [--filter <name>] <file.b>` is a **real test runner** (test-validation U6): it compiles the file with `qcc --emit-test-main` (which emits a `main()` that registers each `test{}` block and dispatches to the fork-isolated C driver `runtime/blang_testrunner.c`), runs each test in a forked child so one failure never aborts the rest, prints per-test `PASS`/`FAIL`, a `<file>:<line>:` located diagnostic on a failed `assert`, and a `N passed, M failed` summary, and exits non-zero iff any test failed; `--filter <substr>` runs the name-matching subset. With no file argument it falls back to discovery (`tests/` or the current directory). `bcc migrate` subcommand supports `--preview`, `--apply`, and `--generate` modes for schema migrations.

**Runtime libraries**: `blang_string` (safe immutable string type — `BlangString` with refcounting, heap allocation, and bounds-checked access), `blang_array` (safe generic array type — `BlangArray` with refcounting, bounds-checked access, and dynamic growth), `blang_json` (C library for JSON encode/decode), `blang_net` (TCP sockets + poll-based Selector event loop — listen/accept/connect/read/write/close, event-driven I/O with fd-based handle table), `blang_fs` (file I/O + directory operations — open/close/read/write/seek/flush, stat/remove/mkdir/list_dir), `blang_db` (database abstraction with optional SQLite backend).

**Stdlib**: `stdlib/net.b` defines BLang-native types — `Socket`, `ServerSocket` (with methods via `impl FileOps`), `Selector` (with `on_accept`, `on_data`, `run`, `shutdown` methods using lambda callbacks), a routed HTTP layer — `HttpServer` (with a `_routes` table and `get`/`post`/`put`/`route(method, path, handler)` registration, `serve()` to parse+dispatch each accepted connection, and `on_request`/`on_stream_request` for single catch-all/streaming handlers), the `Route` struct, and `dispatch_request(routes, req)` (a pure method+path matcher returning the handler's response or 404 — testable without a live socket), plus `HttpRequest` (with `method()`/`path()`/`body()` accessors) / `HttpResponse` (with `status()`/`content_type()`/`body()` accessors) and `http_ok`/`http_json`/`http_not_found` response builders, plus `socket_from_fd(int)` (wrap a raw fd in a `Socket` without a struct literal) — and BLang-native HTTP utility functions: `build_http_response()` (response building via string interpolation), `parse_http_request_line()` (Buffer-based request line parsing into `HttpRequestLine` struct, with `method()`/`path()`/`version()` accessors), `parse_http_headers_from_buffer()` (Buffer-based header parsing into `HttpParsedHeaders`, whose parallel key/value arrays are read through a designed `count()`/`key(i)`/`value(i)`/`has(name)`/`get(name)` surface rather than the raw arrays), `extract_http_body()` (extracts body after `\r\n\r\n` separator), `http_get_buffered()` (BLang-native HTTP GET client using Buffer I/O), and `http_status_text()` (status code to reason phrase). These pure-BLang functions complement the C-backed HTTP types. Compiled into user programs via `qcc --combine stdlib/net.b user.b`; `bcc` auto-includes stdlib when available. `stdlib/fs.b` defines BLang-native types — `File` (with `read`, `write`, `close` FileOps methods plus `read_into`, `read_line`, `read_all`, `flush`, `seek`, `tell`, `size`), `FileInfo` (with `pub` `exists`, `is_file`, `is_dir`, `get_size` accessors), and free functions: `open()`, `read_all()`, `write_all()`, `append()`, `info()`, `exists()`, `is_dir()`, `file_size()`, `remove()`, `mkdir()`, `list_dir()`. `stdlib/io.b` documents the shared `FileOps` protocol (currently duplicated in net.b and fs.b due to namespace visibility limitations). `stdlib/collections.b` defines `Map<K,V>` (key-value store using parallel arrays with `has`, `get`, `set`, `remove`, `length`, and `keys()`/`values()` accessor methods) and `Set<K>` (with an `items()` accessor), usable through `bcc` via `import collections;` and referenced unqualified (`Map<string,int> m = Map<string,int> { keys: [], values: [] };`): in combine mode `collections` is parsed into the shared scope like `buffer`, so its `Map` type resolves in a variable declaration. `bcc` combines `collections.b` only when the program `import`s it; `test_codegen.sh` combines it for tests containing `import collections;`. SQL generation (`SQLGen`) converts query AST nodes to parameterized SQL. Schema migration (`SchemaMigration`) diffs table struct definitions against stored schema and generates CREATE/ALTER TABLE statements.

**Codegen features** (requires LLVM): function definitions, extern function declarations, variadic function calls, variable declarations with initialization, return statements, if/else (no parentheses), while (no parentheses), for-in loops, function calls, binary expressions (arithmetic, comparison, logical, bitwise with correct operator precedence and automatic integer type coercion for mixed-width operands), assignment expressions (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `^=`), constant expressions (int, float, string, char), break/continue in all loop types (while, for, for-in, infinite), safe string type (`BlangString` — literals via `__blang_string_create_static`, concatenation via `__blang_string_concat`, comparison via `__blang_string_equals`, interpolation via `__blang_string_concat_many` with per-type to_string helpers, refcount tracking at scope boundaries), safe array type (`BlangArray` — literals via `__blang_array_create`/`__blang_array_push`, bounds-checked indexing via `__blang_array_get`/`__blang_array_set`, for-in iteration, refcount tracking at scope boundaries), `cstring`/`carray<T>` FFI types with automatic conversion at extern fn boundaries (string→cstring extracts `.data` field, cstring→string wraps via `__blang_string_create`), builtin method calls on string (`.length`, `.is_empty`, `.to_upper()`, `.to_lower()`, `.trim()`, `.contains()`, `.starts_with()`, `.ends_with()`, `.index_of()`, `.substring()`, `.replace()`) and Array (`.length`, `.is_empty`, `.push()`, `.pop()`, `.clear()`), pipeline operator (`|>` desugaring), ARC retain/release at scope boundaries for shared/sync variables, runtime init/shutdown in `main()` for concurrency features, spawn blocks with closure extraction (captured variables packed into context struct, dispatched to thread pool via `__blang_spawn`, `own` variables cannot be captured across spawn boundaries), channel variable declarations (`chan<T>` → `__blang_chan_create`), async function wrappers (body extracted to `void*(void*)`, called via `__blang_async_call`), await expressions (`__blang_await` + `__blang_task_destroy`), event handler callback extraction, database query/insert/update/delete codegen (SQL generated at compile time via `SQLGen`, emits `__blang_db_query`/`__blang_db_exec` calls), enum/sum type tagged union layout (`{i32 tag, [N x i8] payload}` with variant construction and match-based payload extraction), generic struct/function monomorphization (stamps out concrete versions like `Box_int` with mangled names), ownership move semantics enforcement (use-after-move and move-in-loop detection), `@json` annotation codegen (generates `StructName_to_json`/`StructName_from_json` functions supporting all primitive types, nested `@json` structs, with compile-time errors for unsupported field types), `?` try operator codegen (resolves operand's enum type, extracts tag, branches on ok/some vs err/none, unwraps payload on success, propagates error via early return on failure), multi-module type sharing (`registerExternalTypes` shares struct/enum definitions across CodeGen instances for cross-module type resolution), combined compilation mode (`--combine` feeds multiple modules into a single CodeGen instance with shared scope, deduplicating extern/method declarations for stdlib integration), builtin `print`/`println` with compile-time format string validation (positional `{}` placeholders, format specifiers `{:x}`,`{:X}`,`{:o}`,`{:b}`,`{:.Nf}`,`{:e}`, Printable protocol for user-defined types, `@format` annotation for call-site validation on user-defined functions), and lambda/callback codegen (anonymous functions with closure capture via context struct + malloc, function types as `{fn_ptr, ctx_ptr}` pairs with parameter expansion, indirect calls through fn-typed variables, named function references via thunk wrappers with null context). The full pipeline (parse → LLVM IR → native binary) is tested end-to-end with 156 codegen E2E tests, each with a committed stdout golden (except the 7 quarantined non-deterministic network/threading tests). A discarded `query T |> ...;` result (an `Array<T>` rvalue never bound to a variable) is released at statement end via the temp-array tracking, so the database codegen tests are leak-clean under `--leak-check`.

The long-term goals (from README.txt) include integrated threading, eventing, garbage collection, FPGA synthesis support, and networking in the standard library.

## Known Issues and Limitations

- Built-in `Option<T>`/`Result<T,E>` use a type-erased pointer-sized (8-byte) payload slot. All primitives, pointers, and heap structs fit directly, and an **enum value payload is boxed** into the slot (heap child, unboxed at the match binding / `?` unwrap with ownership transferred to the copy) — so `Result<Ast, string>` and `Option<Ast>` work (`codegen_generic_enum_payload.b`; the calculator's parser returns `Result<Ast, string>`). The concrete type argument is recovered at the match/`?` site from the subject's static type; expressions whose static type does not carry the type argument fall back to the erased slot.
- Async function return value boxing/unboxing is simplified (heap-allocated, not yet optimized).
- Event handlers (`on EXPR { }`) register on the global poll-based event loop when EXPR yields an fd (timerfd/socket fd); registration does not fire the handler — the program enters the loop explicitly with `timer.run()` (blocks until `timer.stop()` or no sources remain). This is deliberate: it keeps control flow explicit (no hidden loop after `main`) and avoids the footgun where blocking work in `main` would silently delay handlers. A non-fd event expression still falls back to inline invocation. The HTTP server's Selector is a separate loop instance (not yet unified with the global `on` loop).
- Query result sets ARE mapped back to BLang values: `query T |> ...` returns an `Array<T>`, mapping each result row to a heap-allocated `T` (SELECT * column order == struct field order; int/float/string/bool columns supported), so `Array<Todo> todos = query Todo;` then `for t in todos { ... }` / `todos[i]` works. A pipeline ending in `|> first` returns the built-in `Option<T>` instead — `some(row 0)` when a row matches, `none` otherwise (SQL gets `LIMIT 1`; consumed via `match`, exhaustiveness enforced) — Sema annotates the query expression `Option<T>`/`Array<T>` so assigning a `first` result to an `Array<T>` (or vice versa) is a compile error. `insert`/`update`/`delete` still return affected-row counts. Table struct fields must have a SQL column mapping (int/long/short/char/bool/float/double/string); a nested struct/array field is a located Sema error in all build modes (previously it was silently left null — a guaranteed null-deref on first access).
- PostgreSQL backend is implemented (libpq, with `?`→`$n` placeholder rewrite) but only compiled when `libpq-dev` is present and has not been exercised in CI; SQLite is the tested backend. Connection pooling is not implemented — the process uses a single shared default connection per name (sufficient for SQLite/single-threaded use).
- Multi-module codegen shares type definitions (structs/enums) across modules. Cross-module function linking works via `.bmod` interface files and `bcc build` (functions from deps are auto-declared as extern and linked from `.a` archives).
- **Three type tiers: core / prelude / library** (modules-v2-graph U3, D12/D13/D14). The hardcoded `isGlobalTypeLib` promotion list is replaced by a declared, closed **prelude manifest** — exactly `{Map, Set, Buffer}` (types only; `isPreludeTypeName`, `Frontend.h`/`QModule.cpp`). Prelude types are **automatically in scope with zero imports**: their modules (`buffer.b`, `collections.b`) are **unconditionally loaded** (bcc + `test_codegen.sh`) but LOADED into a prefix-free **holding namespace scope**, then a per-module **PROMOTE** injects only the prelude *types* into a dedicated **prelude scope** that PARENTS the user's combine scope — so a user's own `struct Map` **shadows** the prelude uniformly by child-first lookup (both scope registries), with an `allStructs` shadow filter reconciling CodeGen's `mStructDefMap` (the third registry). This LOAD-vs-PROMOTE split keeps prelude symbols out of the user scope at parse time (no split-brain). `collections` is a **mixed module**: `Map`/`Set` → prelude; the free function `sort` → qualified `collections.sort(...)` (library tier; bare `sort(` call sites migrated). The bare-name `addType("Buffer")` is **deleted** (D14): `Buffer` resolves from `stdlib/buffer.b`, and absent the prelude a `Buffer` type fails at the declaration (`fail/sema/buffer_without_prelude.b`). `Task`/`Array` stay registered as **core**. **`cli` stays promoted this unit** — demoting it to a qualified library module is **deferred out of U3 by owner decision** (F3-gated) to a later unit (KG-6); U2 was characterization-only and retired no promotion, so done-condition 2 is **partial** on the cli item. Fixtures: `codegen_prelude_no_import.b` (zero-import Map/Set/Buffer), `codegen_prelude_shadow.b` (user Map with a distinct API shadows the prelude), and the three self-`Map` tests (`codegen_map.b`, `codegen_arc_map_struct_value.b`, `codegen_ix_method_chain_field.b`) as the shadowing regression guard. See `specs/036-type-tiers/`.
- **Canonical module identity keys generic mangling** (modules-v2-graph U1, design record D5/D10). A generic type's mangled symbol incorporates a 12-hex (48-bit) SHA-256 digest of its **defining module's canonical origin** (`realpath` of the project dir; `url@pin` for git), so two libraries that each export a same-named generic type (e.g. `Box<int>`) get **distinct** symbols instead of collapsing onto one `linkonce_odr` symbol (P10, a silent miscompile). The digest is stamped by the driver: `bcc` passes `--module-origin <realpath(projectDir)>` for the module being compiled and `--bmod-origin <depBmod>=<realpath(depDir)>` for each directly-imported dep, so a consumer stamps a dep's `.bmod` definitions with the **dep's** identity — mangling them identically to the dep's own build, which preserves cross-module `linkonce_odr` dedup (mathlib/myapp). `realpath` failure is a located hard error, never an empty identity. Single-file/combine mode (no `bcc`) defaults each module's origin to `realpath(inputFile)`. The digest is applied at `mangleGenericName` (`CGTypes.cpp`) and mirrored in the generic-instance dtor/type-lookup names (`getOrGenStructDestructor`, `CGStruct.cpp`). Helpers `blangModuleDigest`/`stampModuleDigest` live in shared frontend plumbing (`QModule.cpp`, `Frontend.h`) so `qcc` and `blangd` derive identical digests. Proven by `test_build/boxa`+`boxb`+`boxapp` (`nm` shows two distinct `Box_m<digest>_int` families; same-origin dedup + clean-rebuild guard). See `docs/epics/modules-v2-graph/design-audit-U1.md`, `specs/034-canonical-module-identity/`.
- **The module-prefix string-ARC path is proven clean and regression-locked** (modules-v2-graph U2). A namespaced, module-prefixed module's internal string-returning call (`has_flag → flag_name_of`) once double-freed under the module-prefix codegen — the reason `buffer`/`collections`/`cli` are promoted into the user scope instead of getting a real module boundary (rationale `qcc.cpp:303-307`). That double-free was fixed by a post-2026-07-20 return-retain/borrow ARC change and **no longer reproduces**: the return-retain/temp-tracking decision reads the call's AST `FunctionDefinition` pointer (`callReturnTypeName`, `CGTypes.cpp:1179`), which is prefix-independent — the module prefix only renames the emitted LLVM symbol (`CodeGen.cpp:157-158`), never the ARC decision — so the general fix repaired every prefixed string-return shape uniformly. `test_files/nsarc.b` + `codegen_nsprefix_arc.b` lock it in: a namespaced module kept **out** of every promotion list (so `qcc` emits `@nsarc__`-mangled internal callees, asserted by the harness) exercised under `--leak-check` against the ASan-instrumented runtime. This unblocks retiring the promotions (U3 demotes `cli`). See `specs/035-module-prefix-codegen-fix/characterization.md`.
- **Cross-module struct construction and method calls work** (modules-v2-exports U1). A non-generic `pub struct` ships its `init` and method SIGNATURES in the `.bmod` (an `impl` block of bodyless declarations), so a consumer can write `Counter c = Counter(5);` and `c.bump()` — previously the `.bmod` carried only fields, so an imported struct could be poked at but neither constructed nor called (design record P8). Construction goes through a **library-emitted factory** `__<Struct>_new(args) -> ptr`, which wraps `__blang_rc_alloc_dtor(size, dtor)` + `<Struct>_init`: the consumer never computes a foreign struct's size and never generates its destructor, and release stays `__blang_rc_release` (the dtor pointer is stored in the allocation header, so that path was already layout-free — no runtime change). The factory symbol lives in the reserved `__` family so it can never collide with a user method named `new`, and it is never injected into any scope, so construction keeps exactly one source spelling. Generic structs are unaffected: they still ship full layout and all method bodies for consumer-side monomorphization, and are constructed inline. Proven end to end by the `test_build/counterlib` + `test_build/counterapp` fixture pair, which `test_build/run_build_tests.sh` also relinks against the `build-asan/` runtime and runs under ASan/LSan (`detect_leaks=1`) — the first program whose destructor is installed by one module and invoked by another; the leg skips loudly when the ASan archives are absent.
- **A bodyless method or `init` is a located error in ordinary source** — it is an interface form, valid only in a `.bmod` (and in `protocol` declarations). Previously it silently compiled to an empty function returning zero, and, cross-module, would have collided at link with the real definition. Imported method signatures now lower to LLVM `declare`, never `define ... ret 0`.
- **The `.bmod` is a versioned interface** (modules-v2-exports U2). It carries a `// blang-bmod-format: N` marker line (`BmodFormat.h`, `BlangBmod::kFormatVersion`), which is salted into `BuildCache::computeKey` — so a format change invalidates every warm cache entry instead of serving a stale interface to a newer compiler (proven by the `build_cache_key` ctest). It also carries **protocol conformance records** (`impl Printable for Point { }`, emitted as empty impl blocks after the interface block so conformance checking sees the struct's accumulated methods), which is what makes `print("{}", x)` dispatch through `Printable` on an *imported* type (design record D16). `.bmod` content is pinned by committed goldens under `test_files/golden/bmod/` (regenerate with `UPDATE_GOLDENS=1 ./test_build/run_build_tests.sh`).
- **`pub table struct` now round-trips.** The emitter wrote `table pub struct`, the inverse of source order — and that form is a *hard parse error* (`Expected 'struct' after 'table'`), so a `table` struct's interface file could not be read by any consumer at all.
- **`print("{}", x)` on a `Printable` struct passed the wrong `self`** and printed garbage (fixed in U2). A struct value is a heap pointer, but the print path boxed it in an alloca and passed the alloca — a pointer to the self pointer. Calling `x.to_string()` directly was always correct, which is why it survived; regression test `codegen_printable_dispatch.b`.
- **`pub` on impl members; methods and `init` are private by default** (modules-v2-exports U3, design record D9). `pub fn` / `pub init` / `pub static fn` parse inside `impl` blocks; an unmarked member is module-visible only and is **not emitted into the `.bmod`**, so another module cannot reach it (located error: `type 'Counter' has no method 'reset'`). A **private `init` is still declared** in the interface, as a bare `init(...)` with no `pub`, so a consumer's `Counter(5)` says *"constructor of struct 'Counter' is private to its defining module"* rather than "no constructor" — an author can act on the first and cannot on the second. A `.bmod` whose format version differs from the compiler's is **rejected on read** with a located diagnostic (`interface file was produced by a different compiler version ... rebuild the dependency`) rather than silently reinterpreted. `.bmod` format **3** introduced this: `pub init` means externally constructible and a bare `init` means declared-but-private (in format 2 an unmarked member was *exported*, because `pub` could not yet be written). (U5a bumps the format to **4** when field layout is dropped — see the U5a entries below.) Generic structs are unaffected — they keep full field layout and ALL method bodies, since consumers monomorphize and `pub` methods call private helpers.
- **P9 enforcement: an exported declaration may only reference exported types** (U3), at the **library** build, in all build modes. Covers a `pub fn`'s params/return, a `pub` method/`init` signature, an exported enum's variant payloads (D17), a protocol named in an exported conformance record, and the methods backing an exported conformance (they must be `pub`, non-generic structs only — a generic struct ships all bodies, so the filter never runs on it). Generic type ARGUMENTS are checked too: `pub fn take(Box<Secret> b)` exports `Secret`. **Field types are checked only for `table`/`@json` data-contract structs** (U5 narrowing — a plain struct no longer ships field layout, so its field types never cross; the reproduction stays live for the `@json` case via `p9_pub_struct_private_field.b`, and the plain variant flipped to a `pass` fixture `pub_struct_plain_private_field_ok.b`). Previously such a library built **green, exit 0, no warning**, and the failure landed on the *consumer* as a syntax error inside a generated file they never wrote. Fixtures: `test_files/fail/sema/p9_*.b`.
- **Member variables are always private; struct literals and field access on an imported type are module-private** (modules-v2-exports U5a, design record D9/D7). A `pub struct` exports its `pub` methods/`init`, never its fields. Across a `.bmod` boundary a struct is **opaque**: `imported.field` is a located error (`field 'count' of type 'Counter' is private to its defining module`) and `Imported { ... }` is a located error (`struct literal for imported type 'Counter' is not permitted; construct it with Counter(...)`), so external construction has exactly one form — `Counter(5)` via `pub init`. Same-module access is unchanged (private == module-visible: `self.field`, cross-struct access, and struct literals all work in the defining module). The rules key on `StructDefinition::isFromInterface()` (arrived through a parsed `.bmod`) — so they are **`.bmod`-path-only** this epic; combine-mode namespaced-stdlib field/literal privacy stays grep-gated (`tools/check_no_field_reachins.sh`) until Epic B's per-module scopes (KI-23). **Generic imported structs were excluded in U5a** (they ship full layout + bodies for monomorphization and had no non-literal construction form yet — Q-U4-1); U5b lands generic field/literal enforcement coupled to the `Name<Args>(...)` construction spelling (see the U5b bullet below). Fixtures: `test_files/fail/xmodule/imported_field_access`, `imported_struct_literal`, `imported_datacontract_field`.
- **`.bmod` format 4: field layout is dropped for a plain `pub struct`; `table`/`@json` structs keep theirs as D15 metadata** (U5a). A non-generic, non-data-contract `pub struct` emits an **empty body** (`pub struct Counter { }`) plus its `pub` method/`init`/factory signatures — a private field's type is no longer part of the interface hash, so internal edits stop rebuilding downstream. A `table`/`@json` struct keeps its real field declarations (its shape is its contract, so editing a field correctly moves the hash), and those fields are compiler-facing metadata: an imported data-contract struct stays **queryable** (`query T |> where { .field == ... }`) and **serializable** (`to_json(x)`) from a consumer, while its fields remain **un-nameable** in ordinary source (`CGRuntime.cpp` `genToJsonCall` declares the imported `_to_json` extern and links it from the lib's `.a`). Generic structs are unaffected (full layout + all bodies keep shipping, A6). Proven end-to-end by `test_build/todolib`+`todoapp` (sqlite): schema created from the table struct's canonical source, then `query`/`to_json` on the imported struct run cross-module (done-condition 6).
- **Generic construction spelling `Name<Args>(...)` + generic field/literal privacy** (modules-v2-exports U5b). A generic struct is now constructed via `Name<Args>(args)` (parser builds a `ConstructExpression` carrying the type args; `genConstructExpression` monomorphizes the struct for those args and calls its `init`) — symmetric with the non-generic `Counter(5)`. With that migration path in place, U5b **removes the U5a generic exclusion**: a generic struct that arrived through a `.bmod` gets the same field/literal privacy as a non-generic one (`imported.field` and `Imported<T> { ... }` are located errors; the literal error suggests the type-arg-rendered `Pair<int>(...)`). Emission is unchanged — generics still ship full layout + ALL method bodies for consumer monomorphization (A6); visibility for generics is a pure Sema resolution rule. A latent ARC gap surfaced here and was fixed: `genFieldAssignment` now resolves the field's type name through the active monomorphization substitution **before** the string-retain check, so a generic string field (`T value`, T→string) is retained on store instead of dangling (the first field-assignment of a T-typed string field — existing generic-string paths went through literals/concat). `stdlib` `Map`/`Set` gained `pub init()` and their construction migrated from struct literals to `Map<K,V>()`/`Set<K>()`. Fixtures: `test_files/fail/xmodule/imported_generic_{field_access,struct_literal}`, `codegen_generic_construct.b`.
- **`Type.from_json(str)` — type-directed JSON deserializer** (modules-v2-exports U5b, OQ#1). Symmetric with the value-directed `to_json(value)`: `Todo.from_json(req.body())` parses a JSON string back into a `Todo`, dispatching through the compiler so neither serializer names a mangled symbol (D15: one canonical spelling). A `@json` struct already registers a `<Struct>_from_json(string) -> Struct` extern at parse; the static-method parser resolves `Type.from_json(args)` to it, so all existing Sema arg-checking and codegen dispatch apply unchanged, and the bare `Todo_from_json(str)` spelling still works. Fixture: `codegen_from_json_static.b`.
- **KI-22 fixed: `for x in <generic-method-call>()` resolves the loop-var element type** (modules-v2-exports U5b). A for-in whose iterable is a method call returning `Array<K>` (e.g. `for k in m.keys()`) now substitutes the receiver's type args through the method's return element type (`methodReturnElementType`), and the iterable's owning array temp is released once after the loop instead of by the body's block-scope cleanup (the actual use-after-free). Idiomatic opaque-collection iteration no longer needs the intermediate-typed-var workaround. Fixture: `codegen_forin_generic_method.b`.
- **The reach-in gate `tools/check_no_field_reachins.sh` is CI-wired** (modules-v2-exports U5b, done-condition 8) over `examples/ test_build/` (the `build-system` job, with the `--selfcheck` teeth). It greps for source-level field reach-ins on opaque stdlib/imported-struct fields. `test_files/` is deliberately **not** scanned: its single-file fixtures define their own collection types with the same field names (same-module, no boundary), which a field-name grep cannot distinguish from a reach-in — the `.bmod` cross-module path is enforced precisely by Sema (`fail/xmodule/`), and the remaining combine-mode namespaced-stdlib gap is KI-23 (Epic B).
- **`test_files/fail/xmodule/`** is a cross-module negative-test category: each fixture is a directory of `lib.b` + `consumer.b` + `consumer.b.expected`; `run_tests.sh` emits `lib.bmod`, compiles `consumer.b lib.bmod`, and requires a non-zero exit plus a canonical `file:line:col: error:` matching the pattern.
- **Identifiers beginning with `__` are reserved** for compiler-generated symbols (`__<Struct>_dtor`, `__<Struct>_new`, `__enum_<Name>_box_dtor`) and are rejected in source with a located error; `extern fn` is exempt, since it names a foreign C symbol (the runtime's `__blang_*` entry points).
- Generic functions and generic structs from dependencies WORK: the `.bmod` ships generic definitions **with bodies** (verbatim source slices — generic fns plus an `impl` block of a generic struct's methods), consumers monomorphize on demand, and instances are emitted `linkonce_odr` so a library's own instantiations dedup with the consumer's at link time (`test_build/run_build_tests.sh` proves int/double/string instantiations plus a generic struct with `Pair<T>`-returning methods end to end). Non-generic functions stay signature-only and link from the `.a`.
- Git dependencies work: `deps.foo = { git = "<url>", tag = "v1.0" }` (or `rev = "<commit>"`) is fetched with the git CLI into `~/.cache/blang/git/<name>-<hash(url@pin)>` on first build (shallow `--branch` clone for tags/branches, full clone + checkout for revs), reused offline while warm, then treated exactly like a path dep (recursive build, `.a` + `.bmod`, content-addressed caching — generics included). A pin (tag or rev) is REQUIRED; an unpinned git dep is a hard error. `bcc clean` removes checkouts along with the object cache. Automated by the git-dep leg of `test_build/run_build_tests.sh` (local `file://` repo).
- Generic monomorphization supports struct and function instantiation with full ARC participation: every ARC decision site (scope tracking, borrowed-source bind-retain, untrack-on-store, temp tracking, the `isStringType`/`isArrayType` predicates, and the return-retain borrow rule) resolves declared type names through the active substitution (`resolvedTypeName`/`callReturnTypeName`/`methodReturnTypeName`, CGTypes.cpp), so `T`-typed values inside monomorphized generics are retained/released like concrete ones — `sort<string>`, `Map<string,string>`, struct-valued Map under churn, and `Map<string, Array<int>>` are all ASan-clean (`codegen_generic_arc_*.b`). Generic calls infer their type arguments from argument types when no explicit `<...>` list is given (`sort_items(names)` == `sort_items<string>(names)`); a call that can neither infer nor was given explicit arguments is a loud compile error (`cgfail/generic_infer_fail.b`). Cross-module generic instantiation works (`.bmod` ships generic bodies; instances are `linkonce_odr`). Generic protocols work end to end: `impl Protocol for Struct` checks arity and types (a generic protocol's own params are wildcards), constraints are enforced on inferred calls too (codegen, `cgfail/generic_constraint_inferred.b`), and a constrained generic function's method calls on a param-typed object dispatch to the concrete struct (`codegen_generic_protocol.b`).
- Nested `@json` structs use an encode/decode round-trip (serialize inner struct to string, decode to JSON tree) — works correctly but adds overhead; direct subtree construction would be more efficient.

## devbot program conventions

- **Epic docs root**: `docs/epics/<epic-name>/` — every epic lives in its own
  directory there, with `overview.md` as the entry point (plus `manifest.yaml`
  and companion documents produced by `/devbot-plan`).
- **Speckit**: yes — installed at `.specify/`. Hires run the speckit ceremony
  (`/speckit-specify` → `/speckit-plan` → `/speckit-tasks` →
  `/speckit-implement`) per work unit, under the devbot manager's direction.
- **Constitution**: `.specify/memory/constitution.md` — includes the audit
  pattern and quality gates. It governs all devbot-directed work.
- **Review & merge policy**: all work lands via PR; a secondary reviewer hire
  (distinct from the implementer) reviews each PR, all findings are resolved,
  and only then is the PR merged to `master` by the reviewer — end-to-end at
  the devbot manager's direction. No direct commits to `master`.
- **Quality gates**: `./run_tests.sh` and `./test_codegen.sh` green in both
  build modes; new features carry pass/fail/codegen tests; runtime/ARC
  changes pass `./test_codegen.sh --leak-check`.
- **Active epics**:
  - `blang-ast` (`docs/epics/blang-ast/`) — status: **complete** (U1-U8 merged
    to local master; epic done-condition verified). Dedicated `Sema` pass
    enforces the type system, ownership/move, and concurrency rules in all build
    modes with clean `file:line:col: error:` diagnostics; silent codegen
    coercions removed; `test_files/fail/sema/` has 26 negative fixtures
    including audit_01..10.
  - `test-validation` (`docs/epics/test-validation/`) — status: **complete** (all 8 units merged to local master; done-condition verified — golden-output tests, real `bcc test`, ASan/leak + fuzzing CI, runtime unit tests).
    Phase 2 of the production roadmap ("trust the tests"): golden-output
    codegen tests, a real `bcc test` runner, ASan/LSan + fuzzing in CI, and
    C-runtime unit tests.
  - `feature-integration` (`docs/epics/feature-integration/`) — status:
    **complete** (U1–U8 merged to master via a single merge commit joining the
    two diverged histories; done-condition verified). Reconciled local master
    (blang-ast + test-validation epics) with origin/master's parallel feature
    line — origin's features (channel send/recv, event loop + timers, built-in
    Option/Result + exhaustive match, database layer + `bcc migrate`, HTTP
    routing + builtin `to_json`) were ported INTO local's `CG*`/`Sema`
    architecture (the monolithic `CodeGen.cpp` was NOT resurrected). Both prior
    epics' gates stayed green (26+ `fail/sema` fixtures, golden codegen tests,
    `--leak-check`, `fuzz`), match-exhaustiveness has a single implementation in
    `Sema`, and `codegen_parked.txt` is empty (every feature ported).
  - `functional-hardening` (`docs/epics/functional-hardening/`) — status:
    **U1–U5 complete** (devbot run `7708a1f6-3ec0-402b-83f2-56902320ffa8`).
    Behavioral test matrices (aggregate/field ARC `codegen_arc_*.b`, operators,
    feature interactions `codegen_ix_*.b`, stdlib-via-`bcc`) raised the codegen
    matrix 85 → 107, each with a committed stdout golden; the ARC matrix is
    leak-clean under `--leak-check`. Both seeded bugs fixed: S1 struct-valued
    field reassignment (`CGStruct.cpp`), S2 `Map` via `import collections;`
    through the real `bcc` driver (`qcc.cpp`/`bcc.cpp`). U5 additionally fixed a
    pre-existing full-suite leak in `codegen_db_query.b` (discarded `query T;`
    `Array<T>` rvalue now released at statement end, `CGRuntime.cpp`) so the
    `sanitizers` CI job's full-suite `--leak-check` is green. `known-issues.md`
    holds 0 deferred bugs (fix-or-file cap ≤ 3). From the 2026-07-19
    functional-coverage evaluation.
  - `modules-v2-exports` (`docs/epics/modules-v2-exports/`) — status:
    **complete** (all 5 units U1–U5 merged and pushed to origin `f5b2d13`;
    epic done-condition — all 9 items — independently verified by the owner
    2026-08-09 **and GitHub CI green**, run `31324486733`, all 14 jobs). Epic A of
    the modules-v2 split is done; Epic B (`modules-v2-graph`) can now be planned.
    Runs: `aeba092e` (2026-08-08,
    completed — U4 remainder + U5a/U5b); earlier `370bedc3` (stopped, controller
    bug) and `92b1f2a0` (halted mid-U4, landed U1–U3). U1-U3 (PRs #139/#140/#141:
    construction
    ABI via a library-emitted factory `specs/029`, the `.bmod` as a versioned true
    interface `specs/030`, `pub` on impl members + private-by-default + P9 export
    enforcement `specs/031`); U4 (print/interpolation-renderer fixes KI-8/8b/9/10 +
    design artifact `specs/032` salvage-merged `58a240a`, then the stdlib accessor
    surface + `Map`/`Set` `pub init` + examples migration + KI-20/21, PR #143); U5
    the flip (always-private fields, module-private struct literals, located
    field-access errors, opaque `.bmod` v3→v4, D15 metadata, corpus migration;
    U5a PR #144 `a93bb3c`, U5b `f70e0f4`; `Todo.from_json` symmetric builtin per
    owner OQ#1 ruling, KI-22 fix). Verified: `run_tests` 239/0 + 232/0,
    `test_codegen` 165/0, `--leak-check` Leaks:0, `test_lsp` 62/0, `test_build`
    pass, reach-in gate exits 0. Filed KI-24 (pre-existing for-in
    element-capture-across-break ARC leak, epic-unrelated). Epic A of the modules-v2 split: opaque exports — cross-module
    construction via library-emitted factory, `pub` methods/`init`
    (private-by-default), always-private fields, `.bmod` as true interface
    (signatures + protocol-conformance records + compiler-facing `table`/`@json`
    field metadata), P9 export-signature enforcement, stdlib accessor-API
    redesign, build-cache format versioning. Binding design record (D1–D17):
    `docs/epics/modules-v2/overview.md` (shared with the future
    `modules-v2-graph` Epic B — module identity, per-module scopes, import
    enforcement, type tiers, module-prefix ARC fix, cross-module LSP — to be
    planned after Epic A completes).
  - `modules-v2-graph` (`docs/epics/modules-v2-graph/`) — status: **launched**
    (run `3e3dbafe-0cd0-4838-bb98-10d3a17fedc5`, 2026-08-09; full scope U1–U9).
    Epic B of the modules-v2 split (the resolution half): canonical module
    identity (fixes P10 generic-mangling collapse), three type tiers
    core/prelude/library (prelude = `{Map,Set,Buffer}`, delete bare-name
    registration, demote `cli`), the module-prefix string-ARC codegen fix (crux,
    unblocks retiring the `buffer`/`collections`/`cli` promotions — closes KI-3),
    a per-module import graph via an **extractable resolver** (removes the
    flat-merge injection), enforced imports with use/name-capability (D7) +
    located collision/unknown/unused diagnostics, foreign-type refs in `.bmod` +
    the transitive build graph (closes KI-5), and two stretch units (import
    aliasing, module search path). Owner decisions (2026-08-09): cross-module LSP
    split to a follow-on **Epic C (`modules-v2-lsp`)** that consumes U4's resolver
    seam; both stretch units in scope; fully-autonomous/generous. Inherits
    KI-3/KI-5/KI-16/KI-23 from Epic A. 9 units (U1–U9), REQ-001..014. Binding
    design record: `docs/epics/modules-v2/overview.md` (D1–D17). Next:
    `/devbot-review modules-v2-graph`.
  - `001-toolchain-and-stdlib` (`docs/epics/001-toolchain-and-stdlib/`) — status:
    **complete-local** (devbot run `3a358cfb-38ff-4189-82f5-172d64f05c14`; all 7
    units U0–U6 merged to local master; done-conditions #1–#5 independently
    verified on the real toolchain — gdb debugs BLang, `--json`/`-O2`/`-g`/stdlib
    all work; **#6 CI-green pending push** — master is unpushed and no CI has run
    on these commits). Phase 4 at full depth across four areas: multi-error diagnostics
    (multi-error recovery + `--json` + warnings/`-Werror`), optimization
    (`-O0..3/s/z` in-process pass pipeline + `llc -O` + `--release` +
    `--target` cross-compile, informational `opt-delta.md`), DWARF debug info
    (`-g` via DIBuilder — per-function `DISubprogram` + line tables + gdb
    breakpoints; `-g`→`-O0` stance, emission verifier-clean under `-O`), and
    stdlib breadth (C-backed math/time/random/env via the 6-touchpoint recipe;
    hashed `Map`/`Set` replacing the O(n) scan; generic value-type `sort`; CLI
    flag parsing). 3-role team (architect spec audits + implementer + independent
    code reviewer); each area has committed architect-reviewed speckit artifacts
    under `specs/`. codegen_*.b matrix 107 → **134**. Along the way it fixed
    several codegen bugs (unary float negation, `math.abs_int` ABI, a `match`
    temp-string leak, lexicographic string `<`) and **filed** the deeper
    array/aggregate ARC edges it surfaced (refcounted-element `sort<string>`,
    struct-valued hashed Map under churn, etc.) in
    `docs/epics/001-toolchain-and-stdlib/known-issues.md`. First NNN-numbered epic.
