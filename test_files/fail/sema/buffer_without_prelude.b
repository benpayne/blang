// modules-v2-graph U3 (D14): Buffer is a PRELUDE type resolved from its parsed
// definition (stdlib/buffer.b), NOT a bare-name registration. Compiled standalone
// (no prelude loaded), `Buffer` must fail AT THE TYPE with a located error — the
// D14 win: an undefined type name fails at the type, not later at a symbol lookup.
fn main() -> int {
    Buffer b = Buffer(16);
    return 0;
}
