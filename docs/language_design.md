# BLang Language Design Specification

## Vision

BLang is a compiled, native-performance language designed for **clarity, safety, and LLM code generation**. It takes the best ideas from C, Rust, Swift, Go, and Python while enforcing a single canonical way to express each concept. The language should be simple enough that an AI model can reliably generate correct programs, yet powerful enough for systems-level work including threading, networking, and hardware synthesis.

## Design Principles

### 1. One Right Way

Every concept has exactly one syntax. There are no equivalent alternatives.

- One function declaration syntax (`fn`)
- Two loop constructs: `for ... in` (iteration) and `while` (conditional)
- One error handling pattern (`Result` + `?`)
- One concurrency primitive pair (`spawn` + `chan`)
- One memory model (ownership + automatic reference counting)

This constrains the output space for both humans and LLMs. If there is only one way to write a loop, it is impossible to pick the wrong one.

### 2. Explicit Over Implicit

Nothing happens invisibly. No implicit conversions, no hidden copy constructors, no invisible control flow.

- Type conversions require explicit calls: `float.from(x)`
- Errors are values, not exceptions — control flow is always visible
- Memory ownership is declared, not inferred from usage patterns
- Thread safety constraints are part of the type, not runtime assertions

### 3. Safe by Default

The compiler prevents entire categories of bugs at compile time.

- No null pointers — use `Option<T>` instead
- No data races — ownership rules enforced at compile time
- No use-after-free — ownership and ARC guarantee validity
- No buffer overflows — bounds-checked collections
- No unhandled errors — `Result` must be consumed

### 4. Native Performance

BLang compiles to native code via LLVM. The runtime is minimal — no interpreter, no JIT. Stack allocation is preferred, heap allocation is explicit, and the optimizer has full visibility.

### 5. LLM-Optimized Syntax

The language is designed so that token-based language models produce correct code at high rates.

- Regular grammar — patterns learned once apply everywhere
- Always braces — no dangling-else ambiguity
- Required semicolons — no newline-sensitivity edge cases
- Keywords over symbols — `fn`, `spawn`, `own` are unambiguous tokens
- Small keyword set — fewer things to remember, fewer things to hallucinate

## Syntax Overview

### Functions

All functions use the `fn` keyword. Return types follow the `->` arrow. `void` return is expressed by omitting the arrow.

```
fn add(int a, int b) -> int {
	return a + b;
}

fn greet(string name) {
	print("hello, {name}");
}
```

There is no separate syntax for methods vs free functions. Methods are functions defined in an `impl` block that receive `self`.

### Variables and Constants

Variables are declared with their type. Constants use `const`. Type inference is available with `var`.

```
int x = 42;
string name = "blang";
const float PI = 3.14159;
var count = 0;          // inferred as int
```

Mutability is the default for non-const variables. This matches C's model and avoids the `mut` annotation that Rust requires on nearly every variable.

### Ownership and Memory

BLang uses three ownership modes. The compiler decides stack vs heap allocation via escape analysis — the programmer declares ownership intent, not storage location.

```
// Value types — stack-allocated, copied on assignment (default for primitives)
int x = 42;
string s = "hello";

// Owned heap objects — single owner, freed when owner goes out of scope
own Buffer buf = Buffer.new(1024);

// Shared reference-counted objects — multiple owners, freed when last ref drops
shared Connection conn = Connection.open("localhost");
```

**No raw pointers.** No `malloc`/`free`. No `new`/`delete`. No garbage collector pauses — deallocation is deterministic.

**Ownership transfer** moves the value:

```
own Buffer a = Buffer.new(256);
own Buffer b = a;    // 'a' is moved into 'b', 'a' is no longer valid
// Using 'a' here is a compile error
```

**Why not Rust's borrow checker?** Lifetime annotations are the hardest part of Rust for both humans and LLMs. BLang uses ownership + ARC, which is simpler and still memory-safe. The tradeoff is a small runtime cost for reference counting on `shared` types, which is acceptable for the vast majority of programs.

### Control Flow

#### If/Else

Braces are always required. No parentheses around the condition.

```
if x > 0 {
	do_positive();
} else if x == 0 {
	do_zero();
} else {
	do_negative();
}
```

#### For Loops

Iteration is always over a range or collection using `for ... in`.

```
for i in 0..10 {
	print("{i}");
}

for item in list {
	process(item);
}

for key, value in map {
	print("{key}: {value}");
}
```

There is no C-style `for(;;)` and no `do-while`. Counted loops use range expressions (`0..10`) instead of manual index manipulation. Infinite loops use `for` with no condition:

```
for {
	if should_stop() {
		break;
	}
}
```

#### While Loops

For condition-based looping, `while` is available. Braces are always required. No parentheses around the condition.

```
while running {
	process_next();
}

while buffer.has_data() {
	byte = buffer.read();
	handle(byte);
}
```

BLang has two loop constructs: `for ... in` for iteration over ranges and collections, and `while` for condition-based loops. There is no C-style `for(;;)` and no `do-while`.

