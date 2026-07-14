# Feature Specification: Semantic Pass Skeleton — Symbols, Types, Resolution (U3)

**Feature Branch**: `epic/blang-ast/u3-sema-skeleton`

**Spec Directory**: `specs/003-sema-skeleton`

**Created**: 2026-07-13

**Status**: Draft

**Epic**: blang-ast (`docs/epics/blang-ast/`) — Unit U3, covers **REQ-004** and
**REQ-006**.

**Input**: User description: "Semantic pass skeleton: symbols, types,
resolution. Introduce a new Sema module invoked between `Module::Parse` and
`CodeGen`, compiled in ALL build modes (no `BLANG_HAS_LLVM` guard) so
`--parse-only` becomes 'parse + sema'. Sema resolves every variable, function,
field, and method reference (located errors via the U2 DiagnosticEngine for
anything unknown — nothing resolves to a silent nullptr) and records the
resolved type of every expression on the AST (typed AST) as the single type
representation the checker and codegen share. Introduce the
`test_files/fail/sema/` negative-test category with a resolution test per error
class and wire `run_tests.sh` to assert the canonical located-error format for
it in both build modes. Does NOT create the numbered `audit_NN.b` programs
(U4/U5/U7). Only name/member resolution and expression-type annotation in this
unit; core type checking is U4, match/generics U5, ownership U6, concurrency
U7."

## Context

The epic's target architecture (`design.md` §"Target architecture") inserts a
dedicated **Sema** pass between the parser and `CodeGen`:

```
Lexer ─→ Parser (stamps SourceLocation, U1) ─→ Sema (NEW) ─→ CodeGen
```

Today no such pass exists. Semantic behavior is split and inconsistent:

- **Resolution is half in the parser, half in codegen.** The parser resolves
  *variables* and *functions* eagerly during parsing and throws a located
  `CompileError` on failure — `VariableExpression::Parse`
  (`QExpression.cpp:927-940`) rejects an undefined name (now
  `Failed to find symbol '<name>'`, U2). But *field* and *method* resolution
  lives only in `CodeGen`: `genFieldAccess`/`genMethodCall`
  (`CGStruct.cpp:968,994`) `return nullptr` **silently** when a field or method
  is unknown. Because that path is behind `BLANG_HAS_LLVM` and never reports,
  `p.nonexistent` is **accepted (exit 0)** in a `--parse-only` compile and is
  mis-handled even in an LLVM build (silent drop, per the audit). Verified
  2026-07-13: `p.nonexistent` and `p.frobnicate()` both exit 0 under
  `--parse-only`.
- **No expression carries a resolved type.** The `Expression`/`Type` classes
  exist, but nothing authoritatively records "what type is this expression?"
  on the node. Codegen re-derives types locally and inconsistently. The epic
  (`design.md` decision 3) requires a **typed AST**: sema fills every
  expression's resolved type once, and codegen reads it instead of
  re-deriving — divergence then lives in exactly one place.
- **Nothing runs semantic checks in parse-only / non-LLVM builds.** REQ-004
  requires a semantic pass in *all* build modes; a mistake must be catchable
  without LLVM installed.

This unit builds the **skeleton** of that pass: it resolves names and members
and annotates expression types, and it establishes the `fail/sema/`
negative-test category (with the U2 expected-error harness) that every later
unit's diagnostics land in. It deliberately does **not** add type-*checking*
rules — argument-count/type checking, assignment/return compatibility, and
coercion removal are U4; match/generic soundness is U5; ownership is U6;
concurrency is U7. The only accept/reject change permitted here is that
previously-silent **member-resolution failures now become located errors in
every build mode** (a soundness fix, migrated per the strict decision).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Unknown names are rejected with a located error, in every build mode (Priority: P1)

As a BLang author, when I reference a name that does not exist — an undefined
variable, an unknown function, a struct field the type does not have, or a
method the type does not define — the compiler rejects the program with one
`<file>:<line>:<col>: error: <message>` line pointing at the reference, whether
or not the compiler was built with LLVM, and whether or not I pass
`--parse-only`. Nothing resolves to a silent `nullptr` and compiles anyway.

