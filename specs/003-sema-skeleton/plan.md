# Implementation Plan: Semantic Pass Skeleton — Symbols, Types, Resolution (U3)

**Branch**: `epic/blang-ast/u3-sema-skeleton` | **Date**: 2026-07-13 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/003-sema-skeleton/spec.md`
(epic blang-ast, unit U3, REQ-004 / REQ-006 + design decision 3 typed AST)

## Summary

Introduce a new `Sema` pass (`Sema.h/.cpp`, `QLang` namespace) invoked by the
driver after `Module::Parse` and before code generation, **compiled and run in
every build mode** (added to the always-built `qcc` sources, no `BLANG_HAS_LLVM`
guard). Sema walks each parsed module's AST and (a) resolves every member
reference — struct **fields** and **methods** — that today is resolved only in
codegen and returns a silent `nullptr` (`CGStruct.cpp:968,994`), reporting
unknowns through the U2 `DiagnosticEngine` as located
`<file>:<line>:<col>: error:` diagnostics; and (b) records the resolved `Type`
of every expression it can determine on the AST node (a new resolved-type slot
on the `Expression` base), establishing the single typed-AST representation
later units and codegen read. The parser keeps its existing eager resolution of
**variables** and **functions** (already located, already in all build modes),
still routed through the same engine — no double report. A new
`test_files/fail/sema/` category holds one negative test per resolution class
(undefined variable, undefined function, unknown field, unknown method) with U2
expected-error patterns; `run_tests.sh` runs it in both build modes and asserts
the canonical located-error regex for every file (the per-file check
done-condition #3 will rely on). No type-*checking* rules are added (U4+); the
only accept/reject change is that unknown field/method access — silently
accepted today in `--parse-only` — becomes a located rejection. Decisions with
rationale in [research.md](research.md) R1–R9.

## Technical Context

**Language/Version**: C++17 (house style: tabs, Allman braces, `m`-prefix
members, PascalCase headers, `QLang` namespace — CLAUDE.md).

**Primary Dependencies**: none new. Sema depends only on the existing AST
(`Expression.h`, `Type.h`), the `Scope`/`Symbol` model, and the U2
`DiagnosticEngine`. It has **no** LLVM dependency and is compiled in the
non-LLVM build.

**Storage**: N/A. New `fail/sema/*.b` fixtures + companion `.expected` files.

**Testing**: `./run_tests.sh` (both build modes, now including `fail/sema/`),
`./test_codegen.sh` (63 E2E), plus the harness self-check (mutate a `fail/sema/`
pattern) and the quiet-compile check.

**Target Platform**: Linux / macOS; no platform-specific code.

**Project Type**: compiler (single CMake project, sources at repo root).

**Performance Goals**: one extra AST walk per module on the (already O(nodes))
compile path; no measurable impact.

**Constraints**: parser shape frozen (`Parse(Lexer&, Scope*)` factories,
`SmartPtr`, `QLang` — design.md seam); **accepted-program set unchanged except
unknown-member rejections** (FR-017); sema runs in all build modes (design
decision 1); typed AST is the single type representation (decision 3); single
diagnostic path (decision 2); `--combine`/`.bmod` and `bcc` keep working.

**Scale/Scope**: 1 new component (`Sema.h/.cpp`); a resolved-type slot added to
`Expression` (`Expression.h`); driver wiring in `qcc.cpp` (unconditional sema
invocation per module, before the `#ifdef BLANG_HAS_LLVM` codegen block);
`CMakeLists.txt` (add `Sema.cpp` to `QCC_SOURCES`); `run_tests.sh` (run
`fail/sema/` + assert canonical regex for that category); ≥ 4 `fail/sema/`
fixtures + `.expected`. Codegen's member paths read the new annotation where
they previously re-derived (bounded to the fields/methods this unit touches).
Estimated 8–14 files.

## Constitution Check

*GATE: evaluated against `.specify/memory/constitution.md` v1.1.0.*

| Principle | Assessment | Status |
|-----------|------------|--------|
| I — One Right Way / spec fidelity | No language syntax change. Enforcement of unknown-member rejection is made real and uniform (was silently accepted). `CLAUDE.md` gets a sema-pass note; `docs/language_design.md` unchanged (no rule changed, only enforced). | PASS |
| II — Test-Gated Changes | New enforcement carries `fail/sema/` negative tests with expected messages (one per class) run in both build modes; full suites green in both modes; `test_codegen.sh` proves no codegen regression. Reject-only skeleton verified by negative tests + gates (constitution II). | PASS |
| III — Reject, Don't Coerce | Directly advances it: the silent-`nullptr` member-resolution path becomes a loud located rejection; nothing new coerces. | PASS |
| IV — Memory/Thread Safety | No `runtime/*.c`, ARC, or ownership codegen touched → `--leak-check` N/A (Gate C). Full `test_codegen.sh` still run for regression. | PASS |
| V — House Style | New `Sema` follows C++17 house style; PascalCase header, `m`-prefixed members, Allman braces, `QLang` namespace. | PASS |

**Design-conformance (evaluation.md rubric 3)**: sema is a **separate pass**,
compiled in all build modes (decision 1); reports through the single
`DiagnosticEngine` (decision 2); establishes the **typed AST** annotation
(decision 3) that codegen begins to consume on the member paths it touches;
does not add later units' checks (U4–U7). **Deliberate bounded decision**
(recorded in research R2): the parser retains its pre-existing eager
variable/function resolution in U3 — sema owns the *new* member resolution and
the type annotation; fuller consolidation of all resolution into sema is left to
later units as they need it. This does not violate a fixed decision (sema is the
semantic authority for the checks this epic adds; no diagnostic is double
reported). **No Complexity Tracking entries required.**

## Project Structure

### Documentation (this feature)

```text
specs/003-sema-skeleton/
├── plan.md              # This file
├── research.md          # Phase 0 — R1..R9 decisions
├── data-model.md        # Phase 1 — Sema, resolved-type annotation, fail/sema category
├── quickstart.md        # Phase 1 — how to validate U3 by hand + gate commands
├── contracts/
│   ├── sema-pass.md            # invocation point, all-build-modes, fail→no-codegen contract
│   ├── resolution-diagnostics.md  # the four resolution error classes + message shape
│   └── fail-sema-harness.md    # run_tests.sh fail/sema/ canonical-format enforcement
├── checklists/
│   └── requirements.md  # spec quality checklist (done in /speckit-specify)
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root)

```text
Sema.h                # NEW — QLang::Sema: analyze(Module*, Scope*, DiagnosticEngine&); AST walk, resolve, annotate
Sema.cpp              # NEW — resolution of field/method refs; expression-type synthesis; reports via engine
Expression.h          # Expression base gains a resolved-type slot (setResolvedType/getResolvedType, SmartPtr<Type>)
qcc.cpp               # driver: after Module::Parse, call Sema::analyze unconditionally (all build modes);
                      #   on failure return non-zero and skip codegen. gDiag reused.
CMakeLists.txt        # add Sema.cpp to QCC_SOURCES (always built, before the LLVM-only list)
CGStruct.cpp          # genFieldAccess/genMethodCall read the sema-resolved member/type instead of re-deriving
                      #   on the paths this unit touches (silent-nullptr sites become sema's job, then trusted)
run_tests.sh          # run test_files/fail/sema/ (recursive find already covers it) + assert canonical regex
test_files/fail/sema/*.b (+ .expected)   # NEW — >=4 resolution fixtures, one per class, with patterns
CLAUDE.md             # note the sema pass (parse+sema in all modes) and fail/sema/ category
```

**Structure Decision**: Single compiler project, sources at repo root. `Sema`
is a new standalone header+impl compiled into `qcc` in **all** configurations
(added to `QCC_SOURCES`, not the `if(LLVM_FOUND)` list). Everything else is a
targeted edit. Test fixtures live under a new `test_files/fail/sema/`
subdirectory that `run_tests.sh`'s recursive `find` already discovers.

## Phasing within the unit (implementation order)

1. **Sema skeleton + wiring** (FR-001, FR-002): add `Sema.h/.cpp` with an
   `analyze(Module*, Scope*, DiagnosticEngine&)` entry that walks functions →
   blocks → statements → expressions; invoke it from `qcc.cpp` after each
   module parses, unconditionally, returning non-zero and skipping codegen on
   failure. Add `Sema.cpp` to `QCC_SOURCES`. At this step it walks and resolves
   nothing new yet — both build configs compile and all suites stay green.
2. **Resolved-type slot** (FR-010, FR-012): add a `SmartPtr<Type>` annotation
   to the `Expression` base with `setResolvedType`/`getResolvedType`; sema fills
   it for the expression kinds it can determine (literals, variable refs, field
   access, call/method-call results, index, operators). No codegen change yet.
3. **Member resolution** (FR-006, FR-007, FR-008): in sema, resolve
   `FieldAccessExpression` and `MethodCallExpression` against the base
   expression's resolved (struct) type; report a located error naming the
   unknown field/method (and type); recognize builtin members
   (`string`/`Array`/`Buffer`) and `.bmod`/combine/generic references so no
   false positives. This is the behavior change: unknown members now rejected
   in all build modes.
4. **Variable/function resolution routing** (FR-004, FR-005, FR-009): confirm
   the parser's existing located var/func errors flow through the
   `DiagnosticEngine` (they do, via the U2 top-level catch) and that sema does
   not re-report them (single diagnostic). Annotate var/call result types.
5. **Codegen consumption** (FR-011): change `genFieldAccess`/`genMethodCall`
   member paths to read sema's resolved member/type; the former silent-`nullptr`
   branches become unreachable-after-sema on the touched paths (a surprise there
   is an ICE per design decision 4 — but loud-ICE hardening of *all* codegen is
   U4; here we only stop re-deriving on these paths).
6. **`fail/sema/` fixtures + harness** (FR-013, FR-014, FR-015): create the
   directory and ≥ 4 fixtures (one per class) with `.expected` patterns
   combining the canonical regex and the message; extend `run_tests.sh` so the
   `fail/sema/` category asserts the canonical regex for every file in addition
   to the per-test pattern.
7. **Migration + gates + docs** (FR-016, FR-017, FR-018): run both build modes;
   fix or record any corpus file the new member resolution rejects; update
   `CLAUDE.md`. Gate A/B green; quiet preserved; harness self-check.

Highest-risk step is 3/5 (member resolution + codegen consumption): a
too-strict resolver could reject valid builtin/generic/combined member access
(false positive) — mitigated by keeping the builtin/`.bmod`/combine recognition
that codegen already applies, and by Gate A/B + `test_codegen.sh` at the
boundary. Migration (step 7) covers any corpus fallout.

## Complexity Tracking

No constitution violations; no entries required.
