# BLang Implementation Plan

## Current State (as of February 2026)

### What Works Today

**Lexer**: Hand-written lexer recognizes types (`int`, `char`, `string`, `void`, `float`, `double`), control flow keywords (`if`, `else`, `while`, `for`, `return`), `extern`, identifiers, integer/float/string/char literals, all arithmetic/comparison/logical/bitwise operators, assignment operators (`=`, `+=`, `-=`, `*=`, `/=`), braces, parens, commas, semicolons, single-line and multi-line comments.

**Parser (AST)**: Recursive-descent parser builds AST with these node types:
- `Module` (top-level container of functions)
- `FunctionDefinition` (with parameters, return type, body; includes `extern` declarations and variadic `...`)
- `Block` (scoped statement list)
- `VariableDeclaration` (with optional initializer expression)
- `IfStatement`, `WhileStatement`, `ForStatement`
- `ReturnStatement`
- `Expression` hierarchy: `ConstInteger`, `ConstFloat`, `ConstString`, `ConstChar`, `VariableExpression`, `CallExpression`, `AssignmentExpression`, `OperationsExpression` (binary ops with precedence), `UnaryExpression` (`-`, `!`)

**Codegen (LLVM 18+, conditional)**: When built with `llvm-18-dev`, `CodeGen` class generates LLVM IR for all of the above. Full pipeline tested: parse → `.ll` → `llc` → native binary.

**Tests**: 83 tests in `run_tests.sh` — 62 pass, 21 fail (negative tests), 0 xfails. All tests use `.b` extension. Two lexer test programs. End-to-end codegen test script. GitHub Actions CI configured.

### What the Language Design Spec Describes but Does NOT Exist Yet

The remaining gaps are primarily in codegen (requires LLVM) and Phase 2/3 features (ownership, concurrency, data services). The parser now covers nearly all Phase 1 language constructs.

---

## Phase 1 — Core Language

The foundation: transition from C-style syntax to BLang syntax, complete the type system, and get the core language compiling end-to-end.

### 1.1 Syntax Transition: `fn` keyword and `->` return types

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 1 | Add `fn` keyword to lexer | impl | YES | Add `fn` as a recognized keyword token in `FileLexer.cpp` |
| 2 | Parse `fn` function declarations | impl | YES | Update `FunctionDefinition::Parse` to accept `fn name(type arg, ...) -> type { }` syntax |
| 3 | Support omitted return type as void | impl | YES | `fn greet(string name) { }` — no `->` means void return |
| 4 | Update all pass test files to `fn` syntax | test | YES | All pass tests use fn-style syntax; C-style is now rejected |
| 5 | Update all fail test files | test | YES | All fail tests updated to `.b` extension and fn syntax |
| 6 | Add fail test: old C-style syntax rejected | test | YES | `c_style_func.b` — C-style function declarations correctly rejected |
| 7 | Update codegen for new function syntax | impl | YES | `CodeGen::genFunction` works with both AST shapes |
| 8 | Document `fn` syntax in CLAUDE.md | docs | YES | Documented in CLAUDE.md supported features |

### 1.2 Fix Remaining Parser Issues

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 9 | Move xfail tests that now pass to pass/ | test | YES | Moved `arithmetic_stmt.c`, `assignment_stmt.c`, `binary_expr_return.c`, `comparison_expr.c` to pass/ |
| 10 | Support extern without named params | impl | YES | `extern int printf(string, ...);` works with synthetic `_arg0` names |
| 11 | Move `extern_func_call.b` to pass/ once fixed | test | YES | Added extern fn printf declaration; moved from xfail/ to pass/ |
| 12 | Add `const` keyword to lexer | impl | YES | `const` recognized as keyword token |
| 13 | Implement `const` variable declarations | impl | YES | `const float PI = 3.14;` parsed, must have initializer |
| 14 | Add pass tests for `const` | test | YES | `const_decl.c`, `const_no_init.c` (fail test) |
| 15 | Add `var` keyword for type inference | impl | YES | `var x = 42;` parsed with type inferred from initializer |
| 16 | Add pass/fail tests for `var` | test | YES | `var_infer.c`, `var_no_init.c` (fail test) |

