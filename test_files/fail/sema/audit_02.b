// audit_02: string return type, int returned. Today codegen emits `inttoptr 42`.
fn s() -> string { return 42; }
