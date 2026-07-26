// Three independent top-level syntax errors in one compile.
// Panic-mode recovery must report all three, not just the first.

fn first( int a, int b -> int {      // error 1: missing ')' before '->'
    return a + b;
}

fn second( int x ) -> int {
    return x * ;                     // error 2: missing operand after '*'
}

fn third( int y ) -> int {
    y = ;                            // error 3: missing RHS in assignment
    return y;
}
