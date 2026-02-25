# Phase 3 — Data and Services: Implementation Plan

## Overview

Phase 3 contains 70 tasks (134–203), all unstarted. These tasks add BLang's data layer: pipeline operator, table structs, compile-time query expressions, automatic migrations, serialization annotations, gRPC, HTTP, and GraphQL.

The work decomposes into **8 sub-phases** with clear dependency ordering.

---

## Dependency Graph

```
3.1 Pipeline Operator ──┐
                        ├──> 3.2 Table Structs & Queries ──> 3.3 Migrations
                        │           │
3.4 Annotations ────────┘           │
        │                           │
        ├──> 3.5 gRPC               │
        │                           │
        └──> 3.4 @json ─────> 3.6 HTTP Server ──> 3.7 GraphQL
                                    │
                              3.8 DB Config
```

**Critical path**: Pipeline Operator → Table Structs/Queries → Migrations → DB Config

---

## Current Foundation

**What exists that Phase 3 builds on:**
- Lexer handles two-char tokens (`->`, `..`, `||`, etc.) — pattern for adding `|>`
- `StructDefinition` AST with fields, methods, generic params — extensible with `mIsTable` flag
- `MethodCallExpression` — correct shape for query pipeline steps like `.where {}`, `.limit()`
- `FieldAccessExpression` — needed for `.field` references in query predicates
- `TryExpression` / `MatchExpression` — query results return `Result<T, DBError>`
- Runtime library (`runtime/blang_runtime.h/c`) with ARC, threads, channels — pattern for new modules
- `Module` stores typed definition lists (`mStructList`, `mEnumList`) — add `mTableList`

**What does NOT exist:**
- No `|>` token, no `@` token in lexer
- No annotation AST nodes
- No `table`, `query`, `insert`, `update`, `delete` keywords
- No SQL generation, no database runtime, no JSON/HTTP/gRPC runtime
- No `blang.toml` configuration parser

---

## Sub-phase 3.1 — Pipeline Operator (Tasks 134–138)

**What**: Add `|>` as a two-character operator token. Parse `expr |> fn(args)` as sugar for `fn(expr, args)`.

### Tasks

| # | Task | Type | Details |
|---|------|------|---------|
| 134 | Add `|>` token to lexer | impl | In `FileLexer.cpp`, when we see `|`, peek next char. If `>`, emit `PIPE_ARROW`. Add `PIPE_ARROW` to `LexerSymbols` enum. |
| 135 | Parse pipeline expressions | impl | Create `PipelineExpression` AST node in `Expression.h`. Add `|>` as lowest-precedence binary operator in `QExpression.cpp`. Desugar at parse time: `expr |> fn(args)` → `fn(expr, args)`. |
| 136 | Codegen for pipeline | impl | If desugared at parse time, codegen is free — `CodeGen` sees a `CallExpression`. |
| 137 | Add pass tests | test | `pipeline_basic.b`, `pipeline_chained.b`, `pipeline_method.b` |
| 138 | Document pipeline | docs | Update CLAUDE.md |

**Approach**: Desugar at parse time. The parser rewrites `PipelineExpression` into `CallExpression` so codegen never sees it. This is the simplest approach and matches how `|>` works in F#/Elixir.

**Complexity**: Small (~100 lines new code)

---

## Sub-phase 3.2 — Table Structs and Query Expressions (Tasks 139–157)

**What**: `table struct` definitions with compile-time schema awareness. `query`/`insert`/`update`/`delete` expression syntax using `|>` pipeline.

**Depends on**: 3.1 (pipeline operator)

### Tasks — Parsing (no LLVM needed)

