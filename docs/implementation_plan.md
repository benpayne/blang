# BLang Implementation Plan

## Current State (as of March 2026)

### What Works Today

**Lexer**: Hand-written lexer recognizes types (`int`, `char`, `string`, `void`, `float`, `double`), control flow keywords (`if`, `else`, `while`, `for`, `return`), `extern`, identifiers, integer/float/string/char literals, all arithmetic/comparison/logical/bitwise operators, assignment operators (`=`, `+=`, `-=`, `*=`, `/=`), braces, parens, commas, semicolons, single-line and multi-line comments. Additional keywords: `fn`, `bool`, `struct`, `impl`, `self`, `protocol`, `match`, `import`, `pub`, `break`, `continue`, `enum`, `in`, `own`, `shared`, `sync`, `spawn`, `chan`, `async`, `await`, `on`, `requires`, `ensures`, `test`, `assert`, `table`, `query`, `insert`, `update`, `delete`, `cstring`, `carray`. Additional tokens: `->` (arrow), `..` (range), `_` (wildcard), `?` (try operator), `|>` (pipeline), `@` (annotation), `true`/`false` (boolean constants), float constants.

**Parser (AST)**: Recursive-descent parser builds a complete AST supporting all Phase 1, Phase 2, and Phase 3 syntax constructs. See CLAUDE.md for full list.

**Codegen (LLVM 18+, conditional)**: When built with `llvm-18-dev`, `CodeGen` class generates LLVM IR for: functions, extern declarations, variables, control flow (if/else, while, for-in, break/continue), expressions (arithmetic, comparison, logical, bitwise with type coercion), strings (safe BlangString type with interpolation and methods), arrays (safe BlangArray with bounds checking), structs (literal construction, field access, method calls), enums (tagged union layout with match/destructure), generics (monomorphization), Result/Option `?` operator (tag check, payload extraction, early error return), pipeline operator, ownership (move semantics, use-after-move detection), ARC for shared/sync, spawn (closure extraction, thread pool dispatch), async/await, channels, contracts (requires/ensures), test blocks, assert, `@json` annotation codegen, database query/insert/update/delete codegen, and multi-module type sharing.

**Tests**: 143 tests in `run_tests.sh` — 99 pass, 40 fail (negative), 4 cgfail. 36 end-to-end codegen tests via `test_codegen.sh`. GitHub Actions CI configured.

**Runtime Libraries**: `blang_string` (safe immutable string), `blang_array` (safe growable array), `blang_json` (JSON encode/decode), `blang_db` (database abstraction with optional SQLite), `blang_runtime` (ARC, thread pool, channels, async event loop). SQL generation (`SQLGen`) and schema migration (`SchemaMigration`) libraries.

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
| 26 | Codegen for struct types | impl | YES | LLVM struct type mapping (`getOrCreateStructType`), field access (`genFieldAccess`), struct literal (`genStructLiteral`), generic struct instantiation |
| 27 | Codegen for method calls | impl | YES | Method dispatch (`genMethodCall`), impl block methods emitted as `StructName_methodName`, self passed as first arg; builtin string/array methods also supported |
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
| 42 | Implement generic type instantiation | impl | YES | Monomorphization via `instantiateGenericStruct()` — stamps out concrete types like `Box_int`; cached in `mGenericInstanceMap` |
| 43 | Codegen for generic functions | impl | YES | Monomorphization via `instantiateGenericFunction()` — stamps out concrete functions; explicit type args required (no inference); generic methods/protocols not yet supported |
| 44 | Add pass tests for generics | test | YES | `generic_fn.c`, `generic_struct.c`, `generic_constraint.c`, `generic_protocol.c`, `generic_type_args.c` |
| 45 | Add fail tests for generics | test | YES | `generic_unknown_constraint.b`, `generic_duplicate_param.b` |
| 46 | Document generics | docs | YES | Documented in CLAUDE.md |

