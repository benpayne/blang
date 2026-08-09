# Workplan: modules-v2-graph

**Epic**: [overview.md](overview.md) · **Archetype**: evolve

## Unit map

```text
U1 (identity) ─────────────▶ U4 (module graph) ─▶ U5 (foreign refs) ─▶ U6 (enforce) ─▶ U7 (diagnostics) ─▶ U8 (aliasing, stretch)
                                     │                                      │                                       └▶ U9 (search path, stretch)
U2 (codegen fix) ─▶ U3 (tiers) ──────┴──────────────────────────────────────┘
```

- **U1 and U2 have no preconditions** and run in parallel — the two foundations
  (identity artifact; codegen crux).
- **U2 before U3** (review F3): the module-prefix double-free fix must land
  before `cli` is demoted out of the promotion list, or U3 ships the known
  double-free.
- **U6 (enforce imports)** is the convergence point + the bulk of the corpus
  churn; it needs the tiers (U3), the graph (U4), and the transitive build graph
  (U5).
- **U8/U9 are the stretch units**, gated on enforcement (U6).

Every unit carries the standard gates: `run_tests.sh` + `test_codegen.sh` green
in **both** build modes, `--leak-check` clean on any runtime/ARC-adjacent touch,
`test_lsp.sh` green (blangd must keep building), `test_build/run_build_tests.sh`
green, docs updated in the same PR as the behavior (Principle I).

## Units

### U1 — Canonical module identity (keystone; Principle VI design-audit gate)

- **Covers**: REQ-001, REQ-002
- **Preconditions**: none
- **Work**: Establish a canonical **module identity** as the resolved origin
  (`realpath` for path deps, `url@pin` for git, D5), with a serialized form = a
  short SHA-256 digest (8–12 hex) of a **portable** origin string (open-question
  #2 answer: project-root-relative path inside the workspace, else realpath with
  reproducibility waived + a Known-Issues note). Thread identity through: (a)
  type identity (nominal, owned by the defining module, D2); (b) generic symbol
  mangling — `mangleGenericName` (`CGTypes.cpp:11-21`) incorporates identity so
  two same-named exported types get distinct symbols (D10, eliminating the P10
  `linkonce_odr` collapse); (c) dedup policy (no dedup across distinct origins,
  D6). Give `mDefiningFile` (`Type.h:495`) its file→module mapping (closes KI-16
  obligation 1). **This is the load-bearing artifact — three risks collapse into
  it; the architect writes the identity model as a design artifact reviewed
  before implementation.**
- **Done condition**: a `test_build/` fixture with two path-dep modules each
  exporting a same-named generic type, both instantiated in one binary, builds/
  links/runs with distinct mangled symbols (asserted via `nm`/IR in the fixture
  script); full gates green.
- **Audit**: per constitution; **Principle VI design audit gates before
  implementation** (identity model).
- **Budget hint**: 12–18 turns (design + threading through mangling/dedup).
- **Team hint**: single specialist (codegen/types) + architect design-audit.
- **Speckit**: `034-canonical-module-identity`

### U2 — Module-prefix codegen fix (the crux; spike-first; security-mandatory)

- **Covers**: REQ-005 (the fix half)
- **Preconditions**: none
- **Work**: Repair the string-ARC **double-free** under module-prefix codegen
  that forced the `buffer`/`collections`/`cli` promotions (rationale at
  `qcc.cpp:303-307`; a namespaced module's internal string-returning call, e.g.
  `has_flag → flag_name_of`, double-frees). **Spike first**: reproduce the
  double-free under `--leak-check`/ASan on a minimal namespaced module, then fix
  the root cause. Do NOT retire any promotion here — that is U3; this unit only
  makes the fix so U3 *can*. If the root cause is deeper than the ARC path
  (e.g. touches mangling), stop and raise before widening.
- **Done condition**: a `test_codegen`/`test_build` fixture exercising a
  namespaced module's internal string-returning path is `--leak-check` clean
  (previously double-freed); no regression; full gates green.
- **Audit**: per constitution; **security dimension mandatory** (ARC /
  double-free / dtor correctness); `--leak-check` gates.
- **Budget hint**: 10–16 turns (spike may burn several).
- **Team hint**: single specialist (codegen/ARC); not parallelizable internally.
- **Speckit**: `035-module-prefix-codegen-fix`

### U3 — Three type tiers: core / prelude / library

