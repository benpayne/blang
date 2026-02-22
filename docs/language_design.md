# BLang Language Design Specification

## Vision

BLang is a compiled, native-performance language designed for **clarity, safety, and LLM code generation**. It takes the best ideas from C, Rust, Swift, Go, and Python while enforcing a single canonical way to express each concept. The language should be simple enough that an AI model can reliably generate correct programs, yet powerful enough for systems-level work including threading, networking, and hardware synthesis.

## Design Principles

### 1. One Right Way

Every concept has exactly one syntax. There are no equivalent alternatives.

- One function declaration syntax (`fn`)
- One iteration construct (`for ... in`)
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

One loop construct. Iteration is always over a range or collection.

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

There is no C-style `for(;;)`, no `while`, no `do-while`. Infinite loops use `for` with no condition:

```
for {
	if should_stop() {
		break;
	}
}
```

This eliminates four loop syntaxes (C `for`, `while`, `do-while`, range-for) in favor of one.

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

Callers must handle the result explicitly:

```
// Option 1: match
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

The `?` operator is the only way to propagate errors. This makes error paths visible in the code — an LLM can trace exactly where a function might return early.

### Null Safety

There is no `null`. Optional values use `Option<T>`.

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

### Spawn

`spawn` creates a lightweight green thread scheduled by the BLang runtime.

```
spawn {
	result = compute();
	channel.send(result);
}
```

The runtime uses a work-stealing scheduler over OS threads. Green threads are cheap (small initial stack, growable).

### Channels

Typed, thread-safe communication between spawn contexts.

```
chan int results = chan.new(10);   // buffered, capacity 10

spawn {
	results.send(compute());
}

value = results.recv();           // blocks until value available
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

```
// math.bl
pub fn add(int a, int b) -> int {
	return a + b;
}

fn helper() -> int {    // private to this module
	return 0;
}
```

```
// main.bl
import math;

fn main() {
	result = math.add(1, 2);
}
```

No header files. No forward declarations. No include guards. The compiler resolves dependencies from `import` statements.

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

BLang source files use the `.bl` extension.

## Implementation Status

The BLang compiler is under active development. The current implementation is a hand-written recursive-descent parser that builds an AST. LLVM 18+ code generation infrastructure exists but is not yet connected to the active parser.

### Currently Working

- Function definitions with parameters and return types
- Variable declarations (single and multi-variable)
- Control flow: if/else, while, for
- Constants: integer, float, string, char literals
- Function calls with arguments
- Return statements
- Block scoping
- Comments (single-line and multi-line)

### Next Steps

1. Binary expressions and assignment operators
2. Transition from C-style syntax (`int foo()`) to BLang syntax (`fn foo() -> int`)
3. Struct types and impl blocks
4. Protocol definitions and conformance checking
5. Generics with protocol constraints
6. spawn/chan concurrency primitives
7. async/await and event loop integration
8. Wire AST to LLVM code generation (LLVM 18+ backend already exists)
9. Result/Option types and the `?` operator
10. Module system and imports
