# Spec: private fields, opaque `.bmod`, data-contract metadata

**Epic**: modules-v2-exports · **Unit**: U5 (the flip) · **Branch**: `epic/modules-v2-exports-u5a` (U5a), `-u5b` (U5b)
**Covers**: REQ-004, REQ-005, REQ-008 (+ Epic open-question #1; + Q-U4-1 construction-spelling sub-design deferred from U4; + KI-22 fold-in)
**Depends on**: U3 (merged), U4-remainder (merged `522722d`) · **Speckit**: `033-private-fields-opaque-bmod`
**Status**: Draft — awaiting design audit (`rev`) before any implementation (Principle VI).

Binding inputs: design record **D1–D17** (`docs/epics/modules-v2/overview.md`), epic decisions **A1–A7**
(`docs/epics/modules-v2-exports/design.md`), workplan U5 (`docs/epics/modules-v2-exports/workplan.md`),
standing checks **SC-1 / SC-2**, and the manager rulings folded into U3's spec (P9 field-type enforcement
covers every `pub struct` *for as long as the emitter ships layout* — **U5 removes the layout and narrows it**).

---

## 1. Problem

Today a non-generic `pub struct` ships its full field layout into the `.bmod`, and a consumer can reach
into an imported struct's fields (`c.count`) and write struct literals for it (`Counter { count: 5 }`).
The export model is inverted (design record P8/P9). U1–U4 built the machinery — a library-emitted factory,
`pub` methods/`init`, private-by-default methods, P9 export enforcement, the stdlib accessor surface. **U5
performs the flip**: member variables become always-private (module-visible), struct literals become
module-private, field access on an imported type becomes a located error, and the `.bmod` stops shipping
source-resolvable field layout for non-generic structs — retaining fields only as compiler-facing D15
metadata for `table`/`@json` data-contract structs.

## 2. Goals

- **G1** — a member variable is never nameable from another module: field access `imported.field` is a
  located `file:line:col: error:` in **both** build modes (REQ-004 / done-cond. 2).
- **G2** — a struct literal for an imported type (`Imported { ... }`) is a located error; the one external
  construction form is `Imported(...)` via `pub init` (REQ-004 / done-cond. 2).
- **G3** — the `.bmod` stops emitting source-resolvable field layout for **non-generic** structs; **generic
  structs are exempt** (full layout + all method bodies keep shipping; visibility Sema-only) (REQ-005).
- **G4** — `table`/`@json` structs keep their field shape crossing the boundary as **compiler-facing D15
  metadata** so an imported data-contract struct stays queryable/serializable from a consumer, while the
  same fields remain un-nameable in ordinary source (REQ-008 / done-cond. 6 — D15 both ways).
