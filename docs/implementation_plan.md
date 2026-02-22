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

**Tests**: 25 tests in `run_tests.sh` — 15 pass, 5 fail (negative tests), 4 former xfails now pass, 1 xfail remains (`extern_func_call.c` — extern without named params). Two lexer test programs. End-to-end codegen test script.

### What the Language Design Spec Describes but Does NOT Exist Yet

The gap between the current C-style parser and the full BLang language design is substantial. The spec describes a complete language; we currently have a C-subset parser with LLVM codegen.

---

## Phase 1 — Core Language

The foundation: transition from C-style syntax to BLang syntax, complete the type system, and get the core language compiling end-to-end.

### 1.1 Syntax Transition: `fn` keyword and `->` return types

| # | Task | Type | Description |
|---|------|------|-------------|
| 1 | Add `fn` keyword to lexer | impl | Add `fn` as a recognized keyword token in `FileLexer.cpp` |
| 2 | Parse `fn` function declarations | impl | Update `FunctionDefinition::Parse` to accept `fn name(type arg, ...) -> type { }` syntax |
| 3 | Support omitted return type as void | impl | `fn greet(string name) { }` — no `->` means void return |
| 4 | Update all pass test files to `fn` syntax | test | Rewrite 15 pass tests from `int foo()` to `fn foo() -> int` |
| 5 | Update all fail test files | test | Rewrite 5 fail tests to use `fn` syntax |
| 6 | Add fail test: old C-style syntax rejected | test | `int foo() { }` should fail to parse after transition |
| 7 | Update codegen for new function syntax | impl | Ensure `CodeGen::genFunction` works with new AST shape |
| 8 | Document `fn` syntax in CLAUDE.md | docs | Update supported features and examples |

### 1.2 Fix Remaining Parser Issues

| # | Task | Type | Description |
|---|------|------|-------------|
| 9 | Move xfail tests that now pass to pass/ | test | Move `arithmetic_stmt.c`, `assignment_stmt.c`, `binary_expr_return.c`, `comparison_expr.c` from `xfail/` to `pass/` |
| 10 | Support extern without named params | impl | Allow `extern int printf(string, ...);` — unnamed parameters in extern declarations |
| 11 | Move `extern_func_call.c` to pass/ once fixed | test | After #10, this xfail becomes a pass test |
| 12 | Add `const` keyword to lexer | impl | Recognize `const` for constant declarations |
| 13 | Implement `const` variable declarations | impl | Parse `const float PI = 3.14;` — immutable, must have initializer |
| 14 | Add pass tests for `const` | test | `const_decl.c` — const with various types, fail test for reassignment |
| 15 | Add `var` keyword for type inference | impl | Parse `var x = 42;` — type inferred from initializer |
| 16 | Add pass/fail tests for `var` | test | `var_infer.c` — inference cases; fail test for `var` without initializer |

### 1.3 Struct Types and `impl` Blocks

| # | Task | Type | Description |
|---|------|------|-------------|
| 17 | Add `struct` keyword to lexer | impl | Already partially exists for C compat; ensure it's a proper keyword |
| 18 | Create `StructDefinition` AST node | impl | New node in `Type.h` — name, list of fields (name + type pairs) |
| 19 | Parse struct definitions | impl | `struct Point { int x; int y; }` — new parse method |
| 20 | Add struct type to type system | impl | `Type` class needs to represent struct types, not just primitives |
| 21 | Parse struct literal construction | impl | `Point { x: 1, y: 2 }` syntax |
| 22 | Parse field access expressions | impl | `point.x` — new `FieldAccessExpression` AST node |
| 23 | Add `impl` keyword to lexer | impl | Recognize `impl` for method blocks |
| 24 | Parse `impl` blocks | impl | `impl Printable for Point { fn to_string(self) -> string { } }` |
| 25 | Add `self` keyword | impl | Lexer + parser support for `self` parameter in methods |
| 26 | Codegen for struct types | impl | LLVM struct type mapping, alloca, GEP for field access |
| 27 | Codegen for method calls | impl | `point.to_string()` dispatches to the impl function |
| 28 | Add pass tests for structs | test | `struct_basic.c`, `struct_methods.c`, `struct_nested.c` |
| 29 | Add fail tests for structs | test | Missing field, wrong field type, duplicate field name |
| 30 | Document struct and impl syntax | docs | Update language_design.md implementation status, CLAUDE.md |

