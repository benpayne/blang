struct Counter {
    int value;
}

fn apply_twice(fn() f) {
    f();
    f();
}

fn main() -> int {
    // sync qualifier: mutable shared state (shared is immutable through fields; U7)
    sync Counter c = Counter { value: 0 };

    // Lambda captures c by value — but for shared (ptr-typed) variables,
    // this copies the pointer, so mutations through the pointer are visible
    apply_twice(fn() {
        c.value = c.value + 1;
    });

    if c.value != 2 { return 1; }

    println("Shared capture test passed!");
    return 0;
}