| # | Task | Type | Details |
|---|------|------|---------|
| 139 | Add `table` keyword to lexer | impl | New keyword in `FileLexer.cpp` |
| 140 | Parse `table struct` | impl | Check for `table` before `struct` in parser. Set `mIsTable` flag on `StructDefinition`. |
| 141 | Schema metadata storage | impl | Track table structs in `Module`. Store field names/types for compile-time validation. |
| 142 | Add `query` keyword | impl | New keyword |
| 143 | Add `insert` keyword | impl | New keyword |
| 144 | Add `update` keyword | impl | New keyword |
| 145 | Add `delete` keyword | impl | New keyword |
| 146 | Parse `query T |> where {} |> order_by {} |> limit()` | impl | `QueryExpression` AST node. Pipeline steps parsed as chained `|>` calls. `where`/`order_by` bodies use `.field` syntax. |
| 147 | Parse `insert T { field: value }` | impl | `InsertExpression` AST node. Struct-literal-like syntax after type name. |
| 148 | Parse `update T |> where {} |> set {}` | impl | `UpdateExpression` AST node. Pipeline syntax. |
| 149 | Parse `delete T |> where {}` | impl | `DeleteExpression` AST node. Pipeline syntax. |

### Tasks — Codegen and Runtime

| # | Task | Type | Details |
|---|------|------|---------|
| 150 | Compile-time field validation | impl | Resolve `.field` in query blocks against table struct field list. Error on unknown fields. |
| 151 | SQL generation backend | impl | `SQLGen` class: walks query AST → emits parameterized SQL. |
| 152 | Database runtime library | impl | `runtime/blang_db.h/c` — connection pooling, prepared statements, result mapping. SQLite first. |
| 153 | `@db("name")` annotation | impl | Parse `@db("name")` before query expressions (depends on 3.4 annotation parsing). |
| 154 | Pass tests for queries | test | Select, insert, update, delete, joins, chained pipelines |
| 155 | Fail tests for queries | test | Wrong field name, type mismatch |
| 156 | End-to-end SQLite test | test | Parse → SQL → execute against real database |
| 157 | Document query system | docs | Update CLAUDE.md |

**Complexity**: Large (~500-800 lines parser, ~300 lines SQL gen)

---

## Sub-phase 3.3 — Automatic Migrations (Tasks 158–168)

**What**: Schema diff engine comparing current `table struct` definitions against stored snapshots. Generates migration SQL.

**Depends on**: 3.2 (table structs)

| # | Task | Type | Details |
|---|------|------|---------|
| 158 | Schema snapshot storage | impl | Store schema as JSON in `.blang/schema.json` |
| 159 | Schema diff engine | impl | Compare current table structs vs snapshot. Detect new tables, new fields, removed fields. |
| 160 | Generate `CREATE TABLE` | impl | For new tables |
| 161 | Generate `ALTER TABLE ADD COLUMN` | impl | For new fields |
| 162 | Detect destructive changes | impl | Removed/renamed fields flagged unless `@drop` present |
| 163 | `blang migrate --preview` | impl | CLI subcommand in `bcc.cpp` |
| 164 | `blang migrate --apply` | impl | Execute migration SQL against database |
| 165 | `blang migrate --generate` | impl | Write migration SQL to file |
| 166 | `@drop` annotation | impl | Depends on 3.4 annotation parsing |
| 167 | Tests for migrations | test | Add field, add table, remove with @drop |
| 168 | Document migrations | docs | Update CLAUDE.md |

**Complexity**: Medium (~400 lines)

---

## Sub-phase 3.4 — Serialization Annotations (Tasks 169–176)

**What**: `@annotation` syntax in lexer/parser, then `@json` code generation.

**Depends on**: Nothing (can parallel with 3.1). Unblocks 3.2 task 153, 3.3 task 166, 3.5.

| # | Task | Type | Details |
|---|------|------|---------|
| 169 | Add `@` annotation syntax | impl | Lexer: `@` → `AT_SIGN` token. Parser: parse `@name` and `@name("arg")` before declarations. Add `mAnnotations` vector to `StructDefinition`, `FunctionDefinition`, `EnumDefinition`. |
| 170 | Implement `@json` | impl | Generate `to_json(self) -> string` and `from_json(string) -> Result<T, ParseError>` methods via codegen. |
| 171 | JSON serializer in stdlib | impl | `runtime/blang_json.h/c` — JSON encode/decode |
| 172 | `@msgpack` | impl | Binary format. **Lower priority — defer.** |
| 173 | `@csv` | impl | CSV format. **Lower priority — defer.** |
| 174 | Pass tests for @json | test | Serialize/deserialize structs |
| 175 | Fail tests | test | Malformed JSON returns error |
| 176 | Document serialization | docs | Update CLAUDE.md |