### 1.4 Protocol Definitions

| # | Task | Type | Description |
|---|------|------|-------------|
| 31 | Add `protocol` keyword to lexer | impl | New keyword token |
| 32 | Create `ProtocolDefinition` AST node | impl | Name + list of required method signatures |
| 33 | Parse protocol definitions | impl | `protocol Printable { fn to_string(self) -> string; }` |
| 34 | Implement conformance checking | impl | Verify that `impl Printable for X` provides all required methods |
| 35 | Compile error for missing protocol methods | impl | Clear error: "Point does not implement Printable: missing to_string" |
| 36 | Add pass tests for protocols | test | `protocol_basic.bl`, `protocol_multi.bl` |
| 37 | Add fail tests for protocols | test | Missing method, wrong signature, impl for unknown protocol |
| 38 | Document protocol system | docs | Update CLAUDE.md with protocol support |

### 1.5 Generics

| # | Task | Type | Description |
|---|------|------|-------------|
| 39 | Parse `<T>` type parameters on functions | impl | `fn first<T>(List<T> list) -> Option<T>` |
| 40 | Parse `<T>` type parameters on structs | impl | `struct List<T> { ... }` |
| 41 | Parse protocol constraints `<T: Comparable>` | impl | Constraint syntax in type parameter list |
| 42 | Implement generic type instantiation | impl | Monomorphization or type erasure strategy |
| 43 | Codegen for generic functions | impl | Generate concrete LLVM functions for each instantiation |
| 44 | Add pass tests for generics | test | Generic functions, generic structs, constrained generics |
| 45 | Add fail tests for generics | test | Unsatisfied constraint, wrong type argument count |
| 46 | Document generics | docs | Update CLAUDE.md |

### 1.6 Result/Option Types and `?` Operator

| # | Task | Type | Description |
|---|------|------|-------------|
| 47 | Implement `Result<T, E>` as built-in generic type | impl | Special compiler-known type with `ok(T)` and `err(E)` variants |
| 48 | Implement `Option<T>` as built-in generic type | impl | `some(T)` and `none` variants |
| 49 | Add `match` keyword to lexer | impl | For pattern matching on Result/Option |
| 50 | Parse `match` expressions | impl | `match result { ok(v) { ... } err(e) { ... } }` |
| 51 | Parse `?` operator | impl | `data = read_file("x")?;` — early return on error |
| 52 | Codegen for Result/Option | impl | LLVM tagged union representation |
| 53 | Codegen for match | impl | Branch on tag, bind inner value |
| 54 | Codegen for `?` operator | impl | Generate check + early return |
| 55 | Add pass tests for Result/Option | test | Construction, matching, propagation with `?` |
| 56 | Add fail test: unhandled Result | test | Compiler error when Result is ignored |
| 57 | Document error handling | docs | Update CLAUDE.md |

### 1.7 Module System

| # | Task | Type | Description |
|---|------|------|-------------|
| 58 | Add `import` keyword to lexer | impl | New keyword token |
| 59 | Add `pub` keyword to lexer | impl | For public visibility |
| 60 | Parse `import` statements | impl | `import math;` at top of file |
| 61 | Parse `pub` visibility modifier | impl | `pub fn add(...)` — exported symbol |
| 62 | Implement multi-file compilation | impl | `qcc` accepts multiple `.bl` files, resolves imports |
| 63 | Implement symbol visibility checking | impl | Non-pub symbols are private to their module |
| 64 | Enforce flat namespace rule | impl | Reject `import a.b.c` — only single-level imports |
| 65 | Enforce no function overloading | impl | Compiler error for duplicate function names in same scope |
| 66 | Enforce mandatory pub type signatures | impl | `pub fn` without explicit return type is an error |
| 67 | Codegen for multi-module | impl | Link LLVM modules together or use declarations |
| 68 | Add pass tests for modules | test | Two-file import, pub vs private |
| 69 | Add fail tests for modules | test | Import nonexistent module, access private symbol, nested import path |
| 70 | Document module system | docs | Update CLAUDE.md |