### 1.3 Struct Types and `impl` Blocks

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 17 | Add `struct` keyword to lexer | impl | YES | `struct` is a recognized keyword token |
| 18 | Create `StructDefinition` AST node | impl | YES | `StructDefinition` in `Type.h` with fields, methods, generic params |
| 19 | Parse struct definitions | impl | YES | `QStructDefinition.cpp` — `struct Point { int x; int y; }` |
| 20 | Add struct type to type system | impl | YES | Structs registered as types in scope via `addType()` |
| 21 | Parse struct literal construction | impl | YES | `Point { x: 1, y: 2 }` syntax in `QExpression.cpp` |
| 22 | Parse field access expressions | impl | YES | `point.x` — `FieldAccessExpression` AST node |
| 23 | Add `impl` keyword to lexer | impl | YES | `impl` is a recognized keyword token |
| 24 | Parse `impl` blocks | impl | YES | `QImplBlock.cpp` — `impl Protocol for Struct { ... }` |
| 25 | Add `self` keyword | impl | YES | Lexer + parser support for `self` in method params |
| 26 | Codegen for struct types | impl | — | No LLVM struct type mapping yet |
| 27 | Codegen for method calls | impl | — | No method dispatch codegen yet |
| 28 | Add pass tests for structs | test | YES | `struct_basic.c`, `struct_literal.c`, `field_access.c`, `method_call.c`, `impl_basic.c`, `impl_protocol.c` |
| 29 | Add fail tests for structs | test | YES | `struct_bad_field.c`, `struct_missing_brace.c` |
| 30 | Document struct and impl syntax | docs | YES | Documented in CLAUDE.md |

### 1.4 Protocol Definitions

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 31 | Add `protocol` keyword to lexer | impl | YES | `protocol` is a recognized keyword token |
| 32 | Create `ProtocolDefinition` AST node | impl | YES | `ProtocolDefinition` in `Type.h` with method signatures, generic params |
| 33 | Parse protocol definitions | impl | YES | `QProtocolDefinition.cpp` — `protocol Printable { fn to_string(self) -> string; }` |
| 34 | Implement conformance checking | impl | YES | `ParseImplBlock` verifies all required protocol methods are implemented |
| 35 | Compile error for missing protocol methods | impl | YES | Compile error: "Struct 'X' does not implement method 'Y' required by protocol 'Z'" |
| 36 | Add pass tests for protocols | test | YES | `protocol_basic.c`, `impl_protocol.c` |
| 37 | Add fail tests for protocols | test | YES | `protocol_no_fn.c` |
| 38 | Document protocol system | docs | YES | Documented in CLAUDE.md |

### 1.5 Generics

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 39 | Parse `<T>` type parameters on functions | impl | YES | `fn first<T>(List<T> list) -> Option<T>` |
| 40 | Parse `<T>` type parameters on structs | impl | YES | `struct List<T> { ... }` with `GenericParam` |
| 41 | Parse protocol constraints `<T: Comparable>` | impl | YES | Constraint syntax on functions, structs, protocols, enums |
| 42 | Implement generic type instantiation | impl | — | No monomorphization or type erasure yet |
| 43 | Codegen for generic functions | impl | — | No generic codegen yet |
| 44 | Add pass tests for generics | test | YES | `generic_fn.c`, `generic_struct.c`, `generic_constraint.c`, `generic_protocol.c`, `generic_type_args.c` |
| 45 | Add fail tests for generics | test | YES | `generic_unknown_constraint.b`, `generic_duplicate_param.b` |
| 46 | Document generics | docs | YES | Documented in CLAUDE.md |