- **Covers**: REQ-003, REQ-004; REQ-005 (the retire-promotions half)
- **Depends on**: U2
- **Work**: Replace the hardcoded `isGlobalTypeLib` promotion (`qcc.cpp:308-309`)
  with a declared, compiler-shipped **prelude manifest** whose membership is
  exactly `{Map, Set, Buffer}` (types only, D12/D13; fixed and unextendable).
  **Delete** the bare-name `addType` registrations that have no definition behind
  them (`QModule.cpp:582-584` — `Task`/`Array`/`Buffer` as bare names; D14) so an
  undefined name fails at the *type* with a better diagnostic. **Demote `cli`**
  from the promoted set to an ordinary qualified library module (it is free
  functions, not types — `cli.has_flag`-style, matching its real exports in
  `stdlib/cli.b:53-95`) — safe now that U2 fixed the double-free. Retire the
  `buffer`/`collections` promotions as the manifest dictates. **Mixed-module
  handling**: `stdlib/collections.b` defines the prelude types `Map`/`Set`
  (`:18`, `:175`) **and** the free function `sort` (`:287`); the prelude manifest
  therefore assigns tiers **per name** — the two types go to prelude, `sort`
  becomes a qualified library export `collections.sort`. **Own the `sort(...)`
  call-site migration**: every bare `sort(...)` (e.g. `examples/wordfreq/main.b`,
  and any `Map`/`Set`-literal or codegen fixtures that call it) becomes
  `collections.sort(...)` with goldens updated (or explicitly hand this migration
  to U6's corpus sweep — state which in the U3 PR). Mind the KEEP-IN-SYNC
  contract with `BmodEmitter` (`QModule.cpp:607-613`).
- **Done condition**: prelude manifest is the single source of truth (no string
  list in the driver); a `fail/sema` fixture proves an undefined type name errors
  at the type; `cli` and `collections.sort` used qualified in a fixture; every
  bare `sort(` call site migrated (grep for bare `sort(` in `examples/`/
  `test_files/`/`stdlib/` returns only qualified forms, or the residue is
  explicitly deferred to U6); full gates green in both modes; `--leak-check`
  clean.
- **Audit**: per constitution.
- **Budget hint**: 10–16 turns.
- **Speckit**: `036-type-tiers`

### U4 — Module graph & per-module export scopes (extractable resolver)

- **Covers**: REQ-006, REQ-007
- **Depends on**: U1
- **Work**: Introduce **module nodes** owning their own export scopes and an
  **import-edge** structure, keyed on U1's identity. Pull module/name resolution
  out of the `qcc.cpp` driver loop into a **standalone resolver component** (a
  named class, e.g. `ModuleGraph`/`Resolver`) with its own unit test — the clean
  seam Epic C consumes. Resolve through the graph **while keeping the global
  fallback** so this unit is behavior-neutral (open-question #2 default). Make the
  resolver **one shared entry point invoked from both `qcc.cpp` and
  `lsp/Compile.cpp`** (REQ-007) — not two parallel constructions — so the Epic C
  seam is real; blangd stays single-file this epic but goes through the same
  component.
- **Done condition**: a `ctest` (the resolver's own unit test) exercises the
  resolver independent of the driver; a **`ResolverReuseTest` `ctest` constructs
  the resolver exactly as `lsp/Compile.cpp` does and asserts a fixture resolves
  identically to the `qcc` path**; both `qcc.cpp` and `lsp/Compile.cpp` call the
  single resolver entry point (grep-verifiable at both call sites); behavior
  unchanged (all prior gates green, `test_lsp.sh` green); the global fallback is
  retained (its removal is U6, not here).
- **Audit**: per constitution.
- **Budget hint**: 14–20 turns.
- **Speckit**: `037-module-graph-and-resolver`

### U5 — Foreign type references in `.bmod`; transitive build graph

- **Covers**: REQ-009
- **Depends on**: U1, U4
- **Work**: Give the `.bmod` format a way to reference a type **owned by another
  module, by identity, rendered through the reading module's qualifier** (it has
  none today) and to carry each module's **human-facing name** for diagnostics.
  Bump `BlangBmod::kFormatVersion` (4 → 5, `BmodFormat.h:57`) and confirm the salt
  reaches `BuildCache::computeKey` (Epic A wired it). Assemble the **transitive
  `.bmod` closure** (the build graph — every `.bmod` needed to typecheck a
  dependency, whether named or not) distinct from the direct **name graph**, so
  D7 use-capability holds for indirectly-reached types. Closes KI-5 (identity now
  carried in the interface). **Spike the sharpest corner first** (design record
  §Risks; design.md): a consumer calling a `pub` method that returns a **foreign
  generic** (`Box<T>`) from a module it **never imported** must monomorphize the
  body shipped in the `.bmod` and mangle the instance so U1's identity keeps it
  distinct without breaking the un-named instantiation. Identity (U1), mangling,
  and the build graph meet here — prove it with a fixture before generalizing.
- **Done condition**: a `test_build/` transitive fixture (`A → X → Q`; `A` uses a
  `Q` type via `X` without importing `Q`) builds and runs; **the un-named
  foreign-generic fixture — a binary calls a `pub` method returning `Box<T>` from
  a module it does NOT `import`, monomorphizes, links, and runs** — passes;
  cross-module generics still link; a cache-invalidation test proves the format
  bump invalidates warm entries; `.bmod` foreign refs parse standalone (SC-1
  style).
- **Audit**: per constitution; **security dimension mandatory** (the `.bmod` is
  parsed input — foreign-ref/malformed-interface handling) **and a Principle VI
  design-audit gate** on the `.bmod` foreign-ref format + transitive-closure model
  before implementation (it changes the interface format and parses untrusted
  input).
- **Budget hint**: 14–20 turns.
- **Speckit**: `038-bmod-foreign-refs-transitive-graph`

### U6 — Enforce import edges; use vs. name capability; close KI-23

- **Covers**: REQ-008, REQ-011
- **Depends on**: U3, U4, U5
- **Work**: Flip resolution to walk **only imported modules** (name-capability):
  using a symbol whose module is not imported is a located error; imported names
  are written qualified (`module.name`, D1). Enforce the **D7 split** — use-
  capability (receive/hold/pass/return a foreign type, call its `pub` methods)
  needs no import; name-capability (declare/annotate/store/construct) requires
  it. Remove the up-front global injection (`qcc.cpp:330-381`) — resolve through
  the graph (U4) now that the fallback can be dropped. Close **KI-23**: the
  combine-mode field/literal privacy rule keys on "**defined in a different
  module than the use site**" via U1 identity, not the `.bmod`-arrival-only
  `isFromInterface()` heuristic (`Sema.cpp:1069/1106/1512`). **Migrate the test
  corpus** onto real `import` lines (the bulk mechanical churn — budget for it;
  land it as a reviewable sweep, split into U6a enforcement / U6b corpus if the
  budget runs short).
- **Done condition**: `fail/sema` fixtures (unimported-symbol use; name-without-
  import) match `.expected` in both build modes; positive fixtures for use-
  without-import and qualified access; a KI-23 combine-mode field-privacy
  `fail/sema`/`fail/xmodule` fixture; the injection block is gone; full gates
  green.
- **Audit**: per constitution.
- **Budget hint**: 18–26 turns (largest; corpus churn). Split U6a/U6b if short.
- **Speckit**: `039-enforce-imports-and-capability`

### U7 — Collision & import diagnostics; deterministic display names

- **Covers**: REQ-010
- **Depends on**: U6
- **Work**: Located diagnostics through the `DiagnosticEngine` for **duplicate
  exported names** across imported modules, **unknown-module imports**, and
  **unused imports** (warning). Implement the **deterministic display-name
  renderer** (D3) with a stated tie-break rule (open-question #1) so a diagnostic
  can say *"add `collections` to your dependencies"* and render composed forms
  without them being writable syntax.
- **Done condition**: `fail/sema` fixtures with `.expected` patterns for each
  diagnostic class; the unused-import warning fixture; display-name rule
  documented and deterministic (pinned by fixture text); full gates green.
- **Audit**: per constitution.
- **Budget hint**: 10–16 turns.
- **Speckit**: `040-collision-and-import-diagnostics`

### U8 — Import aliasing `import x as y` (stretch)

- **Covers**: REQ-012
- **Depends on**: U6
- **Work**: Parse and bind `import x as y;` — the module binds to the local
  qualifier `y` (a module-local binding, D4). Re-export stays excluded (D8): `as`
  only renames the local qualifier, it never injects into a consumer's namespace.
- **Done condition**: a positive `test_build`/codegen fixture using `as`; a
  `fail/sema` negative proving re-export is unavailable; full gates green.
- **Audit**: per constitution.
- **Budget hint**: 6–10 turns.
- **Speckit**: `041-import-aliasing`

### U9 — Module search path / std separation (stretch)

- **Covers**: REQ-013
- **Depends on**: U3 (tiers/`cli`), U6
- **Work**: Replace the hardcoded `stdlib/<name>.b` mapping inside `bcc`/`qcc`
  with **configurable resolution roots** so a user module can deterministically
  shadow/replace a stdlib name (P7), with a defined precedence order.
- **Done condition**: a `test_build/` fixture with a custom resolution root
  resolves a user `timer` over stdlib `timer`; default behavior (no root
  configured) unchanged; full gates green.
- **Audit**: per constitution.
- **Budget hint**: 8–12 turns.
- **Speckit**: `042-module-search-path`

## Sequencing notes for the manager

- **U1 and U2 first, in parallel** — identity and the codegen crux are both
  foundational and independent. If U2's spike shows the double-free root cause is
  deeper than the ARC path, stop and raise before widening scope.
- **U2 strictly before U3's `cli` demotion** (review F3) — never demote a
  promoted module before its double-free is fixed.
- **U4 keeps the global fallback** so it's behavior-neutral; **U6 removes it**
  atomically with import enforcement (open-question #2).
- **U6 is the churn unit** — split U6a (enforcement + fixtures) / U6b (corpus
  migration) if its budget runs short rather than cutting fixtures. **If split,
  U7/U8/U9 depend on U6a (the enforcement mechanism), not U6b (the corpus
  sweep)** — diagnostics and the stretch features need enforcement in place, not
  every fixture migrated. U6b may land after them.
- **U8/U9 are stretch** — if the epic's budget is exhausted after U7, they may be
  deferred to a follow-on without failing the epic's core done-condition (the
  core is done conditions 1–8 + 11; 9–10 are the stretch bullets). Record any
  deferral in Known Issues.
- **Epic C (`modules-v2-lsp`)** must not start from these docs; it gets its own
  `/devbot-plan` after this epic completes, consuming U4's resolver seam.