### 1.8 Build System and Tooling

| # | Task | Type | Description |
|---|------|------|-------------|
| 71 | Switch test files from `.c` to `.bl` extension | impl | Rename all test files, update `run_tests.sh` |
| 72 | Add `--emit-ir` flag to qcc | impl | Explicit flag to write `.ll` file (instead of always writing) |
| 73 | Add `--emit-obj` flag to qcc | impl | Invoke `llc` internally to produce `.o` |
| 74 | Add `--output` / `-o` flag to qcc | impl | Control output filename |
| 75 | Set up CI with GitHub Actions | infra | Build + run `run_tests.sh` on push |
| 76 | Add `blang test` subcommand skeleton | impl | Placeholder for built-in test runner (Phase 2 fills in) |

---

## Phase 2 — Concurrency, Safety, and Testing

### 2.1 Ownership Model

| # | Task | Type | Description |
|---|------|------|-------------|
| 77 | Add `own` keyword to lexer | impl | Ownership qualifier |
| 78 | Add `shared` keyword to lexer | impl | Shared reference-counted qualifier |
| 79 | Add `sync` keyword to lexer | impl | Synchronized mutable qualifier |
| 80 | Parse ownership qualifiers on variable declarations | impl | `own Buffer buf = Buffer.new(1024);` |
| 81 | Implement move semantics for `own` | impl | Assignment transfers ownership, original becomes invalid |
| 82 | Implement use-after-move detection | impl | Compile error when accessing moved variable |
| 83 | Implement ARC for `shared` types | impl | Reference counting in codegen, deterministic deallocation |
| 84 | Implement auto-locking for `sync` types | impl | Mutex wrapper generated in codegen |
| 85 | Add pass tests for ownership | test | Move semantics, shared refs, sync access |
| 86 | Add fail tests for ownership | test | Use-after-move, cross-spawn own without channel |
| 87 | Document ownership model | docs | Update CLAUDE.md with ownership support |

### 2.2 Concurrency: spawn/chan

| # | Task | Type | Description |
|---|------|------|-------------|
| 88 | Add `spawn` keyword to lexer | impl | Green thread creation |
| 89 | Add `chan` keyword to lexer | impl | Channel type |
| 90 | Parse `spawn { ... }` blocks | impl | New `SpawnStatement` AST node |
| 91 | Parse channel declarations | impl | `chan int results = chan.new(10);` |
| 92 | Parse channel operations | impl | `.send()` and `.recv()` method calls |
| 93 | Implement BLang runtime: green thread scheduler | impl | Work-stealing scheduler over OS threads (C/C++ runtime library) |
| 94 | Implement BLang runtime: channel implementation | impl | Typed, buffered, thread-safe channels |
| 95 | Codegen for spawn | impl | Emit calls to runtime scheduler API |
| 96 | Codegen for channel operations | impl | Emit calls to runtime channel API |
| 97 | Enforce thread safety rules | impl | `own` can't cross spawn; `shared` is read-only; `sync` auto-locks |
| 98 | Add pass tests for spawn/chan | test | Basic spawn, channel send/recv, buffered channels |
| 99 | Add fail tests for concurrency | test | Own across spawn boundary, data race attempts |
| 100 | Document concurrency | docs | Update CLAUDE.md |

### 2.3 Async/Await and Event Loop

