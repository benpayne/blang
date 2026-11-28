// modules-v2-lsp U1 project_compile ctest, case C1 (diagnostic parity).
// An unused stdlib import (warning), an unknown module (error), and a Sema
// type error — rendered by ProjectCompile::run must equal qcc's stderr.
import net;
import nonesuch;

fn main() -> int {
    int x = "s";
    return 0;
}