### 1.6 Result/Option Types and `?` Operator

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 47 | Implement `Result<T, E>` as built-in generic type | impl | YES | Registered in gScope (`EnumDefinition::CreateBuiltinResult`) and mEnumDefMap; type-erased 8-byte payload, concrete T/E recovered at match/`?` from the subject's type args; user-defined `Result` still shadows it. Tested in `codegen_builtin_result.b`, `codegen_builtin_try.b` |
| 48 | Implement `Option<T>` as built-in generic type | impl | YES | Registered in gScope (`EnumDefinition::CreateBuiltinOption`) and mEnumDefMap; channel `recv()` now returns this same built-in `Option<T>`. Tested in `codegen_builtin_option.b`, `cgfail/builtin_option_non_exhaustive.b` |
| 49 | Add `match` keyword to lexer | impl | YES | `match` is a recognized keyword token |
| 50 | Parse `match` expressions | impl | YES | `QMatchExpression.cpp` — literal, wildcard `_`, destructuring patterns |
| 51 | Parse `?` operator | impl | YES | `QUESTION_MARK` token in lexer; `TryExpression` AST node; postfix parsing in `ParsePrimary` |
| 52 | Codegen for Result/Option | impl | YES | Enum tagged union layout `{i32 tag, [N x i8] payload}` via `genEnumConstruct`; tested in `codegen_result_type.b` and `codegen_enum_payload.b` |
| 53 | Codegen for match | impl | YES | `genMatchExpression` — tag extraction, switch dispatch, variant pattern matching with payload extraction and binding, wildcard arms, and enum exhaustiveness checking (missing variant without `_` is a compile error; tested in `cgfail/match_non_exhaustive.b`, `codegen_match_exhaustive.b`, `codegen_match_wildcard_enum.b`) |
| 54 | Codegen for `?` operator | impl | YES | `genTryExpression` — resolves operand's enum type, extracts tag, branches on success (ok/some) vs error (err/none), unwraps payload on success, propagates error via early return on failure; tested in `codegen_try_operator.b` |
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
| 63 | Implement symbol visibility checking | impl | DEFERRED | `isPublic()` tracked on symbols; cross-module enforcement deferred until cross-module name resolution exists |
| 64 | Enforce flat namespace rule | impl | YES | Enforced by parser — no module-qualified names in expressions |
| 65 | Enforce no function overloading | impl | YES | `Scope::addSymbol` returns false for duplicates; compile error on redefinition |
| 66 | Enforce mandatory pub type signatures | impl | YES | Already enforced by grammar — fn syntax always requires explicit parameter types |
| 67 | Codegen for multi-module | impl | YES | `registerExternalTypes()` shares struct/enum type definitions across CodeGen instances; type-level cross-module support works, function-level symbol linking pending |
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
| 77 | Add `own` keyword to lexer | impl | YES | Ownership qualifier |
| 78 | Add `shared` keyword to lexer | impl | YES | Shared reference-counted qualifier |
| 79 | Add `sync` keyword to lexer | impl | YES | Synchronized mutable qualifier |
| 80 | Parse ownership qualifiers on variable declarations | impl | YES | `own Buffer buf = Buffer.new(1024);` syntax parsed |
| 81 | Implement move semantics for `own` | impl | YES | Move semantics enforced in codegen: use-after-move detection, move-in-loop prevention, own variables cannot be captured across spawn boundaries |
| 82 | Implement use-after-move detection | impl | YES | `mMovedVariables` set tracked during codegen; use-after-move and move-in-loop emit compile errors; tested in `codegen_ownership_move.b` and cgfail tests |
| 83 | Implement ARC for `shared` types | impl | YES | `__blang_rc_alloc`/`__blang_rc_retain`/`__blang_rc_release` emitted at scope boundaries; tested in `codegen_shared_spawn.b` |
| 84 | Implement auto-locking for `sync` types | impl | YES | `__blang_sync_lock`/`__blang_sync_unlock` emitted around sync variable access; tested in `codegen_sync_locking.b` and `codegen_sync_spawn.b` |
| 85 | Add pass tests for ownership | test | YES | own_basic.b, shared_basic.b, sync_basic.b, ownership_all.b, own_move_valid.b |
| 86 | Add fail tests for ownership | test | YES | cgfail/own_use_after_move.b, cgfail/own_move_in_loop.b, cgfail/own_spawn_capture.b |
| 87 | Document ownership model | docs | YES | Update CLAUDE.md with ownership support |

