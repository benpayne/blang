fn main() -> int {
    own string data = "secret";
    spawn {
        own string local = data;
    }
    return 0;
}
