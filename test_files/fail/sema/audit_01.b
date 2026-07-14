// audit_01 (design.md "The 10 audit programs"): struct return type, int returned.
// Today codegen fabricates a zeroed struct; U4 rejects at the semantic stage.
struct Point { int x; int y; }
fn origin() -> Point { return 5; }
