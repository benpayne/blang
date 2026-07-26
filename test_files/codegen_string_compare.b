// U5: lexicographic string relational operators (<, >, <=, >=) via
// __blang_string_compare — previously these compared string POINTERS (garbage
// ordering). Deterministic -> golden.

fn main() -> int {
    println("apple < banana: {}", "apple" < "banana");
    println("banana < apple: {}", "banana" < "apple");
    println("cherry < apple: {}", "cherry" < "apple");
    println("apple > banana: {}", "apple" > "banana");
    println("apple <= apple: {}", "apple" <= "apple");
    println("apple >= apple: {}", "apple" >= "apple");
    println("apple < apple: {}", "apple" < "apple");
    println("ab < abc: {}", "ab" < "abc");
    println("abc < ab: {}", "abc" < "ab");
    println("empty < a: {}", "" < "a");
    return 0;
}