### 2.2 Concurrency: spawn/chan

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 88 | Add `spawn` keyword to lexer | impl | YES | Green thread creation |
| 89 | Add `chan` keyword to lexer | impl | YES | Channel type |
| 90 | Parse `spawn { ... }` blocks | impl | YES | `spawn { ... }` parsed into SpawnStatement AST node |
| 91 | Parse channel declarations | impl | YES | `chan<T>` variable declarations parsed and codegen'd via `__blang_chan_create` |
| 92 | Parse channel operations | impl | YES | `chan<T>` type parsed (`QType.cpp`); `.send()`/`.recv()`/`.close()` parse as method calls; tested in `pass/chan_send_recv.b` |
| 93 | Implement BLang runtime: green thread scheduler | impl | YES | Thread pool with `__blang_spawn`, `__blang_spawn_wait`, `__blang_wait_all` in `blang_runtime.c` |
| 94 | Implement BLang runtime: channel implementation | impl | YES | `__blang_chan_create`/`__blang_chan_send`/`__blang_chan_recv`/`__blang_chan_close`/`__blang_chan_destroy` in `blang_runtime.c` |
| 95 | Codegen for spawn | impl | YES | Closure extraction: captured variables packed into context struct, dispatched to thread pool; tested in `codegen_spawn.b`, `codegen_spawn_threaded.b`, `codegen_shared_spawn.b` |
| 96 | Codegen for channel operations | impl | YES | `genChanMethodCall` emits `__blang_chan_send`/`__blang_chan_recv`/`__blang_chan_close`; `recv()` returns `Option<T>` (synthesized `Option_<T>` enum; `some` on success, `none` on closed+empty) so the closed signal is surfaced and exhaustive match enforces handling; tested in `codegen_channel.b`, `codegen_channel_spawn.b`, `codegen_channel_closed.b`, `cgfail/chan_recv_non_exhaustive.b` |
| 97 | Enforce thread safety rules | impl | YES | Own variables cannot be captured across spawn boundaries (compile error); shared/sync enforced via ARC and locking |
| 98 | Add pass tests for spawn/chan | test | YES | spawn_basic.b, spawn_nested.b, spawn_expr.b, chan_decl.b, wait_basic.b, wait_all_basic.b |
| 99 | Add fail tests for concurrency | test | YES | spawn_missing_brace.b, cgfail/own_spawn_capture.b |
| 100 | Document concurrency | docs | YES | Update CLAUDE.md |

### 2.3 Async/Await and Event Loop

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 101 | Add `async` keyword to lexer | impl | YES | Async function qualifier |
| 102 | Add `await` keyword to lexer | impl | YES | Await expression |
| 103 | Parse `async fn` declarations | impl | YES | `async fn` declarations set mIsAsync flag on FunctionDefinition |
| 104 | Parse `await` expressions | impl | YES | `await expr` parsed into AwaitExpression AST node |
| 105 | Implement BLang runtime: event loop | impl | YES | `__blang_async_call`/`__blang_await`/`__blang_task_destroy` in `blang_runtime.c` |
| 106 | Codegen for async functions | impl | YES | Async body extracted to `void*(void*)` wrapper, called via `__blang_async_call`; tested in `codegen_async.b`, `codegen_async_multi.b` |
| 107 | Codegen for await | impl | YES | `__blang_await` + `__blang_task_destroy`; tested in `codegen_wait.b`, `codegen_wait_all.b` |
| 108 | Add `on` keyword for event handlers | impl | YES | `on timer.every(1000) { ... }` |
| 109 | Parse event handler syntax | impl | YES | `on expr { body }` parsed into EventHandler AST node |
| 110 | Codegen for event handlers | impl | YES | `on EXPR { body }` extracts the body to a callback and registers it on the global event loop via `__blang_event_on` keyed by the fd that EXPR yields (timerfd or socket fd); refcounted captures are retained for deferred invocation. A non-fd event expr falls back to inline invocation (legacy). Runtime: poll-based loop with timerfds (`timer.every/after`, `timer.run/stop`); tested in `codegen_timer_event.b` |
| 111 | Add pass tests for async/await | test | YES | async_fn.b, async_fn_void.b, await_expr.b, event_handler.b |
| 112 | Document async model | docs | YES | Update CLAUDE.md |