### 1.6 Result/Option Types and `?` Operator

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 47 | Implement `Result<T, E>` as built-in generic type | impl | PARTIAL | Parseable as user-defined enum; no special compiler treatment |
| 48 | Implement `Option<T>` as built-in generic type | impl | PARTIAL | Parseable as user-defined enum; no special compiler treatment |
| 49 | Add `match` keyword to lexer | impl | YES | `match` is a recognized keyword token |
| 50 | Parse `match` expressions | impl | YES | `QMatchExpression.cpp` — literal, wildcard `_`, destructuring patterns |
| 51 | Parse `?` operator | impl | YES | `QUESTION_MARK` token in lexer; `TryExpression` AST node; postfix parsing in `ParsePrimary` |
| 52 | Codegen for Result/Option | impl | — | No tagged union representation |
| 53 | Codegen for match | impl | — | No `genMatchExpression` in CodeGen |
| 54 | Codegen for `?` operator | impl | — | Depends on task 51 |
| 55 | Add pass tests for Result/Option | test | YES | `result_option.b`, `match_enum_variants.b`, `try_operator.b` |
| 56 | Add fail test: unhandled Result | test | YES | `match_missing_brace.b` — match arm without block braces rejected |
| 57 | Document error handling | docs | YES | Error handling documented in language_design.md and CLAUDE.md |

### 1.7 Module System

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 58 | Add `import` keyword to lexer | impl | YES | `import` is a recognized keyword token |
| 59 | Add `pub` keyword to lexer | impl | YES | `pub` is a recognized keyword token |
| 60 | Parse `import` statements | impl | YES | `ImportStatement` AST node; dotted paths supported (`import std.io;`) |
| 61 | Parse `pub` visibility modifier | impl | YES | `mIsPublic` flag on functions, structs; `pub fn`, `pub struct`, `pub enum` |
| 62 | Implement multi-file compilation | impl | YES | qcc accepts multiple source files; each parsed into own Module with shared global scope |
| 63 | Implement symbol visibility checking | impl | PARTIAL | `isPublic()` tracked on symbols; cross-module enforcement pending |
| 64 | Enforce flat namespace rule | impl | YES | Enforced by parser — no module-qualified names in expressions |
| 65 | Enforce no function overloading | impl | YES | `Scope::addSymbol` returns false for duplicates; compile error on redefinition |
| 66 | Enforce mandatory pub type signatures | impl | YES | Already enforced by grammar — fn syntax always requires explicit parameter types |
| 67 | Codegen for multi-module | impl | — | Requires LLVM; pending |
| 68 | Add pass tests for modules | test | YES | `import_basic.b`, `import_dotted.b`, `pub_function.b`, `pub_struct.b`, `pub_enum.b`, `visibility_basic.b` |
| 69 | Add fail tests for modules | test | YES | `import_missing_semi.b`, `import_missing_name.b` |
| 70 | Document module system | docs | YES | Module system documented in language_design.md and CLAUDE.md |

### 1.8 Build System and Tooling

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 71 | Switch test files from `.c` to `.b` extension | impl | YES | All 83 tests use `.b` extension |
| 72 | Add `--emit-ir` flag to qcc | impl | YES | `-S` / `--emit-ir` flag in qcc CLI |
| 73 | Add `--emit-obj` flag to qcc | impl | YES | `-c` / `--emit-obj` flag in qcc CLI |
| 74 | Add `--output` / `-o` flag to qcc | impl | YES | `-o` / `--output FILE` flag in qcc CLI |
| 75 | Set up CI with GitHub Actions | infra | YES | `.github/workflows/ci.yml` with parse-only and with-llvm matrix |
| 76 | Add `blang test` subcommand skeleton | impl | YES | `bcc test` subcommand discovers and runs .b test files |
| — | Add `bcc` compiler driver CLI | impl | YES | `bcc.cpp` — orchestrates qcc → llc → cc pipeline with -S, -c, -o, -v, -l, -L flags |

---

## Phase 2 — Concurrency, Safety, and Testing

