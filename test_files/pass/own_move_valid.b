fn consume(own string s) -> int {
    return 0;
}

fn main() -> int {
    own string a = "hello";
    own string b = a;
    int result = consume(b);
    return 0;
}
