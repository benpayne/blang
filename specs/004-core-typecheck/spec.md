# Feature Specification: Core Type Checking, Silent Coercions Removed (U4)

**Feature Branch**: `epic/blang-ast/u4-core-typecheck`
**Spec Directory**: `specs/004-core-typecheck`
**Epic**: blang-ast — Unit U4, covers **REQ-005** and **REQ-012**.

## Context

U3 introduced the `Sema` pass (`Sema.h/.cpp`, `QLang` namespace), run between
`Module::Parse` and codegen in ALL build modes, which resolves member references
and annotates every determinable expression with a resolved `Type`. U4 adds the
first real **type-checking** rules to that pass and deletes the silent-coercion
sites in codegen that fabricated or dropped values.

Current gaps (verified against master):
- Return mismatch is fabricated: `CGStatements.cpp` uses
  `llvm::Constant::getNullValue`/`CreateIntToPtr` to coerce a returned value to
  the declared return type (audit_01, audit_02), and `return;` in a non-void
  function reaches codegen (audit_03).
- A bad initializer is silently dropped: `CGStatements.cpp` sets
  `initVal = nullptr` and skips the store, leaving the variable uninitialized
  (audit_04).
- Call arity/argument types are unchecked (audit_05).
- Unhandled AST node / ill-typed input reaching codegen returns a silent
  `nullptr` (`CodeGen.cpp` expression dispatch) — REQ-012.

The only implicit conversion permitted is integer width promotion (design
decision 6). Everything else is a located error.

## User Scenarios & Testing

### User Story 1 — Type errors are rejected with a located diagnostic in all build modes (P1)
As a BLang author, when I return the wrong type, initialize a variable from an
incompatible value, call a function with the wrong number/type of arguments, or
apply an operator to invalid operands, the compiler rejects the program with one
`<file>:<line>:<col>: error: <message>` line, in the LLVM build and the
non-LLVM/`--parse-only` build alike — never fabricating a value or dropping code.

**Acceptance Scenarios**:
1. `fn origin() -> Point { return 5; }` → return type mismatch (audit_01).
2. `fn s() -> string { return 42; }` → return type mismatch (audit_02).
3. `fn f() -> int { return; }` → return missing a value in non-void function (audit_03).
4. `int x = "hello";` → incompatible initializer type (audit_04).
5. `fn f(int a) -> int { ... }` called `f(1,2,3)` → wrong number of arguments (audit_05).
6. A function whose declared return type is non-void with a path that falls off
   the end → missing return on a path.
7. Valid programs (including documented int width promotion) still compile.

### User Story 2 — Codegen trusts the typed AST loudly (P1)
As a compiler maintainer, after sema accepts a module, codegen never silently
fabricates or drops: the return-fabrication and dropped-initializer sites are
deleted, and an unhandled node type / ill-typed input reaching codegen is a loud
internal compiler error (ICE) with a report request, not a silent `nullptr`.

**Acceptance Scenarios**:
1. `grep -c "CreateIntToPtr" CGStatements.cpp` == 0; the `initVal = nullptr`
   dropped-initializer fallback no longer exists in `CGStatements.cpp`.
2. Reaching the codegen expression-dispatch fallback aborts with an ICE message,
   not a silent skip (no user program can reach it after sema).

### User Story 3 — Audit programs pinned as fail/sema fixtures (P1)
`audit_01.b`..`audit_05.b` and `audit_10.b` exist under `test_files/fail/sema/`
with companion `.expected` patterns, judged by the U2/U3 harness (canonical
regex + message) in both build modes.

## Requirements

### Functional Requirements
- **FR-001**: Sema MUST reject a `return <expr>;` whose expression type is not
  compatible with the function's declared return type (compatible = same type,
  or integer width promotion), with a located error. No codegen fabrication.
- **FR-002**: Sema MUST reject `return;` (no value) in a non-void function, and
  `return <expr>;` in a void function, with a located error.
- **FR-003**: Sema MUST reject a non-void function that can fall off its end
  without returning (missing return on a path), with a located error.
- **FR-004**: Sema MUST reject a variable initializer whose type is incompatible
  with the declared variable type, with a located error. The dropped-initializer
  codegen path MUST be removed.
- **FR-005**: Sema MUST reject a call with the wrong number of arguments
  (respecting variadic and default-free signatures) with a located error.
- **FR-006**: Sema MUST reject a call argument whose type is incompatible with
  the corresponding parameter type (compatible = same, or int width promotion),
  with a located error.
- **FR-007**: Sema MUST reject binary/unary operations with invalid operand
  types (e.g. arithmetic on non-numeric, logical on non-bool) with a located
  error. Float unary minus MUST be accepted.
- **FR-008**: All diagnostics go through the single `DiagnosticEngine`, canonical
  format, in all build modes; no compiler-internal coordinates in default output.
- **FR-009 (REQ-012)**: The codegen return-fabrication (`getNullValue`/
  `CreateIntToPtr`) and dropped-initializer (`initVal = nullptr`) sites MUST be
  deleted; the codegen expression-dispatch silent-`nullptr` fallback MUST become
  a loud ICE.
- **FR-010**: `test_files/fail/sema/audit_{01,02,03,04,05,10}.b` MUST exist, each
  rejected in `--parse-only` with a located error matching a companion
  `.expected` pattern (canonical regex + message).
- **FR-011**: Scope discipline — NO match/generics (U5), ownership (U6),
  concurrency (U7) checks. Only the type-checking rules above.
- **FR-012**: Strict migration — any corpus file (`test_files/`, `stdlib/*.b`,
  `demos/*.b`) the new checks reject MUST be fixed within U4 (if small) or
  recorded for U8; `./run_tests.sh` and `./test_codegen.sh` green in BOTH build
  modes at the unit boundary.

## Success Criteria
- **SC-001**: audit_01..05 and audit_10 each exit non-zero under
  `build/qcc --parse-only` and the non-LLVM build, printing one canonical
  located error line matching the fixture's `.expected`.
- **SC-002**: `grep -c "CreateIntToPtr" CGStatements.cpp` returns 0; the
  `initVal = nullptr` dropped-initializer fallback is gone.
- **SC-003**: Gate A (`./run_tests.sh && ./test_codegen.sh`) and Gate B
  (`BUILD_DIR=build-parse ./run_tests.sh`) both exit 0.
- **SC-004**: Gate D quiet compile still holds; U1 goldens still clean.
- **SC-005**: No valid program spuriously rejected (int width promotion and the
  existing pass/codegen corpus, after migration, stay green).

## Out of Scope
- Match exhaustiveness / generic constraints (U5); ownership/move (U6);
  concurrency (U7); multi-error recovery; the legacy Bison/Flex path.
