# Research: Semantic Pass Skeleton (U3)

Phase 0 decisions. Each entry: **Decision**, **Rationale**, **Alternatives
considered**. These resolve the design choices the plan depends on; none
introduces an [NEEDS CLARIFICATION].

## R1 — Where the sema pass is invoked

**Decision**: The driver calls `Sema::analyze(module, fileScope, *gDiag)` in
`qcc.cpp` immediately after each `Module::Parse` returns a non-null module, and
**before** the `#ifdef BLANG_HAS_LLVM` codegen block. On any sema-reported
error the driver returns non-zero and does not enter codegen for that module.

**Rationale**: Matches the target architecture (`design.md`:
Parser → Sema → CodeGen) and REQ-004 (runs in all build modes, including
`--parse-only` and non-LLVM). Placing it in the existing per-module loop reuses
the already-resolved `fileScope`/combine scope and the installed `gDiag`.

**Alternatives**: (a) invoke inside `Module::Parse` — rejected: entangles sema
with parsing, violates "separate pass". (b) invoke only under
`BLANG_HAS_LLVM` — rejected: breaks REQ-004 (parse-only must catch semantics).

## R2 — Scope of resolution moved to sema in U3 (bounded)

**Decision**: In U3, sema owns **member** resolution (struct fields, methods)
and **type annotation** of expressions. The parser retains its pre-existing
eager resolution of **variables** and **functions**
(`QExpression.cpp` `VariableExpression::Parse`, call resolution), which already
throws located `CompileError`s rendered by the U2 engine. Sema does not
re-report those.

