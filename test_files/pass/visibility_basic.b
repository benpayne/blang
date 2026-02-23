// Test basic visibility modifiers on functions and structs

pub fn public_add(int a, int b) -> int {
    return a + b;
}

fn private_helper() -> int {
    return 42;
}

pub struct PublicPoint {
    int x;
    int y;
}

struct PrivateData {
    int value;
}
