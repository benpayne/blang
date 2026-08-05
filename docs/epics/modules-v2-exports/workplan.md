# Workplan: modules-v2-exports

**Epic**: [overview.md](overview.md) · **Archetype**: evolve

## Unit map

```text
U1 ─▶ U2 ─▶ U3 ─▶ U5
        └▶ U4 ───┘        # U3 and U4 can run in parallel after U2
```

U5 (private fields + migration) is deliberately last: it breaks every
field reach-in at once and needs U2's methods and U4's accessors as the
migration target (review F1/F8 sequencing).

## Units

### U1 — Construction ABI: library-emitted factory (the crux — spike first)

- **Covers**: REQ-001
- **Preconditions**: none
- **Work**: Replace consumer-side caller-allocation for cross-module
  construction with a factory emitted by the defining module:
  `StructName_new(args) -> ptr` wrapping `__blang_rc_alloc_dtor` (size + dtor
  computed where the layout lives) + `StructName_init`. Consumer-side release
  stays `__blang_rc_release` (dtor pointer stored at allocation — verify this
  path needs no runtime change). Begin with a **spike**: hand-wire one
  lib+bin pair in `test_build/` proving construct/call/release across a
  `.bmod` with zero consumer layout knowledge, ASan-clean, before
  generalizing. Same-module construction may keep the current inline path —
  the factory is the cross-module ABI. **The factory applies only to
  non-generic structs** — generic construction is by design consumer-side
  (the `.bmod` ships full field lists and verbatim bodies; the consumer
  monomorphizes and computes size/dtor locally). The spike's generic case is
  a **regression guard only**: confirm the existing generic inline path is
  untouched and keeps working alongside the factory — do NOT attempt a
  generic factory (audit C4).
- **Done condition**: spike fixture in `test_build/` builds, runs with
  expected output, and is leak-clean under ASan; `./test_codegen.sh
  --leak-check` green; no regression in `./run_tests.sh` /
  `./test_codegen.sh` (both build modes).
- **Audit**: per constitution; **security dimension mandatory** (allocation
  ABI, dtor function pointers).
- **Budget hint**: 8–12 turns (spike may burn several).
- **Team hint**: single specialist hire (codegen); not parallelizable.
- **Speckit**: `029-construction-abi-factory`

### U2 — `.bmod` true interface: method/init/factory signatures, conformance records, cache versioning

- **Covers**: REQ-002, REQ-007, REQ-009, REQ-011 (emission half)
- **Depends on**: U1
- **Preconditions**: factory ABI proven by U1's spike.
- **Work**: Extend `BmodEmitter` to ship, for non-generic exported structs:
  `init`/method signatures and the factory declaration (P8); add
  protocol-conformance records (`impl Protocol for Type`) for exported types
  (D16); keep enum variant/payload emission as-is per D17. Consumers resolve
  and call these through the existing merge path (import *enforcement* is
  Epic B). Add a `.bmod` format-version constant emitted into the file and
  salted into `BuildCache::computeKey` (review F6) — this unit makes the
  first format change, so the salt lands here. **Interim semantics** (state
  in the PR): all methods ship until U3 adds `pub`; U3 flips the default.
- **Added after U1's review** (findings folded in from `audit-u1.md` §0.4 and
  `known-issues.md`):
  - **`build-system` CI job** running `test_build/run_build_tests.sh` — without
    it, done-conditions 1, 5 and 6 rest on a suite CI never runs (F-A). The job
    must **provision the sanitizer archives**
    (`cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined` + build), or
    U1's cross-module ASan leg prints `SKIP` forever while the suite reports
    green (MAJOR-6). The leg now fails rather than skips under `CI=true`.
  - **Golden `.bmod` files** rather than more `grep` assertions (F-F). Expect
    U3 to update them when `pub` filtering lands — that is the signal, not churn
    (KI-4).
  - **Fix the `table pub struct` emission order** with a regression fixture
    (M1/F-C): `emitStruct` writes the annotation before `pub`, the inverse of
    source order, and U5's D15 work makes `table` structs load-bearing across
    the boundary.
  - **Cover the prefix-aware factory branch** — it is dead code today, since no
    namespaced stdlib struct has an `init` (MINOR-B, KI-5).
  - **Separate the visibility predicate from `mFromInterface`** (M5): the ABI
    predicate must not be reused for visibility, or the namespaced stdlib is
    silently exempted from every later visibility rule.
- **Done condition**: overview done-conditions 1, 5, and 7 pass (imported
  struct constructible + callable; imported-Printable print E2E with golden;
  cache-invalidation test); `test_build/run_build_tests.sh` green including
  the new fixtures **and run by CI**; full gates green.
