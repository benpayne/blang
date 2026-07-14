# Data Model: Semantic Pass Skeleton (U3)

This unit adds one in-memory pass, one annotation slot on the AST, and one
test-fixture category. No persistent storage, no schema.

## Entities

### Sema (pass)

Owned by the driver; run once per parsed module, in all build modes.

Entry point (shape; exact signature fixed in the plan/impl):
- `bool Sema::analyze(Module *module, Scope *scope, DiagnosticEngine &diag)` —
  walks the module's functions → blocks → statements → expressions; resolves
  member references; annotates expression types; reports located errors through
  `diag`. Returns `true` iff no error was reported (driver skips codegen and
  exits non-zero on `false`).

State (per run): the module, the resolving `Scope` (the same combined/`.bmod`
scope the parser used), and the `DiagnosticEngine`. No LLVM state; no new
symbol table (resolves against the existing `Scope`/`Symbol` model).

Behavior:
- **Resolves** `FieldAccessExpression` and `MethodCallExpression` against the
  base expression's resolved struct type; unknown field/method → one located
  error naming the member (and type where available).
- **Annotates** each determinable expression with its resolved `Type`
  (see below).
- **Recognizes** builtin members (`string`/`Array`/`Buffer`), generic-parameter
  and monomorphized bases, and `--combine`/`.bmod` symbols — no false "unknown"
  errors (R5).
- **Does not** re-report the parser's var/func resolution errors (R6), and does
  **not** add any type-checking rule (U4+).

### Resolved-type annotation (on `Expression`)

```
class Expression : public Statement {
    // NEW:
    void setResolvedType( Type *t );
    Type *getResolvedType() const;   // nullptr = not yet determined
  private:
    SmartPtr<Type> mResolvedType;    // NEW slot on the base
};
```

- Filled by sema for: constants (literal type), variable refs (the
  `VariableDefinition`'s type), field access (field's declared type), function
  and method call results (return type), index results (element type), operator
  results (operand/result type).
- `nullptr` is a valid state for expression kinds whose type depends on a check
  owned by a later unit; sema leaves those for U4+ rather than fabricating.
- The **single** shared type representation: codegen reads this on the member
  paths it migrates (FR-011); the identity/naming is the existing `Type`
  (FR-012), so sema and codegen agree.

### `fail/sema/` test category

New directory `test_files/fail/sema/`. Each fixture is a `.b` program that must
be **rejected at the semantic stage** with a located diagnostic, plus a
companion `<test>.b.expected` (or inline `// EXPECT-ERROR:`) pattern.

Minimum set (one per resolution class, R9):

| Fixture | Program essence | Expected diagnostic category |
|---------|-----------------|------------------------------|
| `undefined_variable.b` | read a never-declared variable | undefined variable, named |
| `undefined_function.b` | call an undeclared function | undefined/unresolved function, named |
| `unknown_field.b` | `value.field` not on the struct type | unknown field, named (+ type) |
| `unknown_method.b` | `value.method()` not on the type | unknown method, named |

Judged by the U2 harness **plus** the `fail/sema/` canonical-format rule (R8):
PASS iff exit ≠ 0 AND stderr matches `^[^:]+\.b:[0-9]+:[0-9]+: error: ` AND
matches the test's own pattern.

## Relationships

- Parser → builds AST + `Scope`/`Symbol` (unchanged) → **Sema** resolves/annotates
  → CodeGen consumes the annotation (member paths). One direction; sema mutates
  only the resolved-type slots and reports diagnostics.
- Sema → `DiagnosticEngine` (U2), the single reporting path.
- `run_tests.sh` → reads `fail/sema/` fixtures + patterns and matches against
  sema's stderr; the canonical regex is the bridge (sema produces it, harness
  asserts it).

## Non-entities (explicitly not modeled in U3)

- Any type-checking result (compatibility, arity, coercion) — U4.
- Match/generic-constraint analysis — U5; ownership/move — U6; concurrency — U7.
- A new symbol table — sema reuses the parser's `Scope`/`Symbol` model.
- The numbered `audit_NN.b` programs — authored by U4/U5/U7, completed in U8.