**Complexity**: Medium (~150 lines annotation parsing, ~300 lines JSON runtime)

---

## Sub-phase 3.5 — gRPC and Protocol Buffers (Tasks 177–184)

**What**: `@grpc` on structs generates protobuf serialization. `@grpc` on protocols generates service stubs.

**Depends on**: 3.4 (annotations)

| # | Task | Type | Details |
|---|------|------|---------|
| 177 | `@grpc` on structs | impl | Generate protobuf wire format methods |
| 178 | `@grpc` on protocols | impl | Generate client stub + server interface |
| 179 | Protobuf wire format in stdlib | impl | `runtime/blang_protobuf.h/c` |
| 180 | gRPC client runtime | impl | HTTP/2 transport — likely wraps `libgrpc` or `nghttp2` |
| 181 | gRPC server runtime | impl | HTTP/2 listener, request dispatch |
| 182 | Pass tests | test | Service definition, stubs, roundtrip |
| 183 | Interop test | test | Cross-language with Go/Python |
| 184 | Document gRPC | docs | Update CLAUDE.md |

**Complexity**: Very large. Consider wrapping existing C libraries rather than reimplementing HTTP/2.

---

## Sub-phase 3.6 — HTTP Standard Library (Tasks 185–191)

**What**: `http.Server` and `http.Client` in the standard library.

**Depends on**: 3.4 (@json for auto-serialization)

| # | Task | Type | Details |
|---|------|------|---------|
| 185 | `http.Server` | impl | `runtime/blang_http.h/c` — HTTP/1.1 server with routing |
| 186 | `http.Request` / `http.Response` | impl | Request parsing, response building |
| 187 | Route registration | impl | `.get()`, `.post()`, etc. |
| 188 | Auto JSON serialization | impl | `http.ok(struct)` calls `to_json()` if `@json` |
| 189 | `http.Client` | impl | Outgoing requests |
| 190 | Pass tests | test | Server start, request, validate response |
| 191 | Document HTTP | docs | Update CLAUDE.md |

**Complexity**: Large (~1000+ lines C runtime)

---

## Sub-phase 3.7 — GraphQL Standard Library (Tasks 192–197)

**What**: `@graphql` annotation and `graphql.Server` with auto-generated resolvers from table structs.

**Depends on**: 3.2 (table structs), 3.4 (annotations), 3.6 (HTTP transport)

| # | Task | Type | Details |
|---|------|------|---------|
| 192 | `@graphql` annotation | impl | Generate GraphQL schema from struct fields |
| 193 | `graphql.Server` | impl | Query parser and executor |
| 194 | Auto-resolver generation | impl | CRUD resolvers from table structs |
| 195 | Custom resolver override | impl | User-defined replaces generated |
| 196 | Pass tests | test | Schema, queries, mutations |
| 197 | Document GraphQL | docs | Update CLAUDE.md |

**Complexity**: Large

---

## Sub-phase 3.8 — Database Configuration (Tasks 198–203)

**What**: `blang.toml` project config, database drivers, connection pooling.

**Depends on**: 3.2 (queries need a database)

| # | Task | Type | Details |
|---|------|------|---------|
| 198 | `blang.toml` config | impl | TOML parser for database URL, driver |
| 199 | Postgres driver | impl | Wraps `libpq` |
| 200 | SQLite driver | impl | Wraps `libsqlite3` |
| 201 | Connection pooling | impl | Shared pool, configurable size |
| 202 | Integration tests | test | Full roundtrip: table → migrate → query → validate |
| 203 | Document DB config | docs | Update CLAUDE.md |

**Complexity**: Medium-large

---

## Recommended Execution Order