| # | Task | Type | Description |
|---|------|------|-------------|
| 101 | Add `async` keyword to lexer | impl | Async function qualifier |
| 102 | Add `await` keyword to lexer | impl | Await expression |
| 103 | Parse `async fn` declarations | impl | Marks function as event-loop scheduled |
| 104 | Parse `await` expressions | impl | `data = await conn.read_all()?;` |
| 105 | Implement BLang runtime: event loop | impl | libuv-style event loop (or integrate libuv directly) |
| 106 | Codegen for async functions | impl | State machine transformation or coroutine lowering |
| 107 | Codegen for await | impl | Yield point in state machine |
| 108 | Add `on` keyword for event handlers | impl | `on timer.every(1000) { ... }` |
| 109 | Parse event handler syntax | impl | New `EventHandler` AST node |
| 110 | Codegen for event handlers | impl | Register callback with event loop |
| 111 | Add pass tests for async/await | test | Async functions, await chaining, event handlers |
| 112 | Document async model | docs | Update CLAUDE.md |

### 2.4 Contracts

| # | Task | Type | Description |
|---|------|------|-------------|
| 113 | Add `requires` keyword to lexer | impl | Precondition keyword |
| 114 | Add `ensures` keyword to lexer | impl | Postcondition keyword |
| 115 | Parse `requires` clauses on functions | impl | `fn div(int a, int b) -> int requires b != 0 { }` |
| 116 | Parse `ensures` clauses on functions | impl | `ensures result >= 0` |
| 117 | Implement runtime contract checks | impl | Insert assertion code at function entry/exit |
| 118 | Implement compile-time contract checking (basic) | impl | Detect constant violations like `divide(x, 0)` |
| 119 | Add pass tests for contracts | test | Valid preconditions satisfied, postconditions hold |
| 120 | Add fail tests for contracts | test | Compile-time contract violation, runtime panic on violation |
| 121 | Document contracts | docs | Update CLAUDE.md |

### 2.5 Built-in Testing

| # | Task | Type | Description |
|---|------|------|-------------|
| 122 | Add `test` keyword to lexer | impl | Test block keyword |
| 123 | Add `assert` keyword to lexer | impl | Assertion keyword |
| 124 | Create `TestBlock` AST node | impl | Named test block containing statements |
| 125 | Parse `test "name" { ... }` blocks | impl | Test blocks at module level |
| 126 | Parse `assert expr` statements | impl | Assert with any boolean expression |
| 127 | Implement `blang test` command | impl | Discover all test blocks, compile, run, report pass/fail |
| 128 | Strip test blocks from release builds | impl | `--release` flag omits test code from binary |
| 129 | Test output formatting | impl | Clear pass/fail reporting with file:line on failure |
| 130 | Add pass tests for test blocks | test | Test block that passes, test block with multiple asserts |
| 131 | Add fail tests for test blocks | test | Assert failure produces clear error |
| 132 | Self-hosting milestone: BLang tests in BLang | test | Write compiler test suite using built-in `test` blocks |
| 133 | Document testing | docs | Update CLAUDE.md with `blang test` usage |

---

## Phase 3 — Data and Services

### 3.1 Pipeline Operator

| # | Task | Type | Description |
|---|------|------|-------------|
| 134 | Add `|>` operator to lexer | impl | Two-character operator token |
| 135 | Parse pipeline expressions | impl | `expr |> fn(args)` desugars to `fn(expr, args)` |
| 136 | Codegen for pipeline | impl | Same as function call after desugaring |
| 137 | Add pass tests for pipeline | test | Chained pipelines, pipeline with methods |
| 138 | Document pipeline operator | docs | Update CLAUDE.md |

### 3.2 Table Structs and Query Expressions

| # | Task | Type | Description |
|---|------|------|-------------|
| 139 | Add `table` keyword to lexer | impl | Table struct qualifier |
| 140 | Parse `table struct` definitions | impl | `table struct User { int id; string name; }` |
| 141 | Implement schema metadata storage | impl | Compiler tracks table schemas for validation |
| 142 | Add `query` keyword to lexer | impl | Query expression keyword |
| 143 | Add `insert` keyword to lexer | impl | Insert expression keyword |
| 144 | Add `update` keyword to lexer | impl | Update expression keyword |
| 145 | Add `delete` keyword to lexer | impl | Delete expression keyword |
| 146 | Parse `query T |> where { } |> order_by { } |> limit()` | impl | Full query expression pipeline |
| 147 | Parse `insert T { field: value }` | impl | Insert expression |
| 148 | Parse `update T |> where { } |> set { }` | impl | Update expression |
| 149 | Parse `delete T |> where { }` | impl | Delete expression |
| 150 | Compile-time field validation | impl | `.nonexistent_field` is a compile error |
| 151 | SQL generation backend | impl | Translate query AST to parameterized SQL strings |
| 152 | Implement database runtime library | impl | Connection pooling, prepared statements, result mapping |
| 153 | Support `@db("name")` annotation for named connections | impl | Multi-database support |
| 154 | Add pass tests for queries | test | Select, insert, update, delete, joins, pipelines |
| 155 | Add fail tests for queries | test | Wrong field name, type mismatch in where clause |
| 156 | Add end-to-end test with SQLite | test | Parse → SQL → execute against real database |
| 157 | Document query system | docs | Update CLAUDE.md |