- **Audit**: per constitution; **security dimension mandatory** (`.bmod` is
  parsed input — emitter/parser round-trip fuzz-safety noted in review).
- **Budget hint**: 10–14 turns.
- **Team hint**: single hire; U3/U4 unblock in parallel when this merges.
- **Speckit**: `030-bmod-true-interface`

### U3 — `pub` on methods/`init`; export-signature enforcement

- **Covers**: REQ-003, REQ-006, REQ-011 (enforcement half)
- **Depends on**: U2
- **Work**: Parse `pub` on `fn`/`init`/`static fn` inside `impl` blocks
  (none exists today — `QImplBlock.cpp` accepts only unmarked members); flip
  method visibility to private-by-default per D9; `BmodEmitter` emits only
  `pub` members **for non-generic structs — generic structs are exempt from
  emission filtering** (full layout + all bodies keep shipping;
  monomorphization requires private helpers like `Map.set` →
  `find_slot`/`rehash`; visibility for generics is a Sema resolution rule
  only). Sema enforces P9 at the **library** build: a non-`pub` type
  referenced in any exported signature — function param/return, method
  signature, `pub struct` field type that must appear in D15 metadata, or
  exported enum variant payload (D17) — is a located
  `file:line:col: error:`. Enforcement applies in `--combine` mode for
  **namespaced** stdlib modules (the combine-promoted `buffer`/
  `collections`/`cli` are exempt this epic — no boundary exists; see
  overview constraints). Two pieces of infrastructure land here: (a) a
  **lightweight defining-origin marker** on symbol/struct definitions
  (bmod-injected vs. source file — NOT Epic B's canonical identity); (b) a
  **`fail/xmodule/` fixture class in `run_tests.sh`**: each fixture is a
  `lib.b` + `consumer.b` pair — the runner emits `lib.bmod` (via
  `--emit-bmod`, parse-side, works in both build modes) then compiles
  `consumer.b lib.bmod` expecting a `.expected` located-error pattern.
  Fixtures: positive (pub method cross-module; private method same-module)
  and negative (`fail/sema` per library-side rejection class,
  `fail/xmodule/` per consumer-side rejection class) in both build modes.
- **Done condition**: overview done-conditions 3 and 4 pass; the
  `fail/xmodule/` runner leg exists in `run_tests.sh` and every new
  `fail/sema` + `fail/xmodule/` fixture has a `.expected` pattern and
  matches in both build modes; existing cross-module generic tests
  (`test_build/run_build_tests.sh`) stay green; full gates green.
- **Audit**: per constitution.
- **Budget hint**: 8–12 turns.
- **Team hint**: parallel with U4.
- **Speckit**: `031-pub-members-and-export-enforcement`

### U4 — stdlib public-API redesign for opaque types (design-audited)

- **Covers**: REQ-010
- **Depends on**: U2
- **Work**: This is API **design** work, not churn (review F8): hand-write
  the accessor/method surface for the stdlib types consumers currently reach
  into — `HttpRequest` (`method`/`path`/`body`), `HttpRequestLine`,
  `HttpParsedHeaders` (parallel arrays → lookup methods), `HttpResponse`,
  `FileInfo`, and any other field-consumed stdlib struct found by a sweep of
  `examples/`, `test_build/`, and `test_files/`. Additionally (audit AF-1):
  add `pub init` to `Map` and `Set` — their only construction form today is
  the struct literal U5 outlaws — and migrate the literal call sites
  (`examples/wordfreq/main.b`, the `Map`-literal codegen tests, e.g.
  `codegen_hashmap_ops.b`, `codegen_hashmap_collision.b`,
  `codegen_generic_arc_map.b`, `codegen_bcc_collections_map.b`,
  `codegen_ix_method_chain_field.b`) with their goldens. `Map`/`Set` remain
  constructed via the existing consumer-side generic monomorphization path —
  not the U1 factory. Per Principle VI the API design is a written artifact
  reviewed **before** implementation (design audit); per Principle I
  `docs/language_design.md`/`CLAUDE.md` stdlib sections update in the same
  change. Existing field access keeps working until U5 flips the rule — this
  unit ships the *target* surface and migrates `examples/` onto it ahead of
  the flip.
- **Added after U2's review (manager ruling 2026-08-05)**: U4 also owns
  **KI-8 and KI-10** — the print/interpolation renderer defects. They are one
  defect at two sites (*which expressions can this renderer actually render?*)
  and U4 owns the question they answer: once fields are private and the stdlib
  exposes accessors, a method call and `Printable` are the only ways to read data
  out of an opaque type, and these bugs break exactly those spellings.
  Two indirect exposures make this load-bearing: (a) DC8 pushes authors from
  `p.x` toward `"{obj.field()}"`, which silently prints its own source text while
  an exit-code-only integration script still passes; (b) `CLAUDE.md` claims
  Printable structs work in `{}` placeholders, which is false inside interpolated
  literals and false for `self` — the exact shape of KI-9, which survived for
  years for that reason. **Hard constraint: fixed BEFORE U5's corpus migration**,
  or the migration bakes the defect into every example and locks the wrong output
  into the goldens.
- **Done condition**: design artifact committed under the unit's speckit dir
  and marked approved in its PR; new accessor surface implemented; `Map`/
  `Set` `pub init` shipped with migrated call sites and goldens green; all
  `examples/` integration scripts pass;
  `tools/check_no_field_reachins.sh examples/` exits 0 (script committed in
  the epic; U5 extends its field list as more types go opaque); full gates
  green.
- **Audit**: per constitution + **design audit gates before implementation**
  (Principle VI checkpoint named in overview).
- **Budget hint**: 10–14 turns.
- **Team hint**: parallel with U3; different files (stdlib/*.b, examples/)
  than U3 (parser/Sema/emitter).
- **Speckit**: `032-stdlib-opaque-api`

### U5 — Fields become private; opaque `.bmod`; data-contract metadata; corpus migration

- **Covers**: REQ-004, REQ-005, REQ-008
- **Depends on**: U3, U4
- **Work**: The flip. Member variables become always-private
  (module-visible); struct literals become module-private; field access on
  an imported type becomes a located error. Scope limits: **generic structs
  are exempt from emission changes** (full layout + bodies keep shipping;
  visibility enforced in Sema only), and the combine-promoted
  `buffer`/`collections`/`cli` are **exempt from enforcement** this epic
  (see overview constraints; recorded in Known Issues). For non-generic
  structs, `BmodEmitter` stops emitting source-resolvable field layout and
  adds the D15 compiler-facing metadata section for `table`/`@json` structs
  (and whatever minimal ABI metadata U1's factory model still requires —
  ideally none). Query codegen, Sema `.field` validation, and `@json`
  generation read D15 metadata for imported types; resolve open question #1
  (generated-function spelling) via the architect's proposal, manager
  approval against D1/D15. New **sqlite-backed lib+bin fixture in
  `test_build/`** proving done-condition 6. Migrate the remaining corpus
  (`test_build/`, `test_files/`) off field reach-ins and extend
  `tools/check_no_field_reachins.sh`'s field list to the full opaque
  surface. Extern-fn/linker-flag note (review F10): confirm a lib wrapping a
  C library still links end-to-end and record any gap in Known Issues.
- **Done condition**: overview done-conditions 2, 6, and 8 pass; done
  condition 9's full gate list green; no `codegen_parked`-style deferral —
  anything cut is recorded in the epic's Known Issues with rationale.
- **Audit**: per constitution; functional review of the epic done condition
  happens at this unit's completion.
- **Budget hint**: 15–22 turns (largest churn; includes the golden
  migrations from AF-1). If turns run short, split into U5a (enforcement
  flip + fixtures) and U5b (corpus migration) rather than cutting scope.
- **Team hint**: single hire after U3+U4 merge; the migration sweep can use
  a second hire for the mechanical fixture updates.
- **Speckit**: `033-private-fields-opaque-bmod`

## Sequencing notes for the manager

- **U1 before everything** — if the factory ABI spike fails, the epic's
  export model needs redesign; stop and raise a question rather than
  proceeding to U2.
- **U3 ∥ U4** after U2: they touch disjoint areas (compiler enforcement vs
  stdlib API). Merge order between them doesn't matter; U5 needs both.
- **Interim visibility hole between U2 and U3 is deliberate** (all methods
  exported until `pub` exists) — documented in overview constraints; don't
  "fix" it in U2.
- The generic-struct case in U1's spike is a **regression guard**, not a
  design task: the factory never applies to generics (audit C4). If any
  generic interaction looks like it needs design work, record it as an
  Epic B input — do not let it stall this epic.
- U5 may split into U5a/U5b (enforcement vs. migration) if its budget runs
  short — prefer that over cutting fixtures.
- Epic B (`modules-v2-graph`) must not start from these docs; it gets its
  own `/devbot-plan` after this epic completes.
