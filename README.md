# BLang

A compiled, native-performance language designed for clarity, safety, and LLM code generation.

BLang compiles to native code via LLVM, offering the performance of C with the safety guarantees of Rust and the readability of Python. The language enforces a single canonical way to express each concept, making it predictable for both humans and AI code generators.

## Quick Example

```
fn factorial(int n) -> int {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

fn main() -> int {
    int result = 10 |> factorial;
    return result;
}
```

## Design Principles

- **One right way** -- every concept has exactly one syntax, no equivalent alternatives
- **Explicit over implicit** -- no hidden conversions, no invisible control flow
- **Safe by default** -- no null, no data races, no use-after-free at compile time
- **Native performance** -- compiles to native code via LLVM, minimal runtime
- **LLM-optimized** -- regular grammar, small keyword set, constrained output space

## Language Features

**Types and functions**
```
fn add(int a, int b) -> int {
    return a + b;
}

struct Point {
    float x;
    float y;
}

enum Result<T> {
    ok(T),
    none
}
```

**Pattern matching**
```
fn handle(int status) -> int {
    match status {
        ok(value) {
            return value;
        }
        err(e) {
            return 0;
        }
    }
}
```

**Pipeline operator**
```
fn double(int x) -> int { return x * 2; }
fn add_one(int x) -> int { return x + 1; }

fn main() -> int {
    return 5 |> double |> add_one;  // add_one(double(5)) = 11
}
```

**Concurrency**
```
fn main() {
    spawn {
        // runs in a green thread
        int result = compute();
    }
}
```

**Built-in database queries**
```
@json
table struct User {
    int id;
    string name;
    bool active;
}

fn get_active_users() {
    query User
        |> where { .active == true }
        |> order_by { .name }
        |> limit(100);
}
```

**Ownership and safety**
```
fn process() {
    own string data = "hello";      // exclusive ownership
    shared string config = "cfg";   // reference-counted
    sync int counter = 0;           // thread-safe
}
```

For the full language specification, see [docs/language_design.md](docs/language_design.md).

## Building

Requires CMake 3.16+ and a C++17 compiler.

```bash
mkdir build && cd build
cmake ..
make
```

To enable LLVM code generation (requires `llvm-18-dev`):

```bash
cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm
make
```

## Usage

```bash
# Compile a BLang source file to an executable
bcc hello.b -o hello
./hello

# Emit LLVM IR only
bcc -S hello.b

# Compile to object file only
bcc -c hello.b

# Run tests
bcc test

# Schema migrations
bcc migrate --preview
bcc migrate --apply
```

## Testing

```bash
# Run all 124 parser tests
./run_tests.sh

# Build first, then run tests
./run_tests.sh --build
```

## Project Status

BLang is under active development. The compiler includes:

- **Parser**: Full recursive-descent parser covering functions, structs, enums, protocols, generics, pattern matching, ownership qualifiers, concurrency primitives, async/await, contracts, test blocks, pipeline operator, annotations, table structs, and query expressions
- **Code generation**: LLVM 18+ backend for functions, control flow, expressions, and assignments
- **Runtime**: C libraries for reference counting, thread pools, channels, JSON serialization, and database access (SQLite)
- **Tooling**: `bcc` compiler driver, `bcc test` runner, `bcc migrate` schema migration

See [docs/implementation_plan.md](docs/implementation_plan.md) for the roadmap.

## License

This project is licensed under the GNU General Public License v3.0 -- see [LICENSE](LICENSE) for details.