### 2.4 Contracts

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 113 | Add `requires` keyword to lexer | impl | YES | Precondition keyword |
| 114 | Add `ensures` keyword to lexer | impl | YES | Postcondition keyword |
| 115 | Parse `requires` clauses on functions | impl | YES | `requires expr` parsed after return type on functions |
| 116 | Parse `ensures` clauses on functions | impl | YES | `ensures expr` parsed after return type on functions |
| 117 | Implement runtime contract checks | impl | YES | `genContractCheck` inserts assertion code at function entry (requires) and before return (ensures); tested in `codegen_contracts.b` |
| 118 | Implement compile-time contract checking (basic) | impl | — | Detect constant violations like `divide(x, 0)` (future optimization) |
| 119 | Add pass tests for contracts | test | YES | requires_basic.b, ensures_basic.b, contract_combined.b |
| 120 | Add fail tests for contracts | test | YES | requires_missing_expr.b |
| 121 | Document contracts | docs | YES | Update CLAUDE.md |

### 2.5 Built-in Testing

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 122 | Add `test` keyword to lexer | impl | YES | Test block keyword |
| 123 | Add `assert` keyword to lexer | impl | YES | Assertion keyword |
| 124 | Create `TestBlock` AST node | impl | YES | Named test block containing statements |
| 125 | Parse `test "name" { ... }` blocks | impl | YES | `test "name" { body }` parsed into TestBlock |
| 126 | Parse `assert expr` statements | impl | YES | `assert expr;` and `assert expr, "msg";` parsed into AssertStatement |
| 127 | Implement `blang test` command | impl | YES | `bcc test` discovers test files, compiles, and runs; test runner codegen emits `genTestRunner` |
| 128 | Strip test blocks from release builds | impl | — | `--release` flag omits test code from binary (future) |
| 129 | Test output formatting | impl | PARTIAL | Test runner reports pass/fail with names; file:line on failure pending |
| 130 | Add pass tests for test blocks | test | YES | test_basic.b, test_assert.b, test_assert_message.b, test_multiple.b, assert_in_function.b |
| 131 | Add fail tests for test blocks | test | YES | test_missing_name.b, test_missing_body.b, assert_missing_semi.b |
| 132 | Self-hosting milestone: BLang tests in BLang | test | — | Write compiler test suite using built-in `test` blocks (future milestone) |
| 133 | Document testing | docs | YES | Update CLAUDE.md with `blang test` usage |

---

## Phase 3 — Data and Services

### 3.1 Pipeline Operator

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 134 | Add `|>` operator to lexer | impl | YES | `PIPELINE` two-character operator token |
| 135 | Parse pipeline expressions | impl | YES | `expr |> fn(args)` desugars to `fn(expr, args)` via `PipelineExpression` AST node |
| 136 | Codegen for pipeline | impl | YES | `genPipelineExpression` desugars to function call; tested in `codegen_pipeline.b` |
| 137 | Add pass tests for pipeline | test | YES | pipeline_basic.b, pipeline_chained.b, pipeline_with_args.b |
| 138 | Document pipeline operator | docs | YES | Documented in CLAUDE.md |

