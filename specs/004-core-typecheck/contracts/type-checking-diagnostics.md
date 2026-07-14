# Contract: Type-Checking Diagnostics (U4)
Each new error is a single located line via DiagnosticEngine:
`<file>:<line>:<col>: error: <message>`. Classes: return type mismatch;
`return;` in non-void / value in void; missing return on a path; incompatible
initializer; wrong number of arguments; incompatible argument type; invalid
operands to operator. Only raised when involved types are determinable and
provably incompatible (closed conversion set = int width promotion + float→double).
MUST NOT double-report U3 resolution errors; MUST NOT add U5/U6/U7 checks.