### 2.1 Ownership Model

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 77 | Add `own` keyword to lexer | impl | — | Ownership qualifier |
| 78 | Add `shared` keyword to lexer | impl | — | Shared reference-counted qualifier |
| 79 | Add `sync` keyword to lexer | impl | — | Synchronized mutable qualifier |
| 80 | Parse ownership qualifiers on variable declarations | impl | — | `own Buffer buf = Buffer.new(1024);` |
| 81 | Implement move semantics for `own` | impl | — | Assignment transfers ownership, original becomes invalid |
| 82 | Implement use-after-move detection | impl | — | Compile error when accessing moved variable |
| 83 | Implement ARC for `shared` types | impl | — | Reference counting in codegen, deterministic deallocation |
| 84 | Implement auto-locking for `sync` types | impl | — | Mutex wrapper generated in codegen |
| 85 | Add pass tests for ownership | test | — | Move semantics, shared refs, sync access |
| 86 | Add fail tests for ownership | test | — | Use-after-move, cross-spawn own without channel |
| 87 | Document ownership model | docs | — | Update CLAUDE.md with ownership support |

### 2.2 Concurrency: spawn/chan

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 88 | Add `spawn` keyword to lexer | impl | — | Green thread creation |
| 89 | Add `chan` keyword to lexer | impl | — | Channel type |
| 90 | Parse `spawn { ... }` blocks | impl | — | New `SpawnStatement` AST node |
| 91 | Parse channel declarations | impl | — | `chan int results = chan.new(10);` |
| 92 | Parse channel operations | impl | — | `.send()` and `.recv()` method calls |
| 93 | Implement BLang runtime: green thread scheduler | impl | — | Work-stealing scheduler over OS threads (C/C++ runtime library) |
| 94 | Implement BLang runtime: channel implementation | impl | — | Typed, buffered, thread-safe channels |
| 95 | Codegen for spawn | impl | — | Emit calls to runtime scheduler API |
| 96 | Codegen for channel operations | impl | — | Emit calls to runtime channel API |
| 97 | Enforce thread safety rules | impl | — | `own` can't cross spawn; `shared` is read-only; `sync` auto-locks |
| 98 | Add pass tests for spawn/chan | test | — | Basic spawn, channel send/recv, buffered channels |
| 99 | Add fail tests for concurrency | test | — | Own across spawn boundary, data race attempts |
| 100 | Document concurrency | docs | — | Update CLAUDE.md |

### 2.3 Async/Await and Event Loop

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 101 | Add `async` keyword to lexer | impl | — | Async function qualifier |
| 102 | Add `await` keyword to lexer | impl | — | Await expression |
| 103 | Parse `async fn` declarations | impl | — | Marks function as event-loop scheduled |
| 104 | Parse `await` expressions | impl | — | `data = await conn.read_all()?;` |
| 105 | Implement BLang runtime: event loop | impl | — | libuv-style event loop (or integrate libuv directly) |
| 106 | Codegen for async functions | impl | — | State machine transformation or coroutine lowering |
| 107 | Codegen for await | impl | — | Yield point in state machine |
| 108 | Add `on` keyword for event handlers | impl | — | `on timer.every(1000) { ... }` |
| 109 | Parse event handler syntax | impl | — | New `EventHandler` AST node |
| 110 | Codegen for event handlers | impl | — | Register callback with event loop |
| 111 | Add pass tests for async/await | test | — | Async functions, await chaining, event handlers |
| 112 | Document async model | docs | — | Update CLAUDE.md |

### 2.4 Contracts

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 113 | Add `requires` keyword to lexer | impl | — | Precondition keyword |
| 114 | Add `ensures` keyword to lexer | impl | — | Postcondition keyword |
| 115 | Parse `requires` clauses on functions | impl | — | `fn div(int a, int b) -> int requires b != 0 { }` |
| 116 | Parse `ensures` clauses on functions | impl | — | `ensures result >= 0` |
| 117 | Implement runtime contract checks | impl | — | Insert assertion code at function entry/exit |
| 118 | Implement compile-time contract checking (basic) | impl | — | Detect constant violations like `divide(x, 0)` |
| 119 | Add pass tests for contracts | test | — | Valid preconditions satisfied, postconditions hold |
| 120 | Add fail tests for contracts | test | — | Compile-time contract violation, runtime panic on violation |
| 121 | Document contracts | docs | — | Update CLAUDE.md |

