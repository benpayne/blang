extern fn puts(string s) -> int;

fn take_string(own string s) -> int {
    puts(s);
    return 1;
}

fn main() -> int {
    own string greeting = "hello world";
    own string moved = greeting;
    int result = take_string(moved);
    if result != 1 { return 1; }
    puts("Ownership move test passed!");
    return 0;
}
