// modules-v2-lsp U1 project_compile ctest, cases C2/C3: prelude types with
// zero imports (Map/Set resolve from the prelude scope).
fn main() -> int {
    Map<string, int> m = Map<string, int>();
    Set<int> s = Set<int>();
    return m.length() + s.length();
}