- **G5** — P9 export-signature enforcement **narrows** its field-type rule to data-contract structs (a plain
  struct's fields no longer cross, so a private field type is fine there); every other P9 surface is
  unchanged; the design record's P9 reproduction still errors at the **library** build for the `@json` case
  (done-cond. 4).
- **G6** — `BuildCache` keys incorporate the new `.bmod` format version; a bump invalidates a warm entry
  (done-cond. 7 — already wired in U2; U5 makes the format change).
- **G7** — the corpus (`test_files/`, `demos/`, examples) is migrated off field reach-ins; the reach-in gate
  covers the full opaque surface and is wired into CI (done-cond. 8). *(U5b)*
- **G8** — DC-9 docs: `docs/language_design.md` (the language-level flip) + `CLAUDE.md`.

## 3. Non-goals

- **The flat-merge resolution path** (`qcc.cpp:324-381`) — Epic B. U5 changes *what is emitted/enforced*,
  not *how names resolve*.
- **Generic emission/monomorphization** — unchanged (A6). Generic structs keep full layout + all bodies.
- **Module-private enforcement for the combine-promoted `buffer`/`collections`/`cli`** — exempt this epic
  (A7/KI-3); recorded in Known Issues. `Map`/`Set` literals keep working (no boundary exists), but U5b still
  migrates them to a construction call for cleanliness (§7).
- **Sema-enforced field/literal privacy for combine-mode *namespaced* stdlib (`net`/`fs`/`timer`).** These
  parse from `.b` source under `--combine`, so `isFromInterface() == false` and U5's `.bmod`-scoped rules do
  not reach them. Their field/literal privacy stays **grep-gated** (`check_no_field_reachins.sh`) this epic;
  real per-module enforcement is Epic B (KI-16 / KI-23). U5's compile-time enforcement is `.bmod`-path-only.
- **Real module identity / per-module scopes / import enforcement** — Epic B.
- **Any ARC/ownership/runtime change** — the U1 factory already composes the existing runtime.

## 4. Design — the flip

### 4.1 Emitter: drop plain-struct field layout, keep data-contract fields (BmodEmitter.cpp)

`emitStruct` currently emits every field of every `pub struct` (`BmodEmitter.cpp:235-240`). Change, for
**non-generic** structs only:

- **Plain (non-`table`, non-`@json`) struct** → emit an **empty body**: `pub struct Name {\n}`. No fields.
  (Verified: an empty-body struct with `pub init` + methods parses, constructs via the factory, and its
  methods link from the `.a`.) This delivers the build-system win (design record §"Export model": a private
  field's type no longer moves the interface hash) and makes field access fail naturally in the consumer
  (its `StructDefinition` has an empty field list, so `resolveFieldAccess` finds no field — §4.2 upgrades the
  message).
- **Data-contract (`table` or `@json`) struct** → **keep emitting the real field declarations** in the
  struct body. Their shape *is* the data contract (DB columns / JSON keys, D15), so the fields must cross,
  **and** editing a field must bump the interface hash — which keeping real field declarations gives for
  free. These emitted fields are the "compiler-facing metadata section" of design.md's diagram, realized as
  in-body declarations rather than a separate `[meta]` block: functionally identical (the fields reach the
  consumer's `StructDefinition`), simpler (all existing `getFields()`-based codegen — `to_json`, `SQLGen`,
  query row-mapping — keeps working cross-module unchanged), and **visibility is enforced as a resolution
  rule, not an emission rule** (D15's exact wording). *Design-audit checkpoint: confirm this realization of
  the "[meta] section" against D15/A5.*
- **Generic struct** → unchanged (A6): full field layout + all method bodies keep shipping.

The `isExportedDataContract(structDef)` predicate already exists (`Sema.cpp:356`, `isTable() || @json`); the
emitter will use the same rule.

### 4.2 Sema: the two visibility rules (Sema.cpp — both build modes)

The rules key on whether a struct **arrived through a parsed `.bmod`** — the existing, honestly-named
`StructDefinition::isFromInterface()` predicate (set at `qcc.cpp:463`, the **`.bmod`-input branch only**;
already the load-bearing signal for construction at `Sema.cpp:1071` and the factory at `CGStruct.cpp:838`).

U5 does **not** add an `isImported()` synonym and makes **no** "from-interface == imported" claim — that
equivalence is **false**. Combine-mode namespaced-stdlib structs (`net`/`fs`/`timer`) are parsed from `.b`
source under `--combine`, so `setFromInterface(true)` is never called for them: they are cross-module yet
`isFromInterface() == false`. **Consequently U5's compile-time field/literal enforcement is
`.bmod`-path-only this epic.** Combine-mode namespaced-stdlib field/literal privacy is **not** Sema-enforced
here; it stays **grep-gated** by `tools/check_no_field_reachins.sh`, and closing it with real per-module
scopes is Epic B's job (Known Issues KI-16 / new KI-23).

Reusing `isFromInterface()` in these rules is **not** the ABI-flag overload Type.h (443-446, 467-475) warns
against: the rules are themselves **scoped to `.bmod`-arrival**, which is exactly what the flag *holds* — not
a general "is this visible here?" question. U5 updates the Type.h comment to record that the `.bmod`-scoped
visibility rules read this flag and that Epic B introduces real module identity for the general (combine-mode
+ `.bmod`) case. Wherever the spec below says "imported", read it as **"arrived through a `.bmod`"**.

**Rule 1 — field access on a `.bmod`-arrived type (REQ-004, done-cond. 2).** In `resolveFieldAccess`
(`Sema.cpp:1450`), before the generic "has no field" fallthrough, if `structDef->isFromInterface()`, report a
located error:
```
field 'count' of type 'Counter' is private to its defining module
```
This fires uniformly for a plain `.bmod`-arrived struct (whose field list is empty — otherwise the message
would be the less-helpful "has no field") and for a data-contract `.bmod`-arrived struct (whose fields *are*
present as D15 metadata but must not be source-nameable). Same-module access is unaffected
(`isFromInterface()` false), so `self.field` inside a struct's own methods and cross-struct access **within a
module** keep working (D9: private == module-visible). Combine-mode namespaced-stdlib field access
(`isFromInterface()` false) is **not** caught here — grep-gated, Epic B (above).

**Rule 2 — struct literal for a `.bmod`-arrived type (REQ-004, done-cond. 2).** Sema does **not** validate
struct literals today (`Sema.cpp:1052-1059` resolves the type only; codegen silently skips unknown fields at
`CGStruct.cpp:674`). Add: in the `StructLiteralExpression` handler, if the resolved type is a struct with
`isFromInterface()`, report:
```
struct literal for imported type 'Counter' is not permitted; construct it with Counter(...)
```
This gives external construction exactly one form (`Counter(...)` via `pub init`, D9). *(Bonus: the current
silent-skip of an unknown field name in a same-module literal is a latent bug; U5 may additionally add a
located "no field 'x'" for same-module literals — noted, not required by a done-condition; decide at audit.)*

**Query field references are NOT field access.** `.field` in `where`/`set`/`order_by` goes through
`checkTableField` (`Sema.cpp:1391`, validates against `getFields()`) and `QueryFieldExpression`, a different
path than `resolveFieldAccess`. Rule 1 must fire only for `FieldAccessExpression` (`obj.field`), so an
imported `table` struct stays queryable (`query T |> where { .id == 5 }`) — D15 forward direction. This
separation is the crux of done-cond. 6 ("queryable, but naming the field in ordinary source is an error").

**GENERIC imported structs are EXCLUDED from Rules 1 & 2 in U5a** (`&& getGenericParams().empty()` on both,
decided during implementation). A generic struct ships full layout + all method bodies (A6) so consumers
monomorphize it, and its only construction form today is the struct literal (Q-U4-1 — `Name<Args>(...)` does
not parse). Enforcing field/literal visibility on generics in U5a would break every existing cross-module
generic consumer (`test_build/myapp` uses `Pair<int> { first: 10, second: 32 }` and `q.first`) with **no
migration path**, because the `pub init`/accessor surface and the `Name<Args>(...)` construction spelling do
not exist yet. Generic field/literal enforcement is therefore **coupled to §7's construction spelling and
lands together in U5b** — the natural dependency. This is a phasing refinement, not a scope cut (the epic's
"visibility for generics is Sema-only" goal is still delivered, in U5b).

### 4.3 P9 narrowing (Sema.cpp:216-239)

The exported-struct field-type check already branches on `isExportedDataContract` for its *message*
(`Sema.cpp:235`). U5 narrows its *scope*: run the field-type export check **only when
`isExportedDataContract(structDef)`**. Rationale: a plain struct no longer emits field types into the
`.bmod` (§4.1), so a plain struct's private field type never crosses — the P9 field rule is vacuous there.
A data-contract struct still emits its fields, so a non-`pub` field type still leaks and must still error at
the library build. Every other P9 surface (fn params/return, method signatures, enum variant payloads,
conformance protocol + backing methods) is **unchanged**.

**Fixture flip.** `test_files/fail/sema/p9_pub_struct_plain_private_field.b` (a *plain* `pub struct` with a
private field type) must flip **fail → pass** under U5 — its field type no longer crosses. It is converted
from a `fail/sema` fixture into a `pass` fixture (a plain struct with a private field type is now legal) with
a comment explaining the U5 narrowing, so the intent is preserved rather than deleted.
`p9_pub_struct_private_field.b` (the `@json` variant) stays a `fail/sema` fixture — its message already reads
"exported data-contract struct".

### 4.4 Format version + cache (BmodFormat.h, BuildCache.cpp)

Bump `BlangBmod::kFormatVersion` **3 → 4** and add the format-4 line to the header's changelog comment
("field layout dropped for non-generic non-data-contract structs; data-contract structs keep field
declarations as compiler-facing metadata"). The salt in `BuildCache::computeKey` (already present, U2) makes
the bump invalidate every warm entry. The format-version-on-read validation (U3, qcc.cpp) already rejects a
mismatched `.bmod` with a located diagnostic. `BuildCacheTest.cpp` (lines 61-66) **already** proves
format-version invalidation *generically* (a key at `kFormatVersion` differs from one at `kFormatVersion+1`),
so the 3→4 bump needs **no new test** — the generic case already covers it (done-cond. 7 stands on the
existing ctest).

### 4.5 What does NOT change

- **Construction ABI** (U1 factory): a consumer already constructs an imported non-generic struct via the
  library factory (`CGStruct.cpp:838`), which needs no field layout — so dropping fields is ABI-safe with
  **zero runtime change** (A1). Release stays `__blang_rc_release` (dtor stored at alloc).
- **`to_json` / `from_json` / query codegen**: all read `getFields()` via `mStructDefMap` by name. For a
  data-contract struct the fields are still present (§4.1), so these keep working cross-module by symbol
  linkage — the library's `.a` carries the generated `Todo_to_json`/`Todo_from_json` bodies, and the
  consumer's parse of the `.bmod`'s `@json` annotation forward-declares them as externs
  (`QModule.cpp:290-311`). Verified end-to-end by the new DC-6 fixture (§8).
- **Generic structs**, **combine-promoted modules**, the **flat merge**, **ARC/ownership** — untouched.

## 5. Phasing: U5a (enforcement flip) vs U5b (corpus + construction spelling)

Split as the manager pre-approved (workplan authorizes it over cutting fixtures):

- **U5a (PR #2) — the enforcement flip.** §4.1–§4.4: emitter drop + D15 retention, the two Sema visibility
  rules, P9 narrowing + the plain-field fixture flip, format 3→4 + cache test. Fixtures: `fail/xmodule/`
  (field access + struct literal on an imported type), the DC-6 sqlite lib+bin (`test_build/`), the D16
  Printable cross-`.bmod` fixture stays green (already `printapp`), positive `test_build/` construct/call.
  Done-cond. **2, 3, 4, 6, 7** + the epic gate list (done-cond. 9) for U5a's slice.
- **U5b (PR #3) — corpus migration + construction spelling + OQ#1.** §6 (OQ#1 from_json), §7 (Q-U4-1 Map/Set
  construction spelling + call-site/golden migration), the `test_files/`/`demos/` reach-in migration,
  extend `check_no_field_reachins.sh` to the full opaque surface + wire into CI, DC-9 docs. Done-cond. **8**.
- **KI-22 fix** (the for-in-over-generic-method-call loop-var element-type bug) lands as **its own commit/PR
  within the U5 phase**, before/alongside U5b's migration so idiomatic `for x in coll.method()` loops need no
  workaround (§9). **Escape hatch**: if the fix proves non-trivial or risks widening U5, stop and raise.

## 6. Epic open-question #1 — cross-module spelling for generated data-contract functions

**Question** (overview OQ#1): canonical cross-module spelling for a generated function like `Todo_from_json`
under D1 — module-qualified name, or builtin dispatch like `to_json`?

**Current state**: `to_json(x)` is a **builtin** (Sema validates the `@json` annotation on `x`'s type,
codegen dispatches to `<Type>_to_json` — the consumer names the *value*, not the function: D7 use-capability,
one spelling, already works cross-module). `from_json` has **no builtin** — a consumer calls the generated
symbol by literal mangled name: `Todo_from_json(req.body())` (`examples/todo_app/main.b`). That is a second,
asymmetric convention and a raw generated-symbol reach-in — exactly what D15's "generated-function access has
one canonical spelling" argues against.

**Architect proposal (for manager approval against D1/D15; flagged to owner at PR #2 review):**
Make `from_json` symmetric with `to_json` via a **type-directed builtin spelled as a static call**:
`Todo.from_json(jsonString)` → lowers to `<Type>_from_json`. Rationale:
- **D1-clean**: the type `Todo` is the qualified/imported name; no bare generated symbol is written.
- **One canonical spelling** (D15): `to_json(value)` / `Type.from_json(string)` — value-directed out,
  type-directed in. Both dispatch through the compiler; neither is a hand-spelled mangled symbol.
- **Reuses existing surface**: static-method-call parsing (`Type.method(args)`) already exists; Sema/codegen
  special-case `Type.from_json` the way `to_json` is already special-cased. Scoped to U5b, one call site
  (`todo_app`) to migrate.

**Lower-risk alternative** (presented for the owner): keep `Todo_from_json(str)` as the documented canonical
spelling (no code change; it already links cross-module). It embeds the qualified type name, so it does **not
conflict with a recorded decision** — but it leaves two conventions, which D15 discourages.

**Disposition**: this is **U5b**, not U5a — the DC-6 fixture (done-cond. 6) exercises `query` + `to_json`,
neither of which needs `from_json`. So U5a is unblocked regardless. I approve the symmetric-builtin proposal
against D1/D15 as the design intent and will flag it to the owner at PR review for a final call
(symmetric builtin vs. document-the-literal), per the recorded OQ#1 process.

**RESOLVED (2026-08-09, product-owner ruling): Option A — the symmetric builtin `Todo.from_json(str)`.**
Implemented in U5b commit `c042fff`; no revert. The bare `Todo_from_json(str)` form stays as an additive,
link-safe alias. Fixture `codegen_from_json_static.b` proves the round-trip + bare-spelling backward compat;
`examples/todo_app` migrated to `Todo.from_json(req.body())`.

## 7. Q-U4-1 sub-design — `Map`/`Set` construction spelling (U5b)

**Problem** (spec 032 §6): a generic struct has **no** construction form but the struct literal, which U5's
literal rule would outlaw for imported generics — except `collections` is combine-promoted and A7-exempt, so
`Map`/`Set` literals keep *working*. Migrating them off the literal (which names internal fields
`{ keys: [], values: [], buckets: [] }`) to a construction call is still the clean end state. All three
candidate spellings fail today: `Map<K,V>(...)` does not parse (re-verified), and `static fn new()/make(T)`
on a generic parse but are not monomorphized by codegen.

**Architect proposal (design-audited here; implemented in U5b):** **Option (a)** — teach the parser to accept
`Name<TypeArgs>(args)` and build a `ConstructExpression` carrying the type arguments, then teach
`genConstructExpression` to monomorphize the struct for those args (reusing `instantiateGenericStruct`) and
call the instantiated `init`. This gives `Map<string,int>()` — symmetric with the non-generic `Counter(5)`,
no new keyword, no LHS-type-inference requirement. Add a `pub init(self)` to `Map`/`Set` that clears the
backing arrays. Migrate the ~14 literal call sites + goldens.
- Rejected: static-factory monomorphization (Option b) — needs zero-arg LHS-type inference, larger surface.
- **Scope guard**: this is a parser + codegen change confined to `ConstructExpression` and the existing
  monomorphization path — in U5b's budget. If it proves to widen U5 unacceptably, **stop and raise** (escape
  hatch), and fall back to leaving `Map`/`Set` literals as-is (A7 exempts them; the reach-in gate does not
  flag literals) with a Known-Issues note.

## 8. Test plan (each done-condition item backed by a committed CI-run test)

- **Done-cond. 2** — `test_files/fail/xmodule/imported_field_access/` (consumer does `c.count`) and
  `test_files/fail/xmodule/imported_struct_literal/` (consumer writes `Counter { count: 5 }`), each a
  `lib.b` + `consumer.b` + `consumer.b.expected` located-pattern pair; green in **both** build modes via the
  `run_tests.sh` xmodule leg (emit `lib.bmod`, parse `consumer.b lib.bmod`, expect non-zero + pattern).
- **Done-cond. 3** — positive: `test_build/counterlib`+`counterapp` (construct + `pub` method cross-module,
  already green) stays green after the emitter change; a `pub`-method reachable / private-method unreachable
  pair (already covered by `fail/xmodule/private_method`).
- **Done-cond. 4** — `p9_pub_struct_private_field.b` (`@json`, stays failing at the **library** build); the
  plain variant flips to a `pass` fixture (§4.3). Both build modes.
- **Done-cond. 5** — `test_build/printlib`+`printapp` (D16 Printable dispatch on an imported type) stays
  green; its `table struct Point` `.bmod` golden is regenerated for format 4.
- **Done-cond. 6** — NEW `test_build/todolib` (a `@json table struct` + a lib function) + `todoapp`
  (sqlite-backed): the consumer runs `query T |> where { .field == ... }` and `to_json(x)` on the imported
  data-contract struct and prints expected output; a companion `fail/xmodule/imported_datacontract_field/`
  proves naming the same field in **ordinary** source is a located error (D15 both ways).
- **Done-cond. 7** — `build_cache_key` ctest updated: a format-3 → format-4 bump misses a warm entry.
- **Done-cond. 8** *(U5b)* — `tools/check_no_field_reachins.sh examples/ test_build/ test_files/` exits 0 with
  the extended field list + the `--selfcheck` teeth leg; wired into CI.
- **`.bmod` goldens** under `test_files/golden/bmod/` regenerated for format 4 (plain structs now empty-body;
  data-contract structs keep fields) — each change called out in the PR.
- **SC-1** — every new library fixture's `.bmod` parses standalone (`bmod_parses` in
  `test_build/run_build_tests.sh`). **SC-2** — every new `.bmod` construct proven with a **user-defined**
  instance, not a builtin.
- **Gates** (done-cond. 9): `run_tests.sh` + `test_codegen.sh` green in **both** build modes;
  `--leak-check` clean; `test_lsp.sh`; `test_build/run_build_tests.sh`; `ctest`.

## 9. KI-22 fold-in (own commit within U5)

`for x in <generic-struct-method-call>()` returning `Array<K>` mis-resolves the loop-var element type
(loop var defaults to `int` → garbage/segfault). Fix the for-in element-type inference to substitute the
receiver's type arguments through the method's return type (the same `methodReturnTypeName`/substitution
family U4's `receiverStructDef` work touched, applied to the for-in source in `CGStatements.cpp`). Land as
its own commit/PR within the U5 phase, before/alongside U5b's migration; add a `codegen_*.b` fixture
(`for k in map.keys()` prints correctly) with a golden, `--leak-check` clean. **Escape hatch**: if
non-trivial / widens U5, stop and raise; keep the intermediate-typed-var workaround.

## 10. Risks

- **Data-contract `.bmod` retention interpretation.** Realizing D15 as in-body field declarations (not a
  separate `[meta]` block) is a deliberate reading of design.md's diagram; flagged for the design audit. If
  the auditor requires a distinct `[meta]` section, the fallback is a comment-based section parsed on load —
  more machinery, same guarantee.
- **`.bmod`-path-only enforcement.** U5's field/literal Sema rules key on `.bmod`-arrival
  (`isFromInterface()`), so they enforce the `.bmod` cross-module path only. The flat merge is **not** the
  sole cross-module path this epic — combine-mode namespaced stdlib (`net`/`fs`/`timer`, `isFromInterface()
  == false`) is another, and its field/literal privacy stays **grep-gated** (not Sema-enforced) until Epic
  B's per-module scopes (KI-16 / KI-23). Recorded as a deliberate simplification (Principle VI Known Issue);
  Epic B introduces real module identity for the general case.
- **Same-module struct-literal validation.** Adding a located "no field" for same-module literals is a
  latent-bug fix but a behavior change; gated behind the audit (optional, not a done-condition).
- **KI-22 / construction-spelling** may each exceed U5b's budget; both carry an explicit escape hatch.

## 11. Traceability

| Goal | Requirement | Verified by |
|---|---|---|
| G1 | REQ-004 | done-cond. 2 `fail/xmodule/imported_field_access` |
| G2 | REQ-004 | done-cond. 2 `fail/xmodule/imported_struct_literal` |
| G3 | REQ-005 | format-4 `.bmod` goldens (plain structs empty-body); generic tests stay green |
| G4 | REQ-008 | done-cond. 6 `test_build/todolib`+`todoapp` + `fail/xmodule/imported_datacontract_field` |
| G5 | REQ-006 (narrowed) | done-cond. 4 `p9_*` fixtures (one flips) |
| G6 | REQ-009 | done-cond. 7 `build_cache_key` ctest |
| G7 | REQ-010/DC8 | done-cond. 8 reach-in gate over the full corpus + CI *(U5b)* |
| G8 | Principle I | `docs/language_design.md`, `CLAUDE.md` |
| OQ#1 | D1/D15 | §6 proposal, flagged to owner at PR #2 |
| Q-U4-1 | AF-1 | §7 `Map<K,V>()` construction + migration *(U5b)* |
| KI-22 | — | §9 `codegen_*.b` fixture *(own commit)* |