**Rationale**: Member resolution is the actual soundness gap (codegen-only,
silent `nullptr`, not enforced in parse-only). Variable/function resolution is
already located and already runs in all build modes. Ripping eager var/func
resolution out of the parser (which uses resolved symbols to build typed
`VariableExpression`s and make parse decisions) is a large, risky refactor with
no soundness payoff in this unit. Design decision 1 ("sema is the semantic
authority") is satisfied for the checks this epic *adds*; full consolidation is
deferred and non-blocking.

**Alternatives**: (a) move all resolution to sema now — rejected: large blast
radius, high regression risk, no soundness gain for U3's REQ-006 target beyond
members. (b) leave member resolution in codegen with a report — rejected:
fails REQ-004 (must work in parse-only / non-LLVM).

## R3 — Typed-AST annotation representation

**Decision**: Add a `SmartPtr<Type> mResolvedType` slot to the `Expression`
base class (`Expression.h`) with `setResolvedType()` / `getResolvedType()`.
Sema fills it for expressions whose type is determinable from resolution:
constants, variable refs (from the `VariableDefinition`), field access (field's
declared type), call / method-call results (function/method return type), index
results (element type), and operator results (operand type). Unfilled slots
(nullptr) are permitted for expression kinds whose type depends on a check
owned by a later unit.

**Rationale**: The `Expression` base currently has **no** type field (verified:
`Expression.h:19-29` is empty besides `Parse`), so a slot must be added. Storing
the resolved `Type` on the node is design decision 3 (one authoritative type
representation) and gives U4+ somewhere to read from. Using the existing
`SmartPtr<Type>` keeps identity/naming consistent with codegen (FR-012).

**Alternatives**: (a) a side table `map<Expression*,Type*>` — rejected: extra
indirection, lifetime hazards, diverges from "on the node". (b) reuse a
per-subclass `mType` — rejected: not all expression subclasses have one; a base
slot is uniform.

## R4 — Resolving a member requires the base expression's type

**Decision**: Sema walks expressions bottom-up. For
`FieldAccessExpression`/`MethodCallExpression` it first resolves and types the
base (`mObject`), obtains its (struct) `Type`, looks up the named field/method
on that struct's definition (via the `Scope`/`StructDefinition` model), reports
a located error if absent, and annotates the access node with the member's
type. An unresolved base yields exactly one error at the base (no cascade).

**Rationale**: Member existence is only decidable once the base type is known;
this is the same information codegen uses today (`CGStruct.cpp`
`genFieldAccess`/`genMethodCall`), lifted into sema and made build-mode
independent.

**Alternatives**: top-down resolution — rejected: member lookup needs the base
type first.

## R5 — Builtins, generics, `.bmod`/combine must not false-positive

**Decision**: Sema recognizes the same non-struct member surfaces codegen
already accepts: builtin methods/properties on `string` (`.length`,
`.to_upper()`, …), `Array` (`.length`, `.push()`, …), and `Buffer`; generic
type parameters and monomorphized references; and symbols provided via
`--combine` namespaces or `.bmod` flat-merge. These resolve without error.
Where the base type is a generic parameter `T` or otherwise not a concrete
struct in scope, sema does **not** invent an "unknown member" error (member
checking on unresolved-generic bases is left as-is / to U5).

**Rationale**: FR-008 forbids false positives; the existing corpus, stdlib, and
demos exercise all of these. Reusing codegen's acceptance criteria keeps the
accepted set stable (FR-017).

**Alternatives**: strict struct-only resolution — rejected: would reject valid
builtin/generic/stdlib code and break the suites.

## R6 — Single diagnostic, no double report

**Decision**: All sema diagnostics go through `gDiag` (the U2
`DiagnosticEngine`). Because the parser throws var/func errors *before* sema
runs (and the module is discarded on parse failure), sema never sees a program
with an unresolved var/func and cannot double-report. Sema emits at most one
error per offending member reference.

**Rationale**: FR-003, FR-009. One reporting path (design decision 2); parse
failure short-circuits before sema.

**Alternatives**: sema re-resolving vars/funcs — rejected: risk of duplicate or
divergent messages.

## R7 — Codegen consumes the annotation on touched paths only

**Decision**: `genFieldAccess`/`genMethodCall` read the sema-resolved member
and the annotated type instead of re-deriving them on the paths this unit
touches. The former silent-`nullptr` fallbacks on those paths become
"can't happen after sema"; U3 does not convert *every* codegen fallback to a
loud ICE (that hardening is U4/REQ-012) — it only removes re-derivation where it
now reads from sema.

**Rationale**: Design decision 3 (codegen reads types from the AST) applied
incrementally, bounded to U3's member paths to limit blast radius. Full ICE
hardening is explicitly U4.

**Alternatives**: rewire all of codegen's type derivation now — rejected: out of
scope, high risk; migrate per-area as later units touch them (design risk note).

## R8 — `fail/sema/` category and harness enforcement

**Decision**: New `test_files/fail/sema/` directory. `run_tests.sh`'s existing
recursive `find "$SCRIPT_DIR/test_files/fail" -name '*.b'` already discovers
files there and judges them via the U2 negative-test path (`--parse-only`,
expected-error). U3 adds, **for the `fail/sema/` category specifically**, an
assertion that stderr matches the canonical regex
`^[^:]+\.b:[0-9]+:[0-9]+: error: ` for every file (in addition to any per-test
message pattern) — implementing the per-file check the epic done-condition #3
requires. Each fixture also declares a message-specific `.expected` pattern.

**Rationale**: Reuses the U2 harness; puts the done-condition #3 per-file format
gate in place now so U4/U5/U7 just drop in `audit_NN.b`. Detecting the category
by path (`/fail/sema/`) is deterministic and localized.

**Alternatives**: (a) rely only on per-test patterns embedding the prefix —
rejected: not enforced for files that lack a pattern; done-condition #3 wants it
for *every* fail/sema file. (b) put the always-canonical rule on all `fail/` —
rejected: out of scope, would change U2's harness semantics for existing tests.

## R9 — Fixture set (one per resolution class)

**Decision**: Four fixtures minimum: `undefined_variable.b`,
`undefined_function.b`, `unknown_field.b`, `unknown_method.b`, each with a
companion `.expected` combining the canonical regex and the specific message
(patterns derived from the **real U3 output**, captured during implementation,
to avoid drift). More may be added but the numbered `audit_NN.b` programs are
**not** created here (U4/U5/U7).

**Rationale**: FR-013 (one per class); done-condition #3 groundwork without
pulling later units' work forward; message-from-real-output avoids the pattern
drift U2 warned about.

**Alternatives**: create audit_10 (unknown field) now — rejected: design.md
assigns audit_10 to U4; U4's done-when explicitly authors it.