### 2.5 Built-in Testing

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 122 | Add `test` keyword to lexer | impl | — | Test block keyword |
| 123 | Add `assert` keyword to lexer | impl | — | Assertion keyword |
| 124 | Create `TestBlock` AST node | impl | — | Named test block containing statements |
| 125 | Parse `test "name" { ... }` blocks | impl | — | Test blocks at module level |
| 126 | Parse `assert expr` statements | impl | — | Assert with any boolean expression |
| 127 | Implement `blang test` command | impl | — | Discover all test blocks, compile, run, report pass/fail |
| 128 | Strip test blocks from release builds | impl | — | `--release` flag omits test code from binary |
| 129 | Test output formatting | impl | — | Clear pass/fail reporting with file:line on failure |
| 130 | Add pass tests for test blocks | test | — | Test block that passes, test block with multiple asserts |
| 131 | Add fail tests for test blocks | test | — | Assert failure produces clear error |
| 132 | Self-hosting milestone: BLang tests in BLang | test | — | Write compiler test suite using built-in `test` blocks |
| 133 | Document testing | docs | — | Update CLAUDE.md with `blang test` usage |

---

## Phase 3 — Data and Services

### 3.1 Pipeline Operator

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 134 | Add `|>` operator to lexer | impl | — | Two-character operator token |
| 135 | Parse pipeline expressions | impl | — | `expr |> fn(args)` desugars to `fn(expr, args)` |
| 136 | Codegen for pipeline | impl | — | Same as function call after desugaring |
| 137 | Add pass tests for pipeline | test | — | Chained pipelines, pipeline with methods |
| 138 | Document pipeline operator | docs | — | Update CLAUDE.md |

### 3.2 Table Structs and Query Expressions

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 139 | Add `table` keyword to lexer | impl | — | Table struct qualifier |
| 140 | Parse `table struct` definitions | impl | — | `table struct User { int id; string name; }` |
| 141 | Implement schema metadata storage | impl | — | Compiler tracks table schemas for validation |
| 142 | Add `query` keyword to lexer | impl | — | Query expression keyword |
| 143 | Add `insert` keyword to lexer | impl | — | Insert expression keyword |
| 144 | Add `update` keyword to lexer | impl | — | Update expression keyword |
| 145 | Add `delete` keyword to lexer | impl | — | Delete expression keyword |
| 146 | Parse `query T |> where { } |> order_by { } |> limit()` | impl | — | Full query expression pipeline |
| 147 | Parse `insert T { field: value }` | impl | — | Insert expression |
| 148 | Parse `update T |> where { } |> set { }` | impl | — | Update expression |
| 149 | Parse `delete T |> where { }` | impl | — | Delete expression |
| 150 | Compile-time field validation | impl | — | `.nonexistent_field` is a compile error |
| 151 | SQL generation backend | impl | — | Translate query AST to parameterized SQL strings |
| 152 | Implement database runtime library | impl | — | Connection pooling, prepared statements, result mapping |
| 153 | Support `@db("name")` annotation for named connections | impl | — | Multi-database support |
| 154 | Add pass tests for queries | test | — | Select, insert, update, delete, joins, pipelines |
| 155 | Add fail tests for queries | test | — | Wrong field name, type mismatch in where clause |
| 156 | Add end-to-end test with SQLite | test | — | Parse → SQL → execute against real database |
| 157 | Document query system | docs | — | Update CLAUDE.md |

