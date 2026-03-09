struct Counter {
    int value;
}

fn apply_twice(fn() f) {
    f();
    f();
}

fn main() -> int {
    // shared qualifier ensures ARC manages the struct
    shared Counter c = Counter { value: 0 };

    // Lambda captures c by value — but for shared (ptr-typed) variables,
    // this copies the pointer, so mutations through the pointer are visible
    apply_twice(fn() {
        c.value = c.value + 1;
    });

    if c.value != 2 { return 1; }

    println("Shared capture test passed!");
    return 0;
}