**Why this priority**: This is REQ-006 and the core value of the unit: the
audit's "unknown field → silent nullptr, statement dropped" hole
(`design.md` audit program #10) becomes a real, located rejection, and it is
enforced in the parse-only build (REQ-004) that later units and CI rely on.

**Independent Test**: In both the LLVM build and a non-LLVM (`BLANG_ENABLE_LLVM=OFF`)
build, compile four fixtures — undefined variable, unknown function, unknown
struct field, unknown method — with `--parse-only`; each exits non-zero and
prints exactly one line matching `^[^:]+\.b:[0-9]+:[0-9]+: error: ` that names
the offending reference.

**Acceptance Scenarios**:

1. **Given** a function body that reads a variable never declared in scope,
   **When** it is compiled (`--parse-only`, either build), **Then** it is
   rejected with a located error naming the variable.
2. **Given** a call to a function that is not declared or imported, **When** it
   is compiled, **Then** it is rejected with a located error naming the
   function.
3. **Given** access to `value.field` where `field` is not a member of
   `value`'s (struct) type, **When** it is compiled (`--parse-only`, either
   build), **Then** it is rejected with a located error naming the field and
   the type — **not** accepted, and not a silent drop.
4. **Given** a call `value.method(...)` where `method` is not defined on
   `value`'s type (nor a builtin for that type), **When** it is compiled,
   **Then** it is rejected with a located error naming the method.
5. **Given** any program that compiled and passed the suites before this unit
   and does **not** reference an unknown name, **When** it is compiled,
   **Then** it still compiles (no spurious resolution error).

---

### User Story 2 - A semantic pass runs in all build modes and types every expression (Priority: P1)

As a compiler maintainer, a dedicated semantic-analysis pass runs between
parsing and code generation in **every** build configuration (LLVM and
non-LLVM, including `--parse-only`), walks the parsed AST, resolves references,
and records the resolved type of every expression on the node. Code generation
consumes those annotations rather than re-deriving types, so there is one
authoritative type representation.

**Why this priority**: This is REQ-004 and design decision 3 (typed AST). It is
the structural foundation every later unit (U4–U7) builds on: they add checks
*inside* this pass, and they read expression types *from* the AST. Without it,
those units have nowhere to live and no shared type to check against.

**Independent Test**: Build with `-DBLANG_ENABLE_LLVM=OFF`; confirm the sema
pass is compiled and invoked (a resolution error fixture is rejected with a
located diagnostic — impossible unless sema ran without LLVM). Confirm the pass
runs for `--parse-only` in the LLVM build too (same fixture rejected). Confirm
codegen still produces correct output for the existing E2E suite (it now reads
sema's type annotations).

**Acceptance Scenarios**:

1. **Given** a non-LLVM build, **When** a program with an unknown member is
   compiled, **Then** the semantic pass detects and reports it (proving sema
   runs without any LLVM dependency).
2. **Given** the LLVM build, **When** a valid program is compiled end-to-end,
   **Then** every `codegen_*.b` test still passes (codegen reads sema's type
   annotations; no behavioral regression).
3. **Given** a valid expression of a determinable type (literal, variable,
   field access, function/method call result, operator result), **When** sema
   runs, **Then** that expression node carries its resolved type afterwards
   (the annotation later units and codegen read).
4. **Given** the `--combine` / multi-module build (stdlib compiled with user
   code) and `.bmod`-imported symbols, **When** sema runs, **Then** resolution
   uses the same combined scope the parser used and does not spuriously reject
   cross-module or imported references.

---

### User Story 3 - Resolution failures are pinned by negative tests in a `fail/sema/` suite (Priority: P1)

As a compiler maintainer, each resolution error class has a negative test under
`test_files/fail/sema/` that asserts *both* that a located error was produced
(canonical format) *and* the specific message, using the U2 expected-error
harness, and `run_tests.sh` runs that suite in both build modes. This is the
category the later units' `audit_NN.b` programs (and the epic's ≥ 25-file
`fail/sema/` done-condition) will populate.

