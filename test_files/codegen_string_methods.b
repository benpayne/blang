fn main() -> int {
    string hello = "Hello, World!";

    // Test length property
    int len = hello.length;
    println("length={}", len);
    if len != 13 {
        return 1;
    }

    // Test is_empty property
    string empty = "";
    bool e1 = hello.is_empty;
    bool e2 = empty.is_empty;
    if e1 {
        return 2;
    }
    if !e2 {
        return 3;
    }

    // Test contains
    bool has = hello.contains("World");
    if !has {
        return 4;
    }

    // Test starts_with
    bool sw = hello.starts_with("Hello");
    if !sw {
        return 5;
    }

    // Test ends_with
    bool ew = hello.ends_with("World!");
    if !ew {
        return 6;
    }

    // Test index_of
    int idx = hello.index_of("World");
    println("index_of={}", idx);
    if idx != 7 {
        return 7;
    }

    // Test to_upper
    string upper = hello.to_upper();
    println("upper={}", upper);

    // Test to_lower
    string lower = hello.to_lower();
    println("lower={}", lower);

    // Test trim
    string padded = "  hi  ";
    string trimmed = padded.trim();
    println("trimmed='{}'", trimmed);

    // Test substring
    string sub = hello.substring(0, 5);
    println("sub='{}'", sub);

    // Test replace
    string replaced = hello.replace("World", "BLang");
    println("replaced='{}'", replaced);

    return 0;
}