#### Match

Pattern matching replaces `switch`. All cases must be handled.

```
match status {
	ok(value) {
		process(value);
	}
	err(e) {
		log(e.message);
	}
}

match command {
	"start" { engine.start(); }
	"stop"  { engine.stop(); }
	_       { log("unknown command"); }
}
```

No fallthrough. No `break` needed. Each arm is a block.

### Error Handling

Functions that can fail return `Result<T, E>`. There are no exceptions.

```
fn read_file(string path) -> Result<string, IOError> {
	if !file.exists(path) {
		return err(IOError.new("not found: {path}"));
	}
	return ok(file.read_all(path));
}
```

`Result<T, E>` is a built-in enum type with two variants:

- `ok(T)` — the successful value
- `err(E)` — the error value

Callers must handle the result explicitly:

```
// Option 1: match (exhaustive)
match read_file("data.txt") {
	ok(data)  { process(data); }
	err(e)    { log(e.message); }
}

// Option 2: propagate with ?
fn load_config() -> Result<Config, IOError> {
	data = read_file("config.txt")?;   // returns early if err
	return ok(Config.parse(data));
}
```

#### The ? Operator

The `?` operator is the only way to propagate errors without explicitly handling them. When applied to a `Result<T, E>` expression:

- If the result is `ok(value)`, execution continues with `value` unwrapped.
- If the result is `err(e)`, the enclosing function returns `err(e)` immediately.

The enclosing function must return `Result` with a compatible error type. `?` can only appear in postfix position on an expression: `expr?`.

This makes every early-return point visible in the source code. An LLM or code reader can trace exactly which calls might return early by scanning for `?` tokens.

```
fn process_config() -> Result<int, IOError> {
	data = read_file("config.txt")?;    // may return early with err
	parsed = parse_int(data)?;          // may return early with err
	return ok(parsed);
}
```

#### Exhaustive Matching

The compiler enforces that all variants are covered in a `match` expression. For `Result`, both `ok` and `err` arms must be present. For `Option`, both `some` and `none` arms must be present. A wildcard `_` arm satisfies any remaining variants.

A `match` on an enum that omits a variant without providing a wildcard arm is a compile error that names the missing variant(s). Exhaustiveness is checked during code generation (when built with LLVM); `match` on non-enum subjects (e.g. integers) is unaffected.

### Null Safety

There is no `null`. Optional values use `Option<T>`.

`Option<T>` is a built-in enum type with two variants:

- `some(T)` — a present value
- `none` — absence of a value

```
fn find_user(int id) -> Option<User> {
	if id in user_table {
		return some(user_table[id]);
	}
	return none;
}

match find_user(42) {
	some(user) { greet(user); }
	none       { log("user not found"); }
}
```

Both `Result` and `Option` use pattern matching as the primary way to extract their contained values. Destructuring bindings in match arms (`ok(value)`, `some(x)`) introduce new variables into the arm's scope.

## Type System

### Built-in Types

| Type | Description | Size |
|------|-------------|------|
| `int` | Signed integer (platform word size) | 32 or 64 bit |
| `int8`, `int16`, `int32`, `int64` | Fixed-width signed integers | as named |
| `uint8`, `uint16`, `uint32`, `uint64` | Fixed-width unsigned integers | as named |
| `float`, `double` | IEEE 754 floating point | 32, 64 bit |
| `bool` | Boolean | 8 bit |
| `char` | Unicode scalar value | 32 bit |
| `string` | UTF-8 string (owned, growable) | heap |
| `byte` | Raw byte alias for `uint8` | 8 bit |

### Structs

Composite value types. No inheritance.

```
struct Point {
	int x;
	int y;
}

struct Rect {
	Point origin;
	Point size;
}
```

### Protocols

Protocols define shared behavior. Types opt in with `impl`.

```
protocol Printable {
	fn to_string(self) -> string;
}

protocol Comparable {
	fn compare(self, Self other) -> int;
}

impl Printable for Point {
	fn to_string(self) -> string {
		return "({self.x}, {self.y})";
	}
}
```

Protocols replace both C++ virtual classes and Go interfaces. They are checked at compile time. Dynamic dispatch is available when a function accepts a protocol type.

### Generics

Simple type parameters without lifetime annotations.

```
struct List<T> {
	fn add(self, T item);
	fn get(self, int index) -> Option<T>;
	fn length(self) -> int;
}

fn first<T>(List<T> list) -> Option<T> {
	if list.length() > 0 {
		return list.get(0);
	}
	return none;
}
```

Generics can be constrained by protocols:

```
fn sort<T: Comparable>(List<T> list) -> List<T> {
	// T must implement Comparable
}
```

## Concurrency

### Spawn and Task Handles

`spawn` creates a lightweight green thread scheduled by the BLang runtime. It returns a `Task` handle that can be waited on.

```
// Fire-and-forget (Task handle discarded)
spawn {
	result = compute();
	channel.send(result);
};

// Capture the handle to wait on later
Task t = spawn {
	process(data);
};
wait t;    // block until the task completes
```

