// audit_03: valueless return in a non-void function. Today reaches LLVM and crashes.
fn f() -> int { return; }