### 3.2 Table Structs and Query Expressions

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 139 | Add `table` keyword to lexer | impl | YES | `table` keyword token |
| 140 | Parse `table struct` definitions | impl | YES | `table struct User { int id; string name; }` with `setIsTable(true)` |
| 141 | Implement schema metadata storage | impl | YES | `SchemaMigration` engine stores and diffs schema snapshots |
| 142 | Add `query` keyword to lexer | impl | YES | `query` keyword token |
| 143 | Add `insert` keyword to lexer | impl | YES | `insert` keyword token |
| 144 | Add `update` keyword to lexer | impl | YES | `update` keyword token |
| 145 | Add `delete` keyword to lexer | impl | YES | `delete` keyword token |
| 146 | Parse `query T |> where { } |> order_by { } |> limit()` | impl | YES | Full query pipeline with `QueryExpression` AST node |
| 147 | Parse `insert T { field: value }` | impl | YES | `InsertExpression` AST node |
| 148 | Parse `update T |> where { } |> set { }` | impl | YES | `UpdateExpression` AST node |
| 149 | Parse `delete T |> where { }` | impl | YES | `DeleteExpression` AST node |
| 150 | Compile-time field validation | impl | YES | Query/update/delete field refs and insert field names validated against the table struct (`validateQueryFields`/`validateInsertFields`); unknown field is a compile error. Test: cgfail/query_bad_field.b. (JOIN field refs validated against primary table only.) |
| 151 | SQL generation backend | impl | YES | `SQLGen` translates query AST to parameterized SQL (SELECT/INSERT/UPDATE/DELETE, CREATE TABLE) |
| 152 | Implement database runtime library | impl | YES | `blang_db` library with connection, query, result APIs; optional SQLite backend |
| 153 | Support `@db("name")` annotation for named connections | impl | YES | `[database.<name>]` parsed and registered via `__blang_db_register`; query codegen routes through `__blang_db_get("name")` when the table struct carries `@db("name")`, else the default connection |
| 154 | Add pass tests for queries | test | YES | table_struct.b, query_basic.b, query_insert.b, query_update.b, query_delete.b, query_join.b |
| 155 | Add fail tests for queries | test | YES | query_missing_table.b, insert_missing_brace.b, table_missing_struct.b |
| 156 | Add end-to-end test with SQLite | test | YES | `test_files/codegen_db_query.b` — insert/update/delete/select with bound WHERE/SET params against in-memory SQLite (run via test_codegen.sh, gated on SQLite) |
| 157 | Document query system | docs | YES | Documented in CLAUDE.md |

### 3.3 Automatic Migrations

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 158 | Implement schema snapshot storage | impl | YES | `SchemaMigration` persists schema state in `.blang/` directory |
| 159 | Implement schema diff engine | impl | YES | Compares current table structs against stored snapshot |
| 160 | Generate `CREATE TABLE` for new tables | impl | YES | `SchemaMigration` generates CREATE TABLE SQL |
| 161 | Generate `ALTER TABLE ADD COLUMN` for new fields | impl | YES | `SchemaMigration` generates ALTER TABLE ADD COLUMN |
| 162 | Detect and flag destructive changes | impl | YES | Detection implemented; `bcc migrate --apply` refuses destructive steps unless `--allow-destructive` is passed |
| 163 | Implement `blang migrate --preview` | impl | YES | `bcc migrate --preview` shows pending SQL |
| 164 | Implement `blang migrate --apply` | impl | YES | `bcc migrate --apply` executes migrations |
| 165 | Implement `blang migrate --generate` | impl | YES | `bcc migrate --generate` writes SQL to file |
| 166 | Add `@drop` annotation for confirmed removals | impl | PARTIAL | Destructive removals gated behind the `--allow-destructive` CLI confirmation; a per-entity `@drop` annotation (vs. the global flag) is still future |
| 167 | Add tests for migration generation | test | YES | `test_migrate.sh` — create table, add column, destructive-guard refuse/allow, end to end against SQLite |
| 168 | Document migration system | docs | YES | Documented in CLAUDE.md |

