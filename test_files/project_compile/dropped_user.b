// modules-v2-lsp U1 project_compile ctest, case C7: a clean user file compiled
// alongside a wrong-format .bmod it does not use. The .bmod is DROPPED (never
// recorded), so its import lines must contribute no diagnostics.
fn main() -> int {
    return 0;
}
