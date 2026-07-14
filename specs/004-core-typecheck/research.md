# Research: Core Type Checking (U4)

## R1 — Checks live in Sema, per determinable type
Decision: implement in `Sema::visitExpr`/`visitStmt`/`visitFunction`, using the
resolved-type annotations U3 records. Rationale: REQ-005 must hold in all build
modes (decision 1). Only reject when the involved types are determinable and
provably incompatible — avoids false positives on nodes later units type.

## R2 — Return-path checking
Decision: per function with a non-void declared return type, walk the body's
control-flow terminuses; a path that reaches the end of the top block without a
`return` (or a diverging construct) is a missing-return error. `return;` in a
non-void function and `return expr;` in a void function are errors. Bound: a
conservative structural analysis (if both branches return → returns; trailing
statement analysis) that errs toward acceptance to avoid false positives, but
catches the audit_03 shape (empty/valueless return) exactly.

## R3 — Type compatibility (closed conversion set)
Decision: `typesCompatible(from,to)`: equal names → ok; both integer-family →
widening ok; float→double ok; a generic-parameter target → unchecked. Anything
else determinable → incompatible. Rationale: decision 6 (int width promotion is
the only implicit conversion); matches existing codegen widening.

## R4 — Call arity/arg-type
Decision: for a resolved `CallExpression` with a non-generic, non-variadic,
non-builtin callee, require `args.size() == params.size()`; each determinable
arg type compatible with its param type. Variadic → only check the fixed prefix.
Generic/builtin callees → arity/type left to their own paths (unchecked here).

## R5 — Operand validity
Decision: arithmetic (`+ - * / %`) requires numeric operands; compar/equality
require compatible operands; logical (`&& ||`) require bool; bitwise/shift
require integer. Only when operand types are determinable. Float unary minus
accepted (fixes an existing gap). String `+` (concat) remains accepted.

## R6 — Codegen hardening (REQ-012)
Decision: delete return-fabrication (`getNullValue`/`CreateIntToPtr`) and the
dropped-initializer (`initVal = nullptr`) branch; the codegen expression
dispatch fallback becomes `reportICE(...)` (stderr "internal compiler error:
unhandled ... — please report", abort/return error) rather than silent nullptr.
Because sema now rejects the ill-typed inputs, no valid program reaches it.

## R7 — Migration
Decision: run full suites after each phase; fix corpus/stdlib/demos fallout to
be type-correct in-unit (strict decision); record any bulk residue for U8.