The runtime uses a thread pool over OS threads. Green threads are cheap (small initial stack, growable).

### Waiting for Tasks

BLang provides two mechanisms for waiting on spawned tasks, both using the `wait` keyword. This is distinct from `await`, which is used for cooperative async/event-loop work.

```
// Wait for a single task
Task t = spawn { compute(); };
wait t;

// Wait for all outstanding spawns
spawn { task_a(); };
spawn { task_b(); };
spawn { task_c(); };
wait_all;    // blocks until all three complete
```

**`wait` vs `await`**: These are intentionally different keywords for different concurrency models:
- **`spawn` + `wait`** — preemptive threads, blocking wait
- **`async` + `await`** — event loop, cooperative yield

`wait_all` does not shut down the thread pool — you can spawn more work after it returns.

When arrays are available, `wait` will also accept `Array<Task>`:

```
Array<Task> tasks = [];
for i in 0..10 {
	tasks.push(spawn { process(i); });
}
wait tasks;    // wait for all tasks in the array
```

### Channels

Typed, thread-safe communication between spawn contexts.

```
chan<int> results;                 // buffered channel of int

spawn {
	results.send(compute());
}

// recv() returns Option<T>: some(value) on success, none when the channel
// is closed and empty. A match must handle both cases (exhaustiveness).
match results.recv() {             // blocks until a value is available
	some(value) {
		use(value);
	}
	none {
		// channel was closed and drained
	}
}

results.close();                   // no further sends allowed
```

### Sync Types

When shared mutable state is needed, `sync` provides automatic locking.

```
sync List<string> log = sync List.new();

spawn {
	log.add("from thread 1");    // auto-locked
}

spawn {
	log.add("from thread 2");    // auto-locked
}
```

### Async / Await

For I/O-bound work, `async` functions run on the built-in event loop (similar to libuv).

```
async fn fetch(string url) -> Result<string, NetError> {
	conn = await net.connect(url)?;
	data = await conn.read_all()?;
	return ok(data);
}

async fn fetch_all(List<string> urls) -> List<string> {
	List<string> results = List.new();
	for url in urls {
		data = await fetch(url)?;
		results.add(data);
	}
	return results;
}
```

`async` functions are not the same as `spawn`. `async` is cooperative (event-loop scheduled, single-threaded per loop) while `spawn` is preemptive (multi-threaded). Both are built into the language — no external runtime library needed.

### Event Handlers

First-class event registration on the event loop.

```
on timer.every(1000) {
	log("tick");
}

on socket.data(conn) |bytes| {
	process(bytes);
}

on signal(SIGINT) {
	shutdown();
}
```

Implemented today: `on EXPR { ... }` registers the body on the global event
loop, keyed by the fd that `EXPR` yields. `import timer;` provides timer
sources — `timer.every(ms)` (repeating) and `timer.after(ms)` (one-shot) —
and `timer.run()`/`timer.stop()` drive and stop the loop. Timer fds and socket
fds share one poll-based loop. Argument bindings (the `|bytes|` form) and
`signal(...)` sources are not yet implemented.

### Thread Safety Rules

The compiler enforces these rules at compile time:

1. **`own` values cannot cross spawn boundaries** — you must send them through a channel (which transfers ownership) or copy them.
2. **`shared` values are read-only** — reference-counted but immutable.
3. **`sync` values are auto-locked** — mutable and thread-safe, with a runtime lock.
4. **Value types are copied** — integers, bools, small structs are safe to use anywhere.

This means data races are compile-time errors, not runtime bugs.

## String Interpolation

Strings support inline expressions with `{}`.

```
string name = "world";
int count = 42;
string msg = "hello {name}, count is {count}";
```

No format specifiers. The compiler calls `to_string()` (via the `Printable` protocol) on each interpolated expression.

## Modules and Imports

Each file is a module. Public symbols are exported with `pub`.

### Import Statement

The `import` statement declares a dependency on another module. The syntax is:

```
import module_name;
import std.io;
```

Every `import` statement must end with a semicolon. Import statements must appear at the top of the file, before any function or type definitions.

The module name corresponds directly to a source file. `import math;` resolves to `math.b` in the project. Dotted paths (`import std.io;`) are supported as a single flat import name — the dot is part of the name, not a hierarchy separator (see Flat Module Namespace below).

```
// main.b
import math;
import std.io;

fn main() {
	result = math.add(1, 2);
}
```

No header files. No forward declarations. No include guards. The compiler resolves dependencies from `import` statements.

### Visibility Modifiers

By default, all top-level symbols (functions, structs, enums, protocols) are **private** to the module that defines them. They cannot be referenced from other modules.

The `pub` keyword makes a symbol visible to any module that imports the defining module:

```
// math.b
pub fn add(int a, int b) -> int {    // exported — visible to importers
	return a + b;
}

fn helper() -> int {                  // private — only usable within math.b
	return 0;
}

pub struct Vector {                   // exported struct
	float x;
	float y;
}

struct Cache {                        // private struct
	int value;
}

pub enum Status {                     // exported enum
	ok,
	err
}
```

