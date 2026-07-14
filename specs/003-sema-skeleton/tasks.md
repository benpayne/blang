---
description: "Task list for U3 — Semantic Pass Skeleton: symbols, types, resolution"
---

# Tasks: Semantic Pass Skeleton — Symbols, Types, Resolution (U3)

**Input**: Design documents from `/specs/003-sema-skeleton/`

**Prerequisites**: plan.md, spec.md, research.md (R1–R9), data-model.md,
contracts/ (sema-pass, resolution-diagnostics, fail-sema-harness)

**Epic**: blang-ast — Unit U3, covers REQ-004, REQ-006 (+ design decision 3
typed AST). **Branch**: `epic/blang-ast/u3-sema-skeleton`.

**Tests**: This unit's "tests" are `fail/sema/` negative fixtures + harness
self-check + the gate suites (per constitution II, a reject-only skeleton is
verified by expected-message negative tests, not new `codegen_*.b`).

**Scope guardrails**: NO type-checking rules (U4), NO match/generics (U5), NO
ownership (U6), NO concurrency (U7), NO numbered `audit_NN.b` files. The only
accept/reject change is unknown field/method → located rejection (FR-017).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on incomplete task)
- **[Story]**: US1 (resolution), US2 (sema-runs/typed-AST), US3 (fail/sema harness)

## Path Conventions

Single compiler project, sources at repo root (`Sema.h/.cpp`, `Expression.h`,
`qcc.cpp`, `CGStruct.cpp`, `CMakeLists.txt`, `run_tests.sh`,
`test_files/fail/sema/`, `CLAUDE.md`).

---

## Phase 1: Setup (baseline)

- [ ] T001 Record the pre-U3 baseline: build both configs
  (`cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm && cmake --build build -j"$(nproc)"`
  and `cmake -S . -B build-parse -DBLANG_ENABLE_LLVM=OFF && cmake --build build-parse -j"$(nproc)"`),
  run `./run_tests.sh`, `./test_codegen.sh`, `BUILD_DIR=build-parse ./run_tests.sh`,
  and note the passing/failing counts in the PR description (FR-017 anchor).
  Confirm `p.nonexistent` / `p.frobnicate()` currently exit 0 under
  `--parse-only` (the gap this unit closes).

---

## Phase 2: Foundational — the Sema pass exists and runs (BLOCKS US1/US2)

**⚠️ CRITICAL**: No resolution/annotation work begins until the pass is wired
and running in both build modes.

- [ ] T002 [US2] Create `Sema.h` (repo root, `QLang` namespace, house style)
  per data-model.md: `class Sema` with a static/instance entry
  `bool analyze( Module *module, Scope *scope, DiagnosticEngine &diag )`. No
  LLVM includes. Include `Expression.h`, `Type.h`, `DiagnosticEngine.h`.
- [ ] T003 [US2] Create `Sema.cpp` implementing an AST walk (functions →
  blocks → statements → expressions) that currently resolves nothing new and
  reports nothing — a no-op skeleton returning `true`. House style.
- [ ] T004 [US2] Add `Sema.cpp` to `QCC_SOURCES` in `CMakeLists.txt` (the
  always-built list, NOT the `if(LLVM_FOUND)` block); confirm both configs
  compile (`cmake --build build` and `cmake --build build-parse`).
- [ ] T005 [US2] Wire the driver: in `qcc.cpp`, after each non-extern module
  parses (`Module::Parse` non-null) and before the `#ifdef BLANG_HAS_LLVM`
  codegen block, call `Sema::analyze( mod, fileScope, *gDiag )`; on `false`
  (or `gDiag->hasErrors()`) return non-zero and skip codegen for that module
  (contracts/sema-pass.md). Extern `.bmod` modules are not analyzed.

**Checkpoint**: Sema compiles, links, and runs in both build modes; no behavior
change yet; all suites green.

---

## Phase 3: US2 — typed-AST annotation slot (Priority: P1)

**Goal**: Every determinable expression carries its resolved type; the single
shared representation exists for codegen and later units.

