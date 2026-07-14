# Contract: Resolution Diagnostics

The four resolution error classes this unit enforces, and the shape of their
diagnostics. Enforced by REQ-006 and the `fail/sema/` tests.

## Common shape

Every resolution error is a single located line via the U2 `DiagnosticEngine`:

```
<file>:<line>:<col>: error: <message>
```

- `<line>:<col>` points at the **offending reference** (the variable, the
  callee, the `.field`, or the `.method`).
- `<message>` names the offending symbol/member; for members it names the field
  or method and, where available, the type. Exact wording is finalized against
  real U3 output and locked by each fixture's `.expected` pattern (message-from-
  output, to avoid drift).
- No compiler-internal C++ coordinates in default output (only under
  `--debug-compiler`).

## The four classes

| Class | Trigger | Enforced in | Owner in U3 |
|-------|---------|-------------|-------------|
| Undefined variable | reference to a name never declared in scope | all build modes (parse stage) | parser (existing), routed via engine |
| Undefined function | call to an undeclared/unresolved function | all build modes (parse stage) | parser (existing), routed via engine |
| Unknown field | `value.field` where `field` ∉ `value`'s struct type | all build modes incl. `--parse-only`/non-LLVM | **sema (new)** |
| Unknown method | `value.method(...)` not defined on `value`'s type and not a builtin | all build modes incl. `--parse-only`/non-LLVM | **sema (new)** |

## Must NOT

- MUST NOT resolve any reference to a silent `nullptr` that is dropped or
  fabricated (the current `CGStruct.cpp:968,994` behavior is removed on the
  touched paths).
- MUST NOT false-positive on recognized builtin members (`string`/`Array`/
  `Buffer`), generic-parameter/monomorphized bases, or `--combine`/`.bmod`
  symbols.
- MUST NOT double-report a var/func error already thrown by the parser.
- MUST NOT add any type-*checking* diagnostic (arity, compatibility, coercion,
  operand validity, return, match, generic-constraint, ownership, concurrency)
  — those are U4–U7.

## Verification hooks

- SC-001: each class rejected with a canonical located line in `--parse-only`
  and non-LLVM builds; unknown field/method (exit 0 today) now exit non-zero.
- SC-003: one `fail/sema/` fixture per class passes the harness.