**Why this priority**: It operationalizes REQ-006 (every new diagnostic gets an
expected-error test, per the constitution) and lays the groundwork for
done-condition #3 without pulling U4/U5/U7's numbered audit programs into this
unit.

**Independent Test**: `run_tests.sh` (both build modes) reports the new
`fail/sema/` tests as passing; each asserts the canonical regex plus its
message; mutating one expected pattern makes that test fail (harness still
discriminates).

**Acceptance Scenarios**:

1. **Given** the `test_files/fail/sema/` directory of resolution fixtures,
   **When** `run_tests.sh` runs (either build mode), **Then** each is judged by
   the U2 rule — rejected (exit ≠ 0) **and** stderr matches the declared
   pattern — and all pass.
2. **Given** any file under `fail/sema/`, **When** `run_tests.sh` runs it,
   **Then** the harness asserts the canonical located-error regex
   `^[^:]+\.b:[0-9]+:[0-9]+: error: ` for it (the per-file check the epic
   done-condition #3 requires), in addition to any message-specific pattern.
3. **Given** a `fail/sema/` test whose declared pattern is changed to a
   non-matching string, **When** `run_tests.sh` runs, **Then** that test is
   reported failed (the suite still discriminates wrong diagnostics).

---

### Edge Cases

- **Variable already resolved by the parser**: undefined variables/functions
  are today thrown during parsing. Sema must not *double-report* them (one
  located error only) and must not regress their message or location.
- **Builtin methods on builtin types** (`string`/`Array`/`Buffer` methods like
  `.length`, `.push()`, `.to_upper()`): these must continue to resolve — sema
  recognizes builtin members and does not reject them as "unknown method".
- **Method vs. field on the same access**: `value.name` used as a field and
  `value.name(...)` used as a method resolve against the correct member kind;
  an unknown one of either kind is a located error naming the right kind.
- **Chained / nested access** (`f().field`, `a.b.c`, `arr[i].field`): the base
  expression's resolved type drives member resolution; an unresolved base
  yields one error at the base, not a cascade.
- **Expression whose type depends on a check deferred to a later unit**
  (e.g. an operator with mismatched operands, an over-applied call): U3
  resolves what it can and records a type where determinable; it does **not**
  add the validity check (that is U4). It must not crash or fabricate on such
  input — it either records the best-known type or leaves the node for U4,
  without a silent miscompile.
- **`--combine` and `.bmod` imports**: symbols provided by stdlib modules or
  imported `.bmod` interfaces resolve exactly as they parse; sema uses the same
  scope model.
- **Generic / monomorphized references**: generic type parameters (`T`) and
  generic instantiations resolve as they do today; U3 does not add
  constraint checking (U5) and must not reject valid generic code.

## Requirements *(mandatory)*

### Functional Requirements

**Semantic pass exists and runs everywhere (REQ-004)**

- **FR-001**: A dedicated semantic-analysis pass (a `Sema` component) MUST run
  between `Module::Parse` and code generation, and MUST be compiled and invoked
  in **all** build configurations — the LLVM build, the non-LLVM
  (`BLANG_ENABLE_LLVM=OFF`) build, and every `--parse-only` compile. It MUST
  NOT be guarded by `BLANG_HAS_LLVM`.
- **FR-002**: When the semantic pass reports a problem, the driver MUST treat
  the compile as failed (non-zero exit) and MUST NOT proceed to code
  generation for that module.
- **FR-003**: All semantic diagnostics MUST be emitted through the single U2
  `DiagnosticEngine`, producing the canonical
  `<file>:<line>:<col>: error: <message>` line (no new reporting path, no
  compiler-internal C++ coordinates in default output).

**Name and member resolution (REQ-006)**

- **FR-004**: The compiler MUST reject a reference to an **undefined variable**
  with a located error naming the variable, in all build modes.
- **FR-005**: The compiler MUST reject a call to an **undefined / unresolved
  function** with a located error naming the function, in all build modes.
- **FR-006**: The compiler MUST reject access to an **unknown struct field**
  (`value.field` where `field` is not a member of `value`'s type) with a
  located error naming the field (and the type where available), in all build
  modes — including `--parse-only` and non-LLVM builds. This is the site that
  today returns a silent `nullptr` (`CGStruct.cpp`).
- **FR-007**: The compiler MUST reject a call to an **unknown method**
  (`value.method(...)` not defined on `value`'s type and not a recognized
  builtin) with a located error naming the method, in all build modes.
- **FR-008**: Resolution MUST NOT resolve any reference to a silent `nullptr`
  that is then dropped or fabricated: every reference either resolves to a real
  symbol/member or produces a located error. Recognized **builtin** members on
  builtin types (`string`, `Array`, `Buffer`) and existing generic/`.bmod`/
  `--combine` references MUST continue to resolve (no false "unknown" errors).
- **FR-009**: Each unknown-name error MUST be reported **once** (no duplicate
  diagnostics when the parser and sema both touch a reference), with an
  accurate `line:col` at the offending reference.

**Typed AST (design decision 3)**

- **FR-010**: The semantic pass MUST record, on each expression node, the
  resolved type of that expression for expressions whose type is determinable
  from resolution (literals, variable references, field accesses, function and
  method call results, index results, and operator results). This annotation is
  the single shared type representation.
- **FR-011**: Code generation MUST consume the sema-recorded type annotations
  where it previously re-derived expression types for the resolution/member
  paths this unit touches, so that the resolved type lives in one place. (U3
  wires the annotation and the member/resolution consumers; later units migrate
  remaining codegen-local derivations as they touch each area.)
- **FR-012**: The type representation MUST be consistent between sema and
  codegen (the same `Type` identity/naming), so a later unit checking a type
  and codegen lowering it agree.

**`fail/sema/` negative-test category (REQ-006 tests; done-condition #3 groundwork)**

- **FR-013**: A `test_files/fail/sema/` directory MUST exist and contain at
  least one negative test for **each** resolution error class introduced here:
  undefined variable, undefined function, unknown field, unknown method (≥ 4
  fixtures), each with an expected-message declaration (companion
  `<test>.b.expected` or inline `// EXPECT-ERROR:`) via the U2 harness.
- **FR-014**: `run_tests.sh` MUST run `test_files/fail/sema/` in **both** build
  modes and, for **every** file in that directory, MUST assert the canonical
  located-error regex `^[^:]+\.b:[0-9]+:[0-9]+: error: ` (in addition to any
  per-test message pattern) — the exact per-file check the epic done-condition
  #3 specifies. A `fail/sema/` test passes only if the compiler exits non-zero
  AND stderr matches the canonical regex AND matches the test's own pattern.
- **FR-015**: The `fail/sema/` fixtures MUST be rejected at the semantic stage
  in a `--parse-only` compile (they do not depend on LLVM), consistent with
  FR-001.

**Scope preservation and migration (design constraints)**

- **FR-016**: This unit MUST NOT add type-*checking* rules beyond resolution:
  no argument-count/argument-type checking, no assignment/initializer/return
  compatibility, no operand-validity, no coercion removal (U4); no match/
  generic-constraint checks (U5); no ownership/move (U6); no concurrency (U7).
- **FR-017**: The only accept/reject change permitted is that previously-silent
  member-resolution failures (unknown field/method) become located errors.
  `./run_tests.sh` and `./test_codegen.sh` MUST pass fully in **both** build
  modes at the unit boundary. Any file in the existing corpus (`test_files/`,
  `stdlib/*.b`, `demos/*.b`) that the new resolution rejects MUST be fixed to be
  correct within this unit (if small) or explicitly recorded for the U8 sweep;
  either way the suites are green at the boundary.
- **FR-018**: `--combine` / multi-module builds and the `bcc` pipeline MUST keep
  working; sema MUST resolve against the same combined/`.bmod` scope model the
  parser uses.

### Key Entities

- **Sema (pass)**: the new semantic-analysis component invoked by the driver
  after `Module::Parse`, before codegen, in all build modes. Walks the AST,
  resolves references, annotates expression types, and reports through the
  `DiagnosticEngine`. Owns no LLVM dependency.
- **Resolved type annotation**: the resolved `Type` recorded on each expression
  node — the single shared representation read by later checks and by codegen.
- **Scope / symbol model (existing)**: the `Scope`/`Symbol` tables the parser
  already builds (including combined-scope and `.bmod`-merged symbols); sema
  resolves against the same model, not a new one.
- **`fail/sema/` test category**: negative fixtures asserting resolution
  diagnostics via the U2 expected-error harness; the container the later units'
  `audit_NN.b` programs populate toward done-condition #3.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001** (member resolution enforced in parse-only, both builds): For each
  of the four resolution error classes, a fixture compiled with
  `build/qcc --parse-only <file>` and with the non-LLVM build exits non-zero
  and prints exactly one line matching `^[^:]+\.b:[0-9]+:[0-9]+: error: `. In
  particular, unknown-field and unknown-method programs that exit 0 today exit
  non-zero after this unit.
- **SC-002** (sema runs without LLVM): The non-LLVM build
  (`-DBLANG_ENABLE_LLVM=OFF`) rejects an unknown-member program with a located
  diagnostic — demonstrating the semantic pass runs with no LLVM dependency.
- **SC-003** (`fail/sema/` suite): `test_files/fail/sema/` contains ≥ 4
  resolution fixtures (one per class), each with an expected-message pattern;
  `run_tests.sh` runs them in both build modes and all pass, asserting the
  canonical regex plus the message.
- **SC-004** (harness discriminates): Changing a `fail/sema/` test's expected
  pattern to a non-matching string makes `run_tests.sh` fail that test; reverting
  restores green.
- **SC-005** (no resolution false positives / both suites green): Gate A
  (`./run_tests.sh && ./test_codegen.sh`, LLVM build) and Gate B
  (`BUILD_DIR=build-parse ./run_tests.sh`, non-LLVM build) both exit 0; the
  pre-U3 passing set still passes (no valid program spuriously rejected), with
  any newly-rejected corpus file migrated or recorded per FR-017.
- **SC-006** (typed AST consumed by codegen): The full `test_codegen.sh` E2E
  suite (63 tests) passes with codegen reading sema's type annotations on the
  member/resolution paths this unit migrates — no behavioral regression.
- **SC-007** (quiet preserved): A clean compile of a passing file remains
  byte-silent (`out=$(build/qcc --parse-only test_files/pass/func_simple.b 2>&1);
  test -z "$out"` succeeds) — the sema pass adds no output on success.

## Assumptions

- U1's `SourceLocation` on every AST node and U2's `DiagnosticEngine` and
  expected-error harness are in place and are reused unchanged; sema consumes
  them and does not re-derive locations or add a second reporting path.
- The parser continues to build the `Scope`/`Symbol` model (including
  combined-scope and `.bmod` merges); sema resolves against that model rather
  than constructing a parallel symbol table.
- "Types every expression" is bounded to types determinable from resolution in
  this unit; expressions whose validity/type depends on checks owned by later
  units are resolved as far as possible without adding those checks, and are
  handled without crash or fabrication.
- The strict-migration decision (overview.md Assumptions) applies: newly
  rejected member-resolution failures in the corpus are fixed here or recorded
  for U8; suites stay green at the boundary.
- The non-LLVM build is configured with `-DBLANG_ENABLE_LLVM=OFF` and exercised
  via `BUILD_DIR=build-parse ./run_tests.sh` (the epic's Gate B), matching U1/U2.

## Out of Scope

- Core type checking — argument count/type, assignment/initializer/return
  compatibility, operand validity, coercion removal, missing-return
  (U4, REQ-005/REQ-012).
- Match exhaustiveness and generic-constraint checking (U5, REQ-007/REQ-008).
- Ownership/move analysis (U6, REQ-009) and concurrency safety (U7, REQ-010).
- Creating the numbered `audit_01.b … audit_10.b` programs — those are authored
  by U4 (audit_01–05, 10), U5 (audit_08, 09), and U7 (audit_06, 07) and
  completed in U8; U3 only establishes the `fail/sema/` category and harness
  enforcement they use.
- Multi-error recovery, caret/snippet rendering, warnings, `--json` (deferred
  diagnostics epic).
- Any change to the legacy Bison/Flex path (`parser.yy`, `parse_helpers.cpp`).