The `pub` modifier is placed immediately before the keyword it modifies (`fn`, `struct`, `enum`, `protocol`):

- `pub fn name(...) -> type { ... }` — exported function
- `pub struct Name { ... }` — exported struct
- `pub enum Name { ... }` — exported enum
- `pub protocol Name { ... }` — exported protocol

### Default Private Visibility

Omitting `pub` makes a symbol private. This is the default and does not require any keyword:

```
fn private_helper() -> int {    // private by default
	return 42;
}

pub fn public_api() -> int {    // explicitly public
	return private_helper();
}
```

This differs from languages like Go (uppercase = public) or Java (explicit `public`/`private`). In BLang, privacy is the default and publicity requires an explicit opt-in. This reduces accidental API surface and makes the module's public interface immediately apparent by scanning for `pub`.

### Flat Module Namespace

Modules are one level deep. There are no nested sub-modules and no deeply qualified paths.

```
// Supported
import math;
import http;
import std.io;       // dotted name is still a single flat import

// Not supported — BLang has no nested module hierarchy
import std.net.http.server;
```

When referencing an imported symbol, use the module name as a qualifier:

```
import math;

fn compute() -> int {
	return math.add(1, 2);
}
```

The flat namespace means every import is one qualifier deep. This eliminates long qualification chains and makes it impossible to hallucinate intermediate namespace segments.

### File Equals Module Convention

Every `.b` source file is exactly one module. The module name is the file's base name without the extension:

| File | Module name |
|------|-------------|
| `math.b` | `math` |
| `http.b` | `http` |
| `user_auth.b` | `user_auth` |

There is no separate module declaration keyword. The file itself is the module boundary. This means:

- One file = one module (no splitting a module across files)
- One module = one file (no merging files into one module)
- The file name is the module name (no mismatch between declaration and file)

This eliminates an entire class of build-system complexity: no `mod.rs`, no `__init__.py`, no package declarations that must match directory names.

## Data and Persistence

### Design Philosophy: Language vs Standard Library

Some features need compiler support (new syntax, compile-time checking). Others are purely runtime and belong in the standard library. BLang draws the line as follows:

| Layer | What belongs here | Examples |
|-------|-------------------|----------|
| **Language syntax** | Things the compiler must parse, type-check, or transform | Query expressions, table structs, contracts, test blocks |
| **Standard library** | Runtime implementations that use normal BLang syntax | Database drivers, HTTP servers, serialization codecs |
| **Built-in annotations** | Compiler-recognized metadata that triggers code generation | `@json`, `@grpc`, `@migrate` |

The goal: an LLM writes normal BLang code and gets databases, serialization, and network services without learning framework APIs. The compiler and standard library collaborate behind the scenes.

### Table Types

A `table` struct is a regular struct that the compiler knows maps to a database table. The compiler can type-check queries against it, generate migrations, and prevent schema mismatches at compile time.

```
table struct User {
	int id;
	string name;
	string email;
	bool active;
}

table struct Post {
	int id;
	int user_id;       // foreign key inferred from User.id by naming convention
	string title;
	string body;
}
```

Table structs are value types like any other struct. They can be passed to functions, stored in lists, and serialized. The `table` keyword simply tells the compiler: "this type has a corresponding database table — check queries against its schema."

### Query Expressions

Queries are language-level expressions that the compiler type-checks and translates to SQL (or another backend). They use a pipeline syntax (`|>`) that composes naturally and reads top-to-bottom.

```
fn active_users() -> Result<List<User>, DBError> {
	return query User
		|> where { .active == true }
		|> order_by { .name }
		|> limit(100);
}

fn user_posts(int user_id) -> Result<List<Post>, DBError> {
	return query Post
		|> where { .user_id == user_id }
		|> order_by { .id };
}

fn user_with_posts(int id) -> Result<Option<User>, DBError> {
	return query User
		|> where { .id == id }
		|> join Post on { User.id == Post.user_id }
		|> first;
}
```

Key design decisions:

- **No string SQL** — queries are syntax, not strings. SQL injection is impossible.
- **Compile-time schema checking** — referencing `.nonexistent_field` is a compile error.
- **One way to query** — no ORM method chains vs raw SQL vs query builder debate.
- **Pipeline operator `|>`** — each stage transforms the result. LLMs generate this reliably because each step is independent and composable.

Mutations use the same table types:

```
fn create_user(string name, string email) -> Result<User, DBError> {
	return insert User { name: name, email: email, active: true };
}

fn deactivate_user(int id) -> Result<int, DBError> {
	return update User
		|> where { .id == id }
		|> set { .active = false };
}

fn remove_old_posts(int days) -> Result<int, DBError> {
	return delete Post
		|> where { .created_at < time.days_ago(days) };
}
```

### Automatic Migrations