### 3.4 Serialization Annotations

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 169 | Add `@` annotation syntax to lexer/parser | impl | YES | `@name` and `@name("arg")` parsed before declarations |
| 170 | Implement `@json` annotation | impl | YES | Generates `StructName_to_json`/`StructName_from_json` functions; supports all primitive types and nested `@json` structs; tested in codegen_json*.b |
| 171 | Implement JSON serializer in standard library | impl | YES | `blang_json` runtime library with `blang_json_encode`/`blang_json_decode` |
| 172 | Implement `@msgpack` annotation | impl | — | Generate msgpack serialization methods |
| 173 | Implement `@csv` annotation | impl | — | Generate CSV serialization methods |
| 174 | Add pass tests for @json | test | YES | annotation_json.b, codegen_json.b, codegen_json_nested.b, codegen_json_roundtrip.b, codegen_json_types.b |
| 175 | Add fail tests for serialization | test | YES | cgfail/json_unsupported_field.b, fail/json_generic_struct.b |
| 176 | Document serialization | docs | YES | Documented in CLAUDE.md |

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
| 185 | Implement `http.Server` in standard library | impl | YES | `HttpServer` in `stdlib/net.b` — selector-backed HTTP/1.1 server with a route table; `serve()` parses each request and dispatches to the matching route |
| 186 | Implement `http.Request` / `http.Response` types | impl | YES | `HttpRequest` (method/path/body), `HttpResponse` (status/content_type/body); request parsing (`parse_http_request_line`, headers, body) and response building (`build_http_response`) |
| 187 | Implement route registration (`.get()`, `.post()`, etc.) | impl | YES | `HttpServer.get/post/put/route(method, path, handler)` build an `Array<Route>` route table; `dispatch_request` matches method+path → handler, else 404. (`delete` is a keyword — use `route("DELETE", ...)`.) Tested in `codegen_http_routing.b` |
| 188 | Implement automatic JSON serialization for responses | impl | YES | Builtin `to_json(value)` dispatches at compile time to `StructName_to_json` for a `@json` struct (compile error otherwise); `net.http_json(to_json(user))` returns an `application/json` response with the serialized struct. Tested in `codegen_to_json_builtin.b`, `codegen_http_json_response.b`, `cgfail/to_json_not_annotated.b` |
| 189 | Implement `http.Client` for outgoing requests | impl | YES | `http_get(host, port, path)` and `http_post(host, port, path, content_type, body)` — BLang-native Buffer I/O, return the response body |
| 190 | Add pass tests for HTTP server | test | YES | `codegen_http_routing.b` (route dispatch + get/post registration); `codegen_http_blang.b` (parsing/response building). Live socket serving verified manually (deterministic socket+thread E2E omitted from the suite) |
| 191 | Document HTTP library | docs | YES | Documented in CLAUDE.md; demo `demos/13_http_server.b` uses the routing API |

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
| 198 | Implement `blang.toml` project configuration | impl | YES | `ProjectConfig` class parses blang.toml for project metadata, type (bin/lib), and dependencies |
| 199 | Implement Postgres driver in stdlib | impl | PARTIAL | libpq backend implemented (`pg_open`/`pg_query`/`pg_exec` with `?`→`$n` rewrite), compile-guarded behind `BLANG_HAS_POSTGRES`; not yet exercised in CI (requires libpq-dev) |
| 200 | Implement SQLite driver in stdlib | impl | YES | SQLite backend with runtime parameter binding behind a driver-dispatch layer; tested via codegen_db_query.b + test_migrate.sh |
| 201 | Implement connection pooling | impl | — | Deferred; process uses a single shared default/named connection (sufficient for SQLite/single-threaded). Pooling for file/Postgres connections is future |
| 202 | Add database integration tests | test | YES | `test_migrate.sh` (migrate → apply against SQLite) + `codegen_db_query.b` (insert/update/delete/query roundtrip with bound params) |
| 203 | Document database configuration | docs | YES | CLAUDE.md: `[database]`/`[database.<name>]` blang.toml format, BLANG_DATABASE_URL fallback, migrate workflow, driver status |

