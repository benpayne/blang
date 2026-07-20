// One compile with BOTH a syntax error (recovered) and a semantic error,
// proving --json is the single sink for both diagnostic control paths (D5).

fn broken( int a -> int {        // syntax error: missing ')'
    return a;
}

struct Point { int x; int y; }

fn bad_field() -> int {
    Point p = Point { x: 1, y: 2 };
    return p.z;                  // semantic error: unknown field 'z'
}