### 3.3 Automatic Migrations

| # | Task | Type | Description |
|---|------|------|-------------|
| 158 | Implement schema snapshot storage | impl | Compiler persists known schema state (JSON or binary in `.blang/` dir) |
| 159 | Implement schema diff engine | impl | Compare current table structs against stored snapshot |
| 160 | Generate `CREATE TABLE` for new tables | impl | Initial migration from nothing |
| 161 | Generate `ALTER TABLE ADD COLUMN` for new fields | impl | Additive migration |
| 162 | Detect and flag destructive changes | impl | Dropping columns/tables requires `@drop` annotation |
| 163 | Implement `blang migrate --preview` | impl | Show pending migration SQL without applying |
| 164 | Implement `blang migrate --apply` | impl | Execute migration against configured database |
| 165 | Implement `blang migrate --generate` | impl | Write migration SQL to file for CI review |
| 166 | Add `@drop` annotation for confirmed removals | impl | `@drop string old_field;` permits column removal |
| 167 | Add tests for migration generation | test | Add field, add table, remove with @drop, rename detection |
| 168 | Document migration system | docs | Update CLAUDE.md |

### 3.4 Serialization Annotations

| # | Task | Type | Description |
|---|------|------|-------------|
| 169 | Add `@` annotation syntax to lexer/parser | impl | General-purpose annotation parsing (`@name` before declarations) |
| 170 | Implement `@json` annotation | impl | Generate `to_json()`/`from_json()` methods on annotated structs |
| 171 | Implement JSON serializer in standard library | impl | Runtime JSON encoding/decoding |
| 172 | Implement `@msgpack` annotation | impl | Generate msgpack serialization methods |
| 173 | Implement `@csv` annotation | impl | Generate CSV serialization methods |
| 174 | Add pass tests for @json | test | Serialize/deserialize structs, nested structs, lists |
| 175 | Add fail tests for serialization | test | Malformed JSON input returns `Result::err` |
| 176 | Document serialization | docs | Update CLAUDE.md |

### 3.5 gRPC and Protocol Buffers

| # | Task | Type | Description |
|---|------|------|-------------|
| 177 | Implement `@grpc` annotation on structs | impl | Generate protobuf-compatible serialization |
| 178 | Implement `@grpc` annotation on protocols | impl | Generate service stubs (client + server interfaces) |
| 179 | Implement protobuf wire format in stdlib | impl | Binary serialization compatible with protobuf v3 |
| 180 | Implement gRPC client runtime | impl | HTTP/2 transport, stub calling |
| 181 | Implement gRPC server runtime | impl | HTTP/2 listener, request dispatch to impl |
| 182 | Add pass tests for gRPC | test | Define service, generate stubs, client-server roundtrip |
| 183 | Add interop test with Go/Python gRPC client | test | Cross-language validation |
| 184 | Document gRPC support | docs | Update CLAUDE.md |

### 3.6 HTTP Standard Library