Schema migrations are derived from the table struct definitions. The compiler tracks the schema history and generates migration steps automatically. There is no manual migration file authoring.

```bash
# Compiler compares current table structs against last known schema
blang migrate --preview     # shows what would change
blang migrate --apply       # applies to the connected database
blang migrate --generate    # emits migration SQL for review/CI
```

When a developer changes a table struct:

```
table struct User {
	int id;
	string name;
	string email;
	bool active;
	string role;         // new field added
	// removed: nothing  // removing a field requires explicit confirmation
}
```

The compiler detects the diff and generates:

```sql
ALTER TABLE user ADD COLUMN role TEXT NOT NULL DEFAULT '';
```

Design principles for migrations:

- **Additive changes are automatic** — adding fields, adding tables, adding indexes.
- **Destructive changes require confirmation** — dropping columns, dropping tables, renaming fields. The compiler flags these and asks for explicit intent (via a `@drop` annotation or CLI confirmation).
- **No migration numbering** — the compiler tracks schema state, not a sequence of numbered files. This eliminates the merge-conflict problem with migration files.
- **LLMs never write migrations** — the LLM modifies the table struct; the compiler handles the rest. This removes an entire category of LLM errors (wrong ALTER syntax, missing rollback, out-of-order migrations).

### Database Configuration

Connection configuration is separate from queries, keeping code portable:

```
// blang.toml (project config, not source code)
[database]
driver = "postgres"
url = "env:DATABASE_URL"
```

The `query`, `insert`, `update`, and `delete` keywords use the project's configured database. No connection objects to pass around. For multiple databases, named connections are explicit:

```
result = query User @db("analytics")
	|> where { .active == true };
```

## Serialization and Wire Protocols

### Built-in Annotations for Code Generation

BLang uses compiler-recognized annotations to generate serialization code. No external code generators, no reflection, no runtime overhead for formats that can be determined at compile time.

```
@json
struct APIResponse {
	int status;
	string message;
	List<User> data;
}

// Automatically gets serializers, dispatched at compile time:
// to_json(value) -> string            // serialize a @json struct
// APIResponse_from_json(string input) -> APIResponse   // parse
```

The builtin `to_json(value)` resolves the argument's struct type at compile
time and serializes it (a compile error if the struct is not `@json`).

### gRPC and Protocol Buffers

For cross-system compatibility, BLang supports gRPC natively through annotations. The compiler generates the serialization and service stubs — no separate `.proto` file or `protoc` step.

```
@grpc
struct UserRequest {
	int id;
}

@grpc
struct UserResponse {
	int id;
	string name;
	string email;
}

@grpc
protocol UserService {
	fn get_user(UserRequest req) -> Result<UserResponse, RPCError>;
	fn list_users(Empty req) -> Result<List<UserResponse>, RPCError>;
}
```

This generates both client stubs and server interfaces. The struct annotation `@grpc` handles protobuf-compatible serialization. The protocol annotation generates the service definition.

Why gRPC as the primary wire protocol:

- **Schema-first** — the struct *is* the schema. No drift between `.proto` files and code.
- **Cross-language** — gRPC clients in Python, Go, Java, etc. can call BLang services and vice versa.
- **LLM-friendly** — the LLM writes normal BLang structs and protocols; the compiler handles the wire format.
- **One right way** — no debate between REST vs gRPC vs GraphQL for service-to-service communication.

### Supported Serialization Formats

Annotations are the mechanism; the standard library provides the implementations:

| Annotation | Format | Use case |
|-----------|--------|----------|
| `@json` | JSON | Human-readable APIs, config files |
| `@grpc` | Protocol Buffers | Service-to-service, cross-language |
| `@msgpack` | MessagePack | Compact binary, same-language |
| `@csv` | CSV | Data import/export |

Multiple annotations can be combined on a single struct. The compiler generates all requested serialization methods.

## Network Services

### The Boundary Question: REST vs GraphQL

REST endpoints and GraphQL schemas are **standard library** concerns, not language syntax. The reasoning: they are presentation-layer patterns that change faster than languages evolve, and they don't require compiler support to be type-safe (BLang's existing type system already catches mismatches).

However, BLang's standard library should make defining services as concise as the language itself. The principle remains: one obvious way, minimal boilerplate, LLM-friendly.

### HTTP Services (Standard Library)

```
import http;

fn main() {
	http.Server server = http.Server.new(8080);

	server.get("/users", fn(http.Request req) -> http.Response {
		users = query User |> where { .active == true }?;
		return http.ok(users);   // auto-serialized via @json
		// implemented form: return net.http_json(to_json(users));
	});

	server.post("/users", fn(http.Request req) -> http.Response {
		input = req.body_as(CreateUserInput)?;
		user = insert User { name: input.name, email: input.email, active: true }?;
		return http.created(user);
	});

	server.listen();
}
```

### GraphQL (Standard Library)

For teams that prefer GraphQL, the standard library provides a schema-from-types approach:

```
import graphql;

@graphql
table struct User {
	int id;
	string name;
	string email;
	List<Post> posts;   // resolved as a relationship
}

fn main() {
	graphql.Server server = graphql.Server.new(8080);
	server.expose(User);   // generates query/mutation resolvers from table struct
	server.listen();
}
```

The `@graphql` annotation generates the GraphQL schema from the struct. Queries, mutations, and field resolvers are derived from the table struct and its query expressions. Custom resolvers override the defaults.

### Why Not Language-Level REST/GraphQL?

- **REST is a pattern**, not a type system concept. URL routing, content negotiation, and middleware are runtime behaviors that don't benefit from compiler analysis.
- **GraphQL is a query language** that could theoretically get language integration (like database queries), but its rapidly evolving spec makes it better suited to library updates than language revisions.
- **gRPC gets deeper integration** because its schema (protobuf) maps cleanly to BLang's type system and its binary wire format benefits from compile-time code generation.

The standard library approach still gives LLMs a single, canonical API to target — they don't need to choose between Express, Axum, Gin, or Flask equivalents.

## Contracts

### Preconditions and Postconditions

Functions can declare contracts that the compiler checks at call sites (when statically provable) or inserts as runtime assertions.

```
fn divide(int a, int b) -> int
	requires b != 0
{
	return a / b;
}

fn binary_search<T: Comparable>(List<T> list, T target) -> Option<int>
	requires list.is_sorted()
{
	// ...
}

fn sqrt(float x) -> float
	requires x >= 0.0
	ensures result >= 0.0
{
	// ...
}
```

Contracts serve three purposes:

1. **Compile-time error detection** — if the compiler can prove a contract violation (e.g., `divide(x, 0)`), it's a compile error.
2. **Runtime safety net** — when static analysis can't prove the contract, a runtime check is inserted. Violation is a panic with a clear message, not undefined behavior.
3. **LLM guidance** — contracts are structured documentation. An LLM reading `requires b != 0` knows to add a zero-check before calling `divide`. This is more reliable than reading prose comments.

Contracts are **not** a full formal verification system. They are lightweight assertions that catch the most common LLM errors: null-like invalid states, off-by-one bounds, and violated preconditions.

## Built-in Testing

### Test Blocks

Tests are part of the language, not a framework. Test blocks can appear in any source file, adjacent to the code they test.

```
fn add(int a, int b) -> int {
	return a + b;
}

test "add returns sum of arguments" {
	assert add(2, 3) == 5;
	assert add(-1, 1) == 0;
	assert add(0, 0) == 0;
}

test "add handles overflow" {
	// result type catches overflow instead of wrapping
	assert add(int.max, 1) == err(OverflowError);
}
```

Design decisions:

- **`test` is a keyword** — no test framework to import, no test runner to configure, no naming conventions to follow.
- **Tests live next to code** — not in a separate `tests/` directory. When an LLM generates a function, it can generate tests in the same file immediately.
- **`assert` is the only assertion** — no `assertEqual`, `assertThrows`, `assertThat`. One way to assert.
- **The compiler is the test runner** — `blang test` discovers and runs all test blocks. No third-party test harness.
- **Test blocks are stripped from release builds** — they have zero runtime cost in production.

### Table-Driven Tests

For parameterized testing, use a `for` loop inside a test block:

```
test "division by zero returns error" {
	for numerator in [0, 1, -1, 100, int.max] {
		assert divide(numerator, 0) == err(DivideByZeroError);
	}
}
```

### Testing Async and Concurrent Code

Test blocks support `async` and `spawn` natively — no special test utilities:

```
test "channel sends and receives" {
	chan int c = chan.new(1);
	spawn {
		c.send(42);
	}
	assert c.recv() == 42;
}
```

## Additional LLM-Optimized Design Rules

These rules complement the syntax design to maximize LLM code generation accuracy, informed by research from MoonBit (ICSE LLM4Code 2024) and the broader LLM-for-code community.

### No Function Overloading

Every function name has exactly one signature. This is a strict rule, not a guideline.

```
// WRONG — BLang does not allow this
fn format(int x) -> string { ... }
fn format(float x) -> string { ... }
fn format(string x) -> string { ... }

// RIGHT — distinct names
fn format_int(int x) -> string { ... }
fn format_float(float x) -> string { ... }
fn format_string(string x) -> string { ... }
```

Why: LLMs frequently hallucinate overloads that don't exist, or call overloaded functions with the wrong argument types. With no overloading, the function name uniquely determines the signature, and the compiler catches every mismatch.

Generics with protocol constraints handle the cases where overloading would be genuinely useful:

```
fn format<T: Printable>(T value) -> string {
	return value.to_string();
}
```

### Mandatory Type Signatures on Public Functions

All `pub` functions must have explicit parameter types and return types. Type inference (`var`) is allowed for local variables but not for module boundaries.

```
// REQUIRED — explicit types on public API
pub fn calculate_tax(float income, float rate) -> float {
	return income * rate;
}

// ALLOWED — inference for locals inside function bodies
fn internal_helper() -> int {
	var x = compute();  // type inferred
	return x + 1;
}
```