---

## Cross-Cutting Concerns

These tasks span multiple phases and should be addressed incrementally.

### Documentation

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 204 | Keep CLAUDE.md in sync with implementation | docs | YES | CLAUDE.md is comprehensive and current |
| 205 | Keep language_design.md implementation status current | docs | PARTIAL | CLAUDE.md is more current; language_design.md needs update |
| 206 | Add examples/ directory | docs | YES | demos/ directory with example programs |
| 207 | Write tutorial: "Your first BLang program" | docs | — | |
| 208 | Write tutorial: "BLang for Rust developers" | docs | — | |

### Testing Infrastructure

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 209 | Add `--verbose` output to `run_tests.sh` showing compiler stderr | test | YES | `--verbose` flag shows stderr on failure |
| 210 | Add codegen-specific tests (require LLVM) | test | YES | `test_codegen.sh` with 36 E2E tests; cgfail/ category in run_tests.sh |
| 211 | Add end-to-end execution tests | test | YES | `test_codegen.sh` runs full pipeline (parse → IR → compile → link → run) and checks exit codes |
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

## Phase 4 — Build System and Dependencies

### 4.1 AST Prerequisites for .bmod Emission

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 218 | Add `isPublic` to `EnumDefinition` | impl | YES | `mIsPublic` flag and `isPublic()` getter, `isPublic` param on `Parse()` |
| 219 | Add `isPublic` to `ProtocolDefinition` | impl | YES | `mIsPublic` flag and `isPublic()` getter, `isPublic` param on `Parse()` |
| 220 | Pass `isPublic` from Module::Parse | impl | YES | `pub enum` and `pub protocol` now correctly set visibility |
| 221 | Add `mProtocolList` to Module | impl | YES | Protocols stored on Module; `getProtocolList()` getter added |
| 222 | Add public getters to Module | impl | YES | `getFunctionList()`, `getImports()`, `getStructList()`, `getEnumList()`, `getProtocolList()` |
| 223 | Add `mIsExtern` to Module | impl | YES | `isExtern()`/`setExtern()` to mark .bmod-loaded modules as extern-only |

### 4.2 .bmod Interface File Emission

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 224 | Create `BmodEmitter` class | impl | YES | `BmodEmitter.h/cpp` — walks Module AST, emits only `pub` symbols in BLang syntax |
| 225 | Emit pub functions as signature-only | impl | YES | `pub fn name(type param, ...) -> rettype;` (no body) |
| 226 | Emit pub structs with full fields | impl | YES | `pub struct Name { fields }` with annotations and generics |
| 227 | Emit pub enums with full variants | impl | YES | `pub enum Name { variants }` with associated types |
| 228 | Emit pub protocols with method sigs | impl | YES | `pub protocol Name { fn method(...) -> type; }` |
| 229 | Preserve annotations on emission | impl | YES | `@json`, `@db("name")` etc. preserved in .bmod output |
| 230 | Add `--emit-bmod <file>` flag to qcc | impl | YES | Runs after parsing, emits .bmod file |

### 4.3 .bmod Consumption

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 231 | Detect `.bmod` extension in qcc | impl | YES | .bmod files parsed first in two-phase parsing |
| 232 | Mark .bmod modules as extern-only | impl | YES | Module `setExtern(true)` for .bmod-loaded modules |
| 233 | Skip codegen for extern modules | impl | YES | Extern modules provide types only, no IR generated |
| 234 | Flat-merge pub symbols into scope | impl | YES | All pub symbols from .bmod injected into global scope; `import mathlib;` resolves by name |
| 235 | Auto-declare extern functions from .bmod | impl | YES | Functions marked extern via `setFunctionExtern(true)`; auto-declared in LLVM module on first use |

