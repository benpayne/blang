# Implementation Plan: Core Type Checking (U4)

**Branch**: `epic/blang-ast/u4-core-typecheck` | **Spec**: [spec.md](spec.md)
Covers REQ-005 (type checking), REQ-012 (codegen hardening).

## Summary
Add type-checking rules to the U3 `Sema` pass (all build modes): return-path
correctness, initializer/assignment compatibility, call arity + argument-type
compatibility, binary/unary operand validity — reported through the single
`DiagnosticEngine`. Delete codegen's return-fabrication and dropped-initializer
sites; turn the codegen expression-dispatch silent-`nullptr` into a loud ICE.
Author audit_01..05,10 fixtures. Migrate corpus fallout; keep suites green.

## Constitution Check
| Principle | Assessment | Status |
|-----------|------------|--------|
| I One Right Way | No syntax change; enforcement made real. CLAUDE.md/language_design.md updated where enforcement becomes real. | PASS |
| II Test-Gated | audit_01..05,10 negative fixtures + expected patterns, both build modes; full suites green both modes; reject-only checks satisfied by expected-message negatives (1.1.0). | PASS |
| III Reject don't coerce | Core of the unit: deletes fabrication/drop; adds located rejections; ICE on codegen surprise. | PASS |
| IV Memory/Thread | No runtime/ARC change; test_codegen still run. | PASS |
| V House Style | Sema additions follow house style. | PASS |

## Type compatibility (closed set, decision 6)
`compatible(from, to)` is true iff: names equal; OR both integer types and
`from` fits by width promotion into `to` (int/short/long/byte widening; the
existing documented implicit conversion); OR `to` is a generic parameter of the
enclosing context (leave unchecked — U5). `bool`/`char` treated as their own
types. `double`/`float` widening float→double permitted (existing behavior).
Unknown/undeterminable operand types (sema left nullptr) are NOT rejected —
U4 only rejects when both sides are determinable and provably incompatible, to
avoid false positives on constructs later units type.

## Phasing
1. Type-compat helper + return-path checking (FR-001..003) in Sema.
2. Initializer/assignment compatibility (FR-004) + delete dropped-initializer.
3. Call arity + arg-type (FR-005,006).
4. Operand validity (FR-007).
5. Delete return fabrication; ICE-harden CodeGen dispatch (FR-009).
6. audit_01..05,10 fixtures + .expected (FR-010).
7. Migration sweep + docs; gates.

## Risk
Strict checking breaks corpus/stdlib/demos (design.md's biggest risk). Mitigate
by conservative "reject only when provably incompatible with both types known",
running full suites after each phase, and migrating fallout in-unit.