This gives LLMs stable anchors: when generating code that calls a module's public API, the LLM can rely on the type signature without needing to trace through implementation details.

### Flat Module Namespace

Modules are one level deep. No nested sub-modules, no deeply qualified paths.

```
// YES
import http;
import db;
import auth;

// NO — BLang does not support this
import std.net.http.server;
import org.example.auth.oauth2.providers;
```

Why: MoonBit's research found that flat structures are "more KV-cache friendly" for LLM inference. Deep namespace paths waste tokens and create opportunities for hallucinated intermediate segments. One level of qualification (`http.Server`) is sufficient and unambiguous.

## Comparison With Other Languages

### BLang vs C

| Aspect | C | BLang |
|--------|---|-------|
| Memory | Manual malloc/free | Ownership + ARC |
| Strings | Null-terminated char arrays | Built-in string type |
| Errors | Return codes, errno | Result type |
| Threading | pthread library | Built-in spawn/chan |
| Safety | None | Memory, null, thread safety |
| Compilation | Fast | Fast (LLVM backend) |

BLang keeps C's compiled-to-native performance and simple mental model while eliminating the unsafe sharp edges.

### BLang vs C++

| Aspect | C++ | BLang |
|--------|-----|-------|
| Complexity | Very high (templates, SFINAE, ADL, etc.) | Minimal |
| Ways to do X | Many (5 init syntaxes, 3 polymorphisms) | One |
| Memory | Manual + RAII + smart pointers | Ownership + ARC |
| Polymorphism | Virtual, templates, CRTP, concepts | Protocols only |
| Error handling | Exceptions + error codes + optional | Result only |
| Build system | CMake/Make/Bazel/etc | TBD — built-in |
| LLM friendliness | Poor | Designed for it |

BLang is what C++ would look like if you removed 40 years of backward compatibility.

### BLang vs Rust

| Aspect | Rust | BLang |
|--------|------|-------|
| Memory safety | Borrow checker + lifetimes | Ownership + ARC |
| Learning curve | Steep (lifetimes, traits, macros) | Low |
| Concurrency | async runtimes (tokio, etc.) | Built-in event loop |
| Error handling | Result + ? | Result + ? (same) |
| Null safety | Option | Option (same) |
| LLM friendliness | Medium (lifetime annotations) | Designed for it |

BLang trades Rust's zero-cost lifetime guarantees for simpler ownership that an LLM can always get right. The small ARC overhead on shared types is worth the dramatic reduction in complexity.

### BLang vs Python

| Aspect | Python | BLang |
|--------|--------|-------|
| Performance | Interpreted, slow | Compiled, native |
| Typing | Dynamic | Static with inference |
| Threading | GIL (no real parallelism) | Real parallelism |
| Memory | GC with pauses | Deterministic (ownership + ARC) |
| Simplicity | Very high | High |
| LLM friendliness | Good (simple syntax) | Better (static types catch errors) |

BLang aims for Python's readability with native performance and real concurrency.

### BLang vs Go

| Aspect | Go | BLang |
|--------|-----|-------|
| Concurrency | Goroutines + channels | spawn + chan (similar) |
| Error handling | `if err != nil` everywhere | Result + ? (less boilerplate) |
| Generics | Limited (added in 1.18) | Full generics with constraints |
| Memory | Garbage collected | Ownership + ARC (no GC pauses) |
| Null safety | nil exists | No null (Option type) |

BLang borrows Go's concurrency model but adds null safety, proper error handling, and deterministic memory.

## LLM Code Generation Considerations

### Why Existing Languages Are Hard for LLMs

- **C++**: Too many ways to express the same thing. Implicit behavior. Template metaprogramming is essentially a second language. LLMs frequently generate code that compiles but has subtle UB.
- **Rust**: Lifetime annotations require global reasoning about data flow. LLMs often generate code that doesn't satisfy the borrow checker.
- **Python**: Dynamic typing means errors surface at runtime, not compile time. An LLM can generate syntactically valid Python that fails on execution.
- **JavaScript**: Loose typing, `this` binding, prototype chains, async callbacks vs promises vs async/await — too many equivalent patterns.

### How BLang Addresses This

1. **Constrained output space**: With one way to write each construct, a random valid BLang program is more likely to be correct than a random valid C++ program.
2. **Static checking catches LLM errors early**: Types, ownership, null safety, and thread safety are all checked at compile time. The compiler acts as a second pass that catches the model's mistakes.
3. **Regular patterns**: Once a model learns `fn name(type arg) -> type { body }`, that pattern never varies. No special cases to memorize.
4. **Explicit error paths**: No hidden exceptions. The `?` operator marks every early-return point. An LLM can trace control flow by reading the code linearly.
5. **No unsafe escape hatch**: Unlike Rust's `unsafe {}`, BLang has no way to bypass safety checks. The model cannot generate memory-unsafe code.

### Token Efficiency

BLang aims for high semantic density per token:

```
// BLang: 8 tokens of meaningful syntax
spawn {
	results.send(compute());
}

// C++ equivalent: ~30+ tokens
std::thread t([&results]() {
	std::lock_guard<std::mutex> lock(mtx);
	results.push_back(compute());
});
t.detach();
```

Fewer tokens means fewer opportunities for the model to make errors.

## File Extension

BLang source files use the `.b` extension.

## Implementation Status

The BLang compiler is under active development. The current implementation is a hand-written recursive-descent parser that builds an AST. LLVM 18+ code generation is wired to the parser via the `CodeGen` class — the full pipeline (parse → LLVM IR → native binary) is tested end-to-end.

For the detailed implementation plan with 217 tasks across all phases, see **[docs/implementation_plan.md](implementation_plan.md)**.

### Currently Working — Parser

All of the following are parsed into AST nodes by the recursive-descent parser (`qcc`):

- Function definitions using `fn` keyword with parameters and return types (`fn add(int a, int b) -> int { }`)
- Extern function declarations with variadic support (`extern fn printf(string fmt, ...) -> int;`)
- Variable declarations (single and multi-variable) with expression initializers
- Constants: `const` declarations, integer, float, string, char, boolean (`true`/`false`) literals
- Binary expressions with full operator precedence: arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`&&`, `||`), bitwise (`&`, `|`, `^`, `<<`, `>>`)
- Unary expressions (`-`, `!`, `~`)
- Assignment operators (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `^=`)
- Control flow: if/else, while, for-in (range, collection, key-value, infinite)
- Function calls with arguments (including nested calls)
- Return statements, break, continue
- Block scoping with `{ }`
- Comments (single-line `//` and multi-line `/* */`)
- Struct definitions with fields and generic parameters
- Enum/sum type definitions with variants and associated types, including generic enums (`Result<T, E>`, `Option<T>`)
- Protocol definitions with generic parameters
- Protocol conformance checking (`impl Protocol for Struct`)
- Generic functions with protocol constraints (`fn sort<T: Comparable>(...)`)
- `impl` blocks for method definitions
- Method calls (`obj.method(args)`) and field access (`obj.field`)
- Array literals (`[1, 2, 3]`) and indexing (`arr[i]`)
- Range expressions (`start..end`)
- String interpolation (`"hello {name}"`)
- `match` expressions with literal patterns, wildcard `_`, and destructuring bindings (`ok(value)`, `some(x)`)
- `?` (try/propagate) operator in postfix position on expressions (`expr?`)
- Import statements (`import std;`, `import std.io;`)
- Visibility modifier (`pub fn`, `pub struct`, `pub enum`)
- Ownership qualifiers (`own`, `shared`, `sync`)
- Spawn blocks (`spawn { }`)
- Channel declarations (`chan int c;`)
- Async functions (`async fn fetch() -> int { }`)
- Await expressions (`await expr`)
- Event handlers (`on expr { }`)
- Contract clauses (`requires expr`, `ensures expr`)
- Test blocks (`test "name" { }`)
- Assert statements (`assert expr;`, `assert expr, "message";`)
- Pipeline operator (`expr |> fn(args)`)
- Annotations (`@name`, `@name("arg")`)
- Table structs (`table struct T { }`)
- Query expressions (`query T |> where { } |> order_by { } |> limit(n)`)
- Insert/update/delete expressions

### Currently Working — Code Generation

LLVM IR code generation (when built with `llvm-18-dev`) covers:

- Function definitions and extern declarations (including variadic)
- Variable declarations with initialization
- Return statements
- If/else, while, for loops
- Function calls
- Binary expressions (arithmetic, comparison, logical, bitwise with correct operator precedence)
- Assignment expressions (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `^=`)
- Constant expressions (int, float, string, char)
- End-to-end compilation: parse → `.ll` → `llc` → native binary

### Next Steps

**Codegen expansion** — Extend LLVM IR generation to cover all parsed features:

1. Struct types (layout, field access, method dispatch)
2. Enum types and match expressions
3. Generic type instantiation (monomorphization)
4. Result/Option types and the `?` operator
5. For-in loops with range and collection iteration
6. Array literals and indexing
7. String interpolation
8. Ownership semantics (move, ARC for `shared`, auto-locking for `sync`)
9. Spawn/channel concurrency (runtime integration)
10. Async/await and event loop (runtime integration)
11. Contract clauses (runtime assertion insertion)
12. Test block discovery and execution
13. Multi-module compilation (linking symbols across files)

**Parser updates** — Align parser with language spec:

14. Remove C-style `for(;;)` loops (use `for..in` and `while` instead)
15. Remove parentheses requirement from `if` and `while` conditions

**Runtime and tooling:**

16. Automatic schema migrations (`bcc migrate`)
17. Serialization code generation (`@json`, `@grpc`, `@msgpack`)
18. gRPC service generation from protocols
19. HTTP and GraphQL standard library
20. No-overloading rule enforcement, mandatory public type signatures