### 4.4 blang.toml and `bcc build`

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 236 | Create minimal TOML parser | impl | YES | `ProjectConfig.cpp` — handles `[section]`, `key = "value"`, inline tables, comments |
| 237 | Create `ProjectConfig` class | impl | YES | `ProjectConfig.h/cpp` — parses blang.toml into name, version, type, dependencies |
| 238 | Add `bcc build` subcommand | impl | YES | Discovers blang.toml, auto-discovers .b sources, builds lib (.a+.bmod) or bin (executable) |
| 239 | Add `bcc clean` subcommand | impl | YES | Removes `~/.cache/blang/` build cache directory |

### 4.5 Dependency Resolution

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 240 | Implement dependency graph traversal | impl | YES | Topological sort of deps from blang.toml |
| 241 | Recursive dependency building | impl | YES | Each dep built recursively; .bmod and .a collected |
| 242 | Pass dep .bmod files to qcc | impl | YES | Dependency .bmod files passed as additional input |
| 243 | Pass dep .a files to linker | impl | YES | Dependency .a files linked into final binary |
| 244 | Detect circular dependencies | impl | YES | Error with clear message on circular dep chains |

### 4.6 Content-Addressable Build Cache

| # | Task | Type | Done | Description |
|---|------|------|------|-------------|
| 245 | Bundle standalone SHA-256 | impl | YES | `sha256.h/c` — public domain C implementation |
| 246 | Create `BuildCache` class | impl | YES | `BuildCache.h/cpp` — `computeKey()`, `lookup()`, `store()`, `clean()` |
| 247 | Hash-based cache key computation | impl | YES | SHA-256 of source files + blang.toml + dep hashes |
| 248 | Cache lookup and store | impl | YES | `~/.cache/blang/objects/<hash>/` with .a and .bmod artifacts |
| 249 | Integrate cache into `bcc build` | impl | YES | Cache checked before building each dependency; second build skips unchanged deps |
| 250 | Add `test_build/` integration test | test | YES | mathlib (lib) + myapp (bin) project pair; verifies full bcc build flow |

---

## Summary

| Phase | Tasks | Focus | Done | Partial | Deferred | Remaining |
|-------|-------|-------|------|---------|----------|-----------|
| Phase 1 | 1–76 | Core language: fn syntax, structs, protocols, generics, Result/Option, modules | 75 | 0 | 1 | 0 |
| Phase 2 | 77–133 | Safety and concurrency: ownership, spawn/chan, async/await, contracts, testing | 50 | 1 | 0 | 6 |
| Phase 3 | 134–203 | Data and services: pipeline, queries, migrations, serialization, gRPC, HTTP, GraphQL | 51 | 2 | 0 | 17 |
| Phase 4 | 218–250 | Build system: .bmod, blang.toml, deps, cache | 33 | 0 | 0 | 0 |
| Cross-cutting | 204–217 | Documentation, test infrastructure, CI | 9 | 3 | 0 | 2 |
| **Total** | **250** | | **218** | **6** | **1** | **25** |

### Phase 1 Complete

All Phase 1 tasks are done or explicitly deferred:
- **Task 47/48** (built-in Result/Option): Done — `Option<T>` and `Result<T,E>` are registered as built-in generic enums (no user definition required). They use a type-erased pointer-sized payload; the concrete type argument is recovered at the match/`?` site from the subject's static type. A user-defined `Option`/`Result` shadows the built-in (user defs land in a child scope).
- **Task 63** (visibility checking): Deferred — requires cross-module name resolution which depends on multi-module function linking (not just type sharing).

### Phase 4 Complete

All Phase 4 tasks are done:
- **blang.toml** project manifest with `[project]` (name, version, type) and `[deps]` (local path dependencies)
- **.bmod interface files** emitted via `qcc --emit-bmod` and consumed via two-phase parsing with flat symbol merge
- **`bcc build`** recursively builds dependency graph, produces .a+.bmod for libraries and executables for binaries
- **`bcc clean`** removes the build cache
- **Content-addressable cache** at `~/.cache/blang/objects/<hash>/` skips rebuilding unchanged dependencies
- **Integration test** in `test_build/` verifies the full lib→bin build flow