### 3.3 Automatic Migrations

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 158 | Implement schema snapshot storage | impl | — | Compiler persists known schema state (JSON or binary in `.blang/` dir) |
| 159 | Implement schema diff engine | impl | — | Compare current table structs against stored snapshot |
| 160 | Generate `CREATE TABLE` for new tables | impl | — | Initial migration from nothing |
| 161 | Generate `ALTER TABLE ADD COLUMN` for new fields | impl | — | Additive migration |
| 162 | Detect and flag destructive changes | impl | — | Dropping columns/tables requires `@drop` annotation |
| 163 | Implement `blang migrate --preview` | impl | — | Show pending migration SQL without applying |
| 164 | Implement `blang migrate --apply` | impl | — | Execute migration against configured database |
| 165 | Implement `blang migrate --generate` | impl | — | Write migration SQL to file for CI review |
| 166 | Add `@drop` annotation for confirmed removals | impl | — | `@drop string old_field;` permits column removal |
| 167 | Add tests for migration generation | test | — | Add field, add table, remove with @drop, rename detection |
| 168 | Document migration system | docs | — | Update CLAUDE.md |

### 3.4 Serialization Annotations

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 169 | Add `@` annotation syntax to lexer/parser | impl | — | General-purpose annotation parsing (`@name` before declarations) |
| 170 | Implement `@json` annotation | impl | — | Generate `to_json()`/`from_json()` methods on annotated structs |
| 171 | Implement JSON serializer in standard library | impl | — | Runtime JSON encoding/decoding |
| 172 | Implement `@msgpack` annotation | impl | — | Generate msgpack serialization methods |
| 173 | Implement `@csv` annotation | impl | — | Generate CSV serialization methods |
| 174 | Add pass tests for @json | test | — | Serialize/deserialize structs, nested structs, lists |
| 175 | Add fail tests for serialization | test | — | Malformed JSON input returns `Result::err` |
| 176 | Document serialization | docs | — | Update CLAUDE.md |

### 3.5 gRPC and Protocol Buffers

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 177 | Implement `@grpc` annotation on structs | impl | — | Generate protobuf-compatible serialization |
| 178 | Implement `@grpc` annotation on protocols | impl | — | Generate service stubs (client + server interfaces) |
| 179 | Implement protobuf wire format in stdlib | impl | — | Binary serialization compatible with protobuf v3 |
| 180 | Implement gRPC client runtime | impl | — | HTTP/2 transport, stub calling |
| 181 | Implement gRPC server runtime | impl | — | HTTP/2 listener, request dispatch to impl |
| 182 | Add pass tests for gRPC | test | — | Define service, generate stubs, client-server roundtrip |
| 183 | Add interop test with Go/Python gRPC client | test | — | Cross-language validation |
| 184 | Document gRPC support | docs | — | Update CLAUDE.md |

### 3.6 HTTP Standard Library

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 185 | Implement `http.Server` in standard library | impl | — | Basic HTTP/1.1 server with routing |
| 186 | Implement `http.Request` / `http.Response` types | impl | — | Request parsing, response building |
| 187 | Implement route registration (`.get()`, `.post()`, etc.) | impl | — | Method-based routing API |
| 188 | Implement automatic JSON serialization for responses | impl | — | `http.ok(struct)` auto-calls `to_json()` if `@json` annotated |
| 189 | Implement `http.Client` for outgoing requests | impl | — | `http.get(url)`, `http.post(url, body)` |
| 190 | Add pass tests for HTTP server | test | — | Start server, make request, validate response |
| 191 | Document HTTP library | docs | — | Update CLAUDE.md |

### 3.7 GraphQL Standard Library

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 192 | Implement `@graphql` annotation | impl | — | Generate GraphQL schema from struct |
| 193 | Implement `graphql.Server` in standard library | impl | — | GraphQL endpoint with query execution |
| 194 | Implement auto-resolver generation from table structs | impl | — | CRUD resolvers derived from query expressions |
| 195 | Implement custom resolver override | impl | — | User-defined resolvers replace generated ones |
| 196 | Add pass tests for GraphQL | test | — | Schema generation, query execution, mutations |
| 197 | Document GraphQL library | docs | — | Update CLAUDE.md |