- [ ] T006 [US2] Add a resolved-type slot to the `Expression` base in
  `Expression.h`: `SmartPtr<Type> mResolvedType;` with
  `void setResolvedType( Type* )` and `Type *getResolvedType() const`
  (data-model.md). Default nullptr; friend `CodeGen` as needed.
- [ ] T007 [US2] In `Sema.cpp`, annotate the resolved type of the expression
  kinds determinable from resolution: constants (literal type), variable refs
  (the `VariableDefinition`'s type), field access (field type), function/method
  call results (return type), index (element type), operator results
  (operand/result type). Leave nullptr where the type depends on a later-unit
  check; never fabricate (FR-010, R3).

**Checkpoint**: annotations populated on valid programs; suites still green
(no codegen consumption yet).

---

## Phase 4: US1 — member resolution enforced in all build modes (Priority: P1)

**Goal**: Unknown struct field/method → one located error, in `--parse-only`
and non-LLVM builds. This is the behavior change.

**Independent test**: `build-parse/qcc --parse-only /tmp/unk.b` (unknown field)
exits non-zero with a canonical located line naming the field.

- [ ] T008 [US1] In `Sema.cpp`, resolve `FieldAccessExpression`: resolve+type
  the base (`mObject`), obtain its struct `Type`, look up `mFieldName` on the
  struct definition; on miss, report via `diag` a located error naming the
  field (and type where available); on hit, annotate the field's type
  (R4, FR-006). One error at the base if the base is unresolved (no cascade).
- [ ] T009 [US1] In `Sema.cpp`, resolve `MethodCallExpression`: resolve+type
  the base, look up the method on the struct definition **or** as a recognized
  builtin for the base type; on miss, located error naming the method; on hit,
  annotate the return type (FR-007).
- [ ] T010 [US1] Builtin/generic/combine no-false-positive guard: recognize
  builtin members on `string`/`Array`/`Buffer`, generic-parameter/monomorphized
  bases, and `--combine`/`.bmod` symbols exactly as codegen accepts them, so
  none is reported "unknown" (FR-008, R5). Cross-check against the acceptance
  logic in `CGStruct.cpp` member paths.
- [ ] T011 [US1] Confirm var/func single-report: the parser's existing located
  var/func errors flow through `gDiag` and sema does not re-report them
  (FR-009, R6). Add a code comment documenting the bounded split (research R2).

**Checkpoint**: unknown field/method rejected in both builds with located
errors; valid member access unaffected; run Gate A/B here (before codegen
consumption) to isolate resolution regressions.

---

## Phase 5: US2 — codegen consumes the annotation (Priority: P1)

- [ ] T012 [US2] In `CGStruct.cpp`, change `genFieldAccess`/`genMethodCall`
  member paths to read the sema-resolved member/type instead of re-deriving on
  the paths this unit touches; the former silent-`nullptr` fallbacks on those
  paths are now "can't happen after sema" (FR-011, R7). Do NOT convert all
  codegen fallbacks to ICEs — that is U4.
- [ ] T013 [US2] Run `./test_codegen.sh` (63 E2E) and confirm no regression
  (SC-006): codegen produces identical behavior reading sema's annotations.

**Checkpoint**: typed-AST consumed on member paths; E2E green.

---

## Phase 6: US3 — fail/sema/ fixtures + harness (Priority: P1)

**Goal**: One negative test per resolution class; `run_tests.sh` asserts the
canonical format for every `fail/sema/` file.

- [ ] T014 [US3] Extend `run_tests.sh`: for a negative test whose path is under
  `test_files/fail/sema/`, additionally require stderr to match the canonical
  regex `^[^:]+\.b:[0-9]+:[0-9]+: error: ` (in addition to the per-test pattern),
  keeping the U2 behavior for the rest of `fail/` (contracts/fail-sema-harness.md,
  FR-014). The recursive `find` already discovers `fail/sema/` files.
- [ ] T015 [US3] Create `test_files/fail/sema/` with ≥ 4 fixtures — one per
  class: `undefined_variable.b`, `undefined_function.b`, `unknown_field.b`,
  `unknown_method.b` — using correct BLang syntax (struct construction
  `T(args)`, field access `x.f`) so each fails at the semantic stage
  (FR-013, R9).
- [ ] T016 [US3] Capture the **real** U3 stderr for each fixture and write a
  companion `<test>.b.expected` pattern combining the canonical regex and the
  specific message (message-from-output, to avoid drift). Confirm each fixture
  exits non-zero under `build/qcc --parse-only`.
- [ ] T017 [US3] Run `./run_tests.sh` (both build modes); confirm the
  `fail/sema/` fixtures pass and the pre-U3 verdicts are unchanged (SC-003).

**Checkpoint**: fail/sema/ live in both modes; harness discriminates.

---

## Phase 7: Migration & docs

- [ ] T018 Migration sweep: build the U3 compiler and run both suites over the
  full corpus; if the new member resolution rejects any `test_files/`,
  `stdlib/*.b`, or `demos/*.b` file, fix it to be correct (if small) or record
  it in the PR description for the U8 sweep — suites MUST be green at the
  boundary (FR-017). Note the count.
- [ ] T019 [P] Update `CLAUDE.md`: note the semantic pass (`--parse-only` is now
  parse + sema; sema runs in all build modes), the four resolution diagnostics,
  and the `test_files/fail/sema/` category. No `docs/language_design.md` change
  (no language rule changed, only enforced).
- [ ] T020 Confirm `--combine`/multi-module and `bcc` still work (compile a
  `--combine` case and a `bcc` build) (FR-018).

---

## Phase 8: Gates (must all pass before requesting review)

- [ ] T021 Gate A (LLVM build): `./run_tests.sh && ./test_codegen.sh` exit 0.
- [ ] T022 Gate B (non-LLVM build): `BUILD_DIR=build-parse ./run_tests.sh` exit 0.
- [ ] T023 Gate D (quiet compile):
  `out=$(./build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"`
  succeeds with exit 0 (SC-007).
- [ ] T024 SC-004 harness self-check: mutate one `fail/sema/` fixture's pattern
  to `ZZZ_NO_MATCH`, confirm `./run_tests.sh` FAILs that test, then revert;
  confirm the un-mutated suite is green.
- [ ] T025 U1 golden regression: `--dump-locations` diffs for
  `func_simple`/`match_basic` remain clean in both build modes (sema must not
  perturb the location dump).

---

## Dependencies & Execution Order

- **Setup (T001)** → baseline.
- **Foundational (T002–T005)** blocks everything (the pass must exist/run).
- **US2 annotation (T006–T007)** depends on Foundational; precedes codegen
  consumption.
- **US1 resolution (T008–T011)** depends on Foundational + the annotation slot
  (member lookup needs the base type). Delivers the behavior change.
- **US2 codegen consumption (T012–T013)** depends on US1 (reads what sema
  resolved).
- **US3 harness+fixtures (T014–T017)** depends on US1 (patterns come from real
  U3 output).
- **Migration/docs (T018–T020)** after US1–US3.
- **Gates (T021–T025)** last.

## Parallel Opportunities

- T019 (docs) is `[P]` relative to T018/T020.
- The four `fail/sema/` fixtures (T015) can be authored together, but their
  `.expected` patterns (T016) must come from real output after US1 lands.

## MVP Scope

US1 (member resolution enforced everywhere) is the unit's core value and the
soundness fix. US2 (sema-runs-everywhere + typed AST) is the structural
foundation later units require; US3 pins the diagnostics. All three are P1 and
required for the done-when.

## Implementation Strategy

Follow plan.md's order: Sema skeleton + wiring → annotation slot → member
resolution → codegen consumption → fail/sema harness+fixtures → migration →
docs → gates. Keep suites green at each checkpoint; the highest-risk step is
member resolution (false positives on builtin/generic/combined access) — Gate
A/B and `test_codegen.sh` guard it.