### Batch 1: Parser-only (no LLVM, no runtime — validate syntax)
1. **3.1** Pipeline Operator — lexer + parser (tasks 134-138)
2. **3.4** Annotation parsing infrastructure only (task 169)
3. **3.2** Table structs + query parsing only (tasks 139-149)

### Batch 2: Core codegen + runtime
4. **3.4** `@json` implementation (tasks 170-176)
5. **3.2** Query codegen — SQL generation + DB runtime (tasks 150-157)
6. **3.8** Database config + drivers (tasks 198-203)
7. **3.3** Automatic migrations (tasks 158-168)

### Batch 3: Services (heaviest — can be deferred to Phase 3b)
8. **3.6** HTTP server (tasks 185-191)
9. **3.5** gRPC (tasks 177-184)
10. **3.7** GraphQL (tasks 192-197)

---

## Scope Decisions

1. **Parser-first approach**: Implement all parsing (Batch 1) before any codegen/runtime. This follows the successful Phase 1/2 pattern and validates the syntax design with tests before investing in runtime work.

2. **Defer @msgpack/@csv** (tasks 172-173): Low value early. Focus on `@json` first.

3. **Defer gRPC/GraphQL** (tasks 177-197): These are the heaviest sub-phases and depend on substantial runtime infrastructure (HTTP/2, query parsing). They could become a separate Phase 3b.

4. **SQLite-first**: Start with SQLite driver (task 200) for testing. Postgres (task 199) follows once the query pipeline works end-to-end.

5. **Wrap, don't reimplement**: For HTTP, gRPC, and database drivers, wrap existing C libraries (`libsqlite3`, `libpq`, `libcurl`/`libuv`) rather than writing from scratch.

---

## Files to Create/Modify

### New Files (Parser)
| File | Purpose |
|---|---|
| `QPipelineExpression.cpp` | Pipeline `|>` parsing and desugaring |
| `QQueryExpression.cpp` | `query`, `insert`, `update`, `delete` expression parsing |
| `QAnnotation.cpp` | `@name` annotation parsing |

### Modified Files (Parser)
| File | Changes |
|---|---|
| `FileLexer.h` | Add `PIPE_ARROW`, `AT_SIGN`, new keyword enums |
| `FileLexer.cpp` | Add `|>` token, `@` token, keywords: `table`, `query`, `insert`, `update`, `delete` |
| `Expression.h` | Add `PipelineExpression`, `QueryExpression`, `InsertExpression`, `UpdateExpression`, `DeleteExpression`, `AnnotationNode` |
| `Type.h` | Add `mIsTable` to `StructDefinition`, `mAnnotations` to definition classes |
| `QExpression.cpp` | Integrate pipeline operator at low precedence |
| `QStructDefinition.cpp` | Handle `table struct` prefix |
| `QStatement.cpp` | Route `query`/`insert`/`update`/`delete` to query parser |
| `CodeGen.cpp` | Add codegen for pipeline (if not desugared), queries, annotations |
| `CMakeLists.txt` | Add new .cpp files to build |

### New Files (Runtime)
| File | Purpose |
|---|---|
| `runtime/blang_db.h/c` | Database abstraction, connection pooling, SQL execution |
| `runtime/blang_json.h/c` | JSON encode/decode |
| `runtime/blang_http.h/c` | HTTP server/client |
| `runtime/blang_protobuf.h/c` | Protobuf wire format |

### New Test Files
| File | Category |
|---|---|
| `test_files/pass/pipeline_basic.b` | Pipeline syntax |
| `test_files/pass/pipeline_chained.b` | Chained pipelines |
| `test_files/pass/table_struct.b` | Table struct definition |
| `test_files/pass/query_basic.b` | Basic query expression |
| `test_files/pass/query_insert.b` | Insert expression |
| `test_files/pass/query_update.b` | Update expression |
| `test_files/pass/query_delete.b` | Delete expression |
| `test_files/pass/annotation_json.b` | @json annotation |
| `test_files/fail/pipeline_missing_fn.b` | Pipeline without function |
| `test_files/fail/query_bad_field.b` | Unknown field in query |
| `test_files/fail/table_missing_struct.b` | `table` without `struct` |