| # | Task | Type | Description |
|---|------|------|-------------|
| 185 | Implement `http.Server` in standard library | impl | Basic HTTP/1.1 server with routing |
| 186 | Implement `http.Request` / `http.Response` types | impl | Request parsing, response building |
| 187 | Implement route registration (`.get()`, `.post()`, etc.) | impl | Method-based routing API |
| 188 | Implement automatic JSON serialization for responses | impl | `http.ok(struct)` auto-calls `to_json()` if `@json` annotated |
| 189 | Implement `http.Client` for outgoing requests | impl | `http.get(url)`, `http.post(url, body)` |
| 190 | Add pass tests for HTTP server | test | Start server, make request, validate response |
| 191 | Document HTTP library | docs | Update CLAUDE.md |

### 3.7 GraphQL Standard Library

| # | Task | Type | Description |
|---|------|------|-------------|
| 192 | Implement `@graphql` annotation | impl | Generate GraphQL schema from struct |
| 193 | Implement `graphql.Server` in standard library | impl | GraphQL endpoint with query execution |
| 194 | Implement auto-resolver generation from table structs | impl | CRUD resolvers derived from query expressions |
| 195 | Implement custom resolver override | impl | User-defined resolvers replace generated ones |
| 196 | Add pass tests for GraphQL | test | Schema generation, query execution, mutations |
| 197 | Document GraphQL library | docs | Update CLAUDE.md |

### 3.8 Database Configuration and Tooling

| # | Task | Type | Description |
|---|------|------|-------------|
| 198 | Implement `blang.toml` project configuration | impl | Parse TOML config for database URL, driver, build options |
| 199 | Implement Postgres driver in stdlib | impl | libpq-based or native protocol |
| 200 | Implement SQLite driver in stdlib | impl | For local development and testing |
| 201 | Implement connection pooling | impl | Shared connection pool with configurable size |
| 202 | Add database integration tests | test | Full roundtrip: table struct → migrate → query → validate |
| 203 | Document database configuration | docs | Update CLAUDE.md |

---

## Cross-Cutting Concerns

These tasks span multiple phases and should be addressed incrementally.

### Documentation

| # | Task | Type | Description |
|---|------|------|-------------|
| 204 | Keep CLAUDE.md in sync with implementation | docs | Update after each feature lands — supported features, known issues, build instructions |
| 205 | Keep language_design.md implementation status current | docs | Move items from "Next Steps" to "Currently Working" as completed |
| 206 | Add examples/ directory | docs | One `.bl` file per major feature showing idiomatic usage |
| 207 | Write tutorial: "Your first BLang program" | docs | Hello world → functions → structs → queries |
| 208 | Write tutorial: "BLang for Rust developers" | docs | Side-by-side comparison of common patterns |

### Testing Infrastructure

| # | Task | Type | Description |
|---|------|------|-------------|
| 209 | Add `--verbose` output to `run_tests.sh` showing compiler stderr | test | Already exists but validate it works for new test types |
| 210 | Add codegen-specific tests (require LLVM) | test | Separate test category that only runs when LLVM is available |
| 211 | Add end-to-end execution tests | test | Parse → compile → run → check exit code AND stdout |
| 212 | Add regression test for each bug fix | test | Every fixed bug gets a test that would have caught it |
| 213 | Implement test timeout handling | test | Already 10s in `run_tests.sh`; ensure new test types also have timeouts |

### Build and CI

| # | Task | Type | Description |
|---|------|------|-------------|
| 214 | Add GitHub Actions CI workflow | infra | Build (with and without LLVM), run tests, on every push/PR |
| 215 | Add `install_deps.sh` to CI | infra | Ensure LLVM dev headers installed in CI |
| 216 | Add build matrix: Linux + macOS | infra | Test on both platforms |
| 217 | Add `blang` wrapper script or rename `qcc` | impl | User-facing binary should be `blang`, not `qcc` |

---

## Summary

| Phase | Tasks | Focus |
|-------|-------|-------|
| Phase 1 | 1–76 | Core language: fn syntax, structs, protocols, generics, Result/Option, modules |
| Phase 2 | 77–133 | Safety and concurrency: ownership, spawn/chan, async/await, contracts, testing |
| Phase 3 | 134–203 | Data and services: pipeline, queries, migrations, serialization, gRPC, HTTP, GraphQL |
| Cross-cutting | 204–217 | Documentation, test infrastructure, CI |
| **Total** | **217** | |

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
