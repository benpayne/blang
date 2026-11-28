// modules-v2-lsp U3 sibling-source fixture: main.b calls a function defined in
// the sibling project file helpers.b (a namespaced module in bcc's combine plan).
import helpers;

fn main() -> int {
    int v = helpers.triple(14);
    println("{}", v);
    return 0;
}