### 3.8 Database Configuration and Tooling

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 198 | Implement `blang.toml` project configuration | impl | — | Parse TOML config for database URL, driver, build options |
| 199 | Implement Postgres driver in stdlib | impl | — | libpq-based or native protocol |
| 200 | Implement SQLite driver in stdlib | impl | — | For local development and testing |
| 201 | Implement connection pooling | impl | — | Shared connection pool with configurable size |
| 202 | Add database integration tests | test | — | Full roundtrip: table struct → migrate → query → validate |
| 203 | Document database configuration | docs | — | Update CLAUDE.md |

---

## Cross-Cutting Concerns

These tasks span multiple phases and should be addressed incrementally.

### Documentation

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 204 | Keep CLAUDE.md in sync with implementation | docs | YES | CLAUDE.md is comprehensive and current |
| 205 | Keep language_design.md implementation status current | docs | PARTIAL | CLAUDE.md is more current; language_design.md needs update |
| 206 | Add examples/ directory | docs | — | No examples/ directory yet |
| 207 | Write tutorial: "Your first BLang program" | docs | — | |
| 208 | Write tutorial: "BLang for Rust developers" | docs | — | |

### Testing Infrastructure

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 209 | Add `--verbose` output to `run_tests.sh` showing compiler stderr | test | YES | `--verbose` flag shows stderr on failure |
| 210 | Add codegen-specific tests (require LLVM) | test | PARTIAL | `test_codegen.sh` exists; no LLVM-conditional category in run_tests.sh |
| 211 | Add end-to-end execution tests | test | — | run_tests.sh only checks qcc exit codes, not compiled output |
| 212 | Add regression test for each bug fix | test | — | No systematic regression test policy |
| 213 | Implement test timeout handling | test | YES | `timeout 10` in run_tests.sh |

### Build and CI

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 214 | Add GitHub Actions CI workflow | infra | YES | `.github/workflows/ci.yml` — builds with and without LLVM, runs tests on push/PR |
| 215 | Add `install_deps.sh` to CI | infra | YES | `install_deps.sh` — cross-platform dependency installer with `--with-llvm` option |
| 216 | Add build matrix: Linux + macOS | infra | PARTIAL | CI matrix includes parse-only and with-llvm; macOS support in install_deps.sh |
| 217 | Add `blang` wrapper script or rename `qcc` | impl | PARTIAL | User-facing binary is `bcc` (BLang Compiler CLI); no `blang` alias yet |

---

## Summary

| Phase | Tasks | Focus | Done | Partial | Remaining |
|-------|-------|-------|------|---------|-----------|
| Phase 1 | 1–76 | Core language: fn syntax, structs, protocols, generics, Result/Option, modules | 66 | 3 | 8 |
| Phase 2 | 77–133 | Safety and concurrency: ownership, spawn/chan, async/await, contracts, testing | 0 | 0 | 57 |
| Phase 3 | 134–203 | Data and services: pipeline, queries, migrations, serialization, gRPC, HTTP, GraphQL | 0 | 0 | 70 |
| Cross-cutting | 204–217 | Documentation, test infrastructure, CI | 6 | 4 | 4 |
| **Total** | **217** | | **72** | **7** | **139** |

### Recommended Execution Order within Phase 1

The most impactful first steps (unblock the most downstream work):

1. **Tasks 9–11**: Housekeeping — move fixed xfails to pass, fix last extern issue
2. **Tasks 1–8**: `fn` syntax transition — establishes BLang identity
3. **Tasks 17–30**: Structs + impl — required by everything else (protocols, generics, Result, queries)
4. **Tasks 47–57**: Result/Option + match + `?` — unlocks error handling throughout
5. **Tasks 31–38**: Protocols — needed for generics constraints and serialization
6. **Tasks 39–46**: Generics — needed for `List<T>`, `Result<T,E>`, etc.
7. **Tasks 58–70**: Modules — needed once we have multi-file programs
8. **Tasks 71–76**: Tooling — CI, file extension, CLI flags
