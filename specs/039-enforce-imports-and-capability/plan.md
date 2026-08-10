# U6 plan — enforce imports; use vs. name capability; collision diagnostics

**Epic**: modules-v2-graph · **Unit**: U6 (the convergence + corpus-churn unit).
**Covers**: REQ-008, REQ-011 · **Closes**: done-conditions 6 + 8 (interlocks DC9/KI-23).
**Depends on**: U3 (tiers, merged), U4 (resolver, merged), U5 (transitive graph, merged).

This is the **largest and riskiest** unit: it changes resolution *behavior* (not just
structure), so it carries the bulk of the test-corpus churn. Per the workplan it is
**split U6a / U6b** when budget is short — landed as separate PRs to a distinct
reviewer. **U7/U8/U9 depend on U6a (the mechanism), not U6b (the corpus sweep).**

## The behavior change (why it churns)

Today `.bmod` dependency symbols are **flat-merged** into `gScope` and called
**unqualified** (`add(3,4)`, `boxed_a(5)`, `get_box(7)` — verified across all 9
`test_build` consumers). U6 makes resolution go through the **import graph**:
- Using a symbol whose module is **not imported** is a **located error**.
- Imported names are written **qualified** — `module.name` (D1).
- The up-front global-symbol injection (now `injectBmodSymbols`, `qcc.cpp:281/659`;
  the design record's `qcc.cpp:330-381` block) is **removed atomically** with
  enforcement — resolution runs through the graph (U4's resolver + U5's closure).

So every cross-`.bmod` call site in the corpus must move to the qualified form
(`mathlib.add(3,4)`, `boxa.boxed_a(5)`, `midx.get_box(7)`). That is the churn.

## The codegen-naming crux (found during recon — solve this first)

Routing `.bmod` deps through namespaces (like stdlib) has a **codegen fork** the
stdlib path does not hit. Qualified access emits a prefixed callee name
(`QExpression.cpp:277`, `call->setMangledName(identName + "__" + memberName)`), which
is correct for a **combined stdlib** module (compiled in-process WITH the module
prefix — `sys.args` → `sys__args`). But a **`.bmod` dependency** is compiled in its
OWN prefix-free build, so `mathlib.a` exports plain `add`, not `mathlib__add`. So
`mathlib.add(3,4)` under the naive path emits a call to a symbol that does not exist
→ link error.

**Resolution:** at the qualified-access site, choose the callee name by provenance —
a `.bmod`-dep function is `isExtern()`/`isFromInterface()` (marked by
`injectBmodSymbols`), a combined-stdlib function is not:
- extern/`.bmod`-dep, non-generic → **unprefixed** `memberName` (links from the `.a`);
- combined stdlib, non-generic → **prefixed** `module__member` (as today);
- generic (either) → the monomorphized name from `mangleGenericName` (already
  prefix-free + U1-digest-keyed), so `setMangledName` is not the deciding factor.
This fork is the first thing U6a implements + tests (mathlib/myapp with qualified
`mathlib.add`/`mathlib.largest` must build+link+run), before routing/removal.

## U6a — import enforcement + injection removal (the mechanism)

1. **Remove `injectBmodSymbols`** (the flat merge). `.bmod` modules become
   **per-module export scopes** keyed by their local qualifier (the import name),
   registered as namespaces on the resolver — NOT merged into `gScope`.
2. **Qualified resolution for `.bmod` deps.** `module.name` resolves through the
   importing module's import table → the dep's export scope (like the existing
   namespaced-stdlib path, `QExpression.cpp:251` `isModuleImported`), now covering
   `.bmod` deps too. Unqualified use of a `.bmod` symbol → **located error** (its
   module is not in scope unqualified).
3. **`import` enforcement.** Using `module.name` without `import module;` → located
   error (`isModuleImported` already gates the namespaced path; extend to `.bmod`).
4. **Corpus migration (the sweep).** Migrate all 9 `test_build` consumers + any
   `test_files` combine fixtures that lean on the flat merge onto qualified calls +
   real `import` lines; update goldens. Prelude names (`Map`/`Set`/`Buffer`) and
   `cli` (still promoted, KG-6) stay unqualified — only true `.bmod`-dep symbols
   move. **Split point:** if budget runs short, U6a lands the mechanism + the
   `test_build` migration; the wider `test_files` sweep is U6b's tail.
5. **Fixtures:** `fail/sema` for use-without-import (both build modes);
   positive fixtures for qualified access.

## U6b — capability model (D7) + collision/import diagnostics

1. **D7 use vs. name.** *Use-capability* (receive/hold/pass/return a foreign type,
   call its `pub` methods) needs **no import** — the U5 transitive closure already
   grants it (the un-named foreign generic). *Name-capability* (declare a variable
   of it, annotate a param, store it in a struct, construct one) **requires** the
   import. Each direction gets a **positive AND negative** `fail/sema` fixture. (The
   `usebox` fixture's `Box<int> b = ...` becomes a name-capability case — it must
   either `import boxq;` or use inference; this is where U5's deferred D7 lands.)
2. **KI-23 (DC9 interlock).** Combine-mode namespaced-stdlib field/literal privacy
   re-keys on **module-of-definition via U1 identity** (not the `.bmod`-arrival-only
   `isFromInterface()` heuristic, `Sema.cpp:1069/1106/1512`) — closing the
   grep-gated gap with a `fail/sema`/`fail/xmodule` fixture.
3. **Collision & import diagnostics** through the `DiagnosticEngine`, each with an
   `.expected` pattern:
   - **duplicate exported names** across imported modules (was silent, P2);
   - **unknown-module import** (`import nonesuch;`);
   - **unused-import** (a **warning**).
   - **Deterministic display-name renderer** (D3) with the OQ#1 tie-break rule, so a
     diagnostic renders `module.name` deterministically (pinned by fixture text).

## Risk & sequencing

- **Atomic**: enforcement + injection removal must land together (a half-removed
  flat merge resolves nothing). No behavior-neutral intermediate — hence the churn
  is front-loaded and the gates re-baseline in the SAME PR.
- **Regression guard**: `test_build` (mathlib/myapp, boxa/boxb, boxq/midx/usebox,
  counterlib, etc.) must stay green post-migration; cross-module generics + the U5
  un-named-foreign path must still build/link/run.
- **Gates (both modes)**: `run_tests.sh`, `test_codegen.sh` + `--leak-check`,
  `test_lsp.sh`, `test_build/run_build_tests.sh` — all green, goldens updated for
  the deliberate call-site migrations (each called out in the PR).

## Status

**U6a — LANDED.** The mechanism is in: a `.bmod` dependency's free functions are
routed through a per-module namespace registered on `gScope` (out of the flat
merge), the codegen-naming provenance fork picks the emitted symbol
(generic/extern/combined-stdlib), unqualified dependency-function use is a located
error (`fail/xmodule/unqualified_import_call/`), and the free-function consumers
(`myapp`, `boxapp`, `usebox`, git-dep app) are migrated to qualified access — the
positive proof. Types stay in `gScope` as the documented U6a/U6b bridge (foreign
name-capability enforcement is U6b). Gates green both modes: `run_tests`
241/0 (LLVM) + 234/0 (parse-only), `test_codegen` 168/0, `--leak-check` Leaks:0,
`test_lsp` 63/0, `test_build` all pass.

**U6b-1 — LANDED (closes DC6).** The gScope type-injection bridge is retired:
`injectBmodSymbols` puts a dep's types/protocols in its per-module namespace, and
the `import` handler grants D7 **name-capability** by copying a *dependency*
namespace's type names into the importing scope (functions stay qualified-only;
stdlib namespaces are not copied — flagged via `grantsNameCapability`).
**Use-capability** rides on CodeGen's `mStructDefMap` and `var` inference (a Sema
fix: a `var` now takes its type from the initializer, which repaired a latent
struct/string mistyping and makes the un-named foreign generic real). The
foreign-type pre-scan and `BmodEmitter` were re-scoped off `gScope`. Fixtures both
directions: name pos (`counterapp`, transitive `run_build_tests.sh`) / neg
(`fail/xmodule/name_capability_requires_import`); use pos (`usebox`) / use-≠-name
neg (`run_build_tests.sh`). Gates green both modes: `run_tests` 242/0 + 235/0,
`test_codegen` 168/0, `--leak-check` Leaks:0, `test_lsp` 63/0, `test_build` all pass.

**U6b-2 — LANDED (closes DC8).** A whole-program import-diagnostics pass
(`qcc.cpp`, gated on an authoritative module set) reports unknown-module imports
(located error) and unused imports (warning, `-Werror` promotes) through the
`DiagnosticEngine`. Duplicate exported names across imported modules are handled by
unbinding the ambiguous name (importing both stays legal per D4/`boxapp`) with a
located error on bare use. D3 sharpening: an unqualified dependency function →
"did you mean 'lib.greet'?"; an unimported foreign type → "did you mean to
`import lib;`?"; the `Failed parse varible` typo is fixed. Fixtures:
`fail/xmodule/{unqualified_import_call,unknown_module_import}` + `run_build_tests.sh`
DC8 checks. Gates green both modes: `run_tests` 243/0 + 236/0, `test_codegen`
168/0, `--leak-check` Leaks:0, `test_lsp` 63/0, `test_build` all pass.

**U6b-3 — LANDED (closes DC9/KI-23).** `Sema::resolveFieldAccess` field privacy now
keys on **module identity** (`imported = isFromInterface() || crossModule`, where
`crossModule` is the struct's defining-file basename ≠ the use-site module's), so a
combine-mode reach-in into a namespaced-stdlib struct's private field is a located
Sema error in all build modes — the grep gate is no longer the only guard. Uses the
file basename (not the U1 digest, which collapses to one project origin in a combine
build). `Sema::analyze` already took the `Module*`, so `lsp/Compile.cpp` is unchanged
(blangd single-file → DC9 inert, `test_lsp` green). The corpus's one white-box
reach-in (`codegen_map_hashed.b`) moved to a new `pub Map.bucket_count()`. Fixtures:
`run_build_tests.sh` DC9 checks (reach-in rejected + located; pub-accessor positive).
Gates green both modes: `run_tests` 243/0 + 236/0, `test_codegen` 168/0,
`--leak-check` Leaks:0, `test_lsp` 63/0, `test_build` all pass.

**Epic core complete:** DC6, DC8, DC9 all closed by U6b-1/-2/-3. Stretch DC10/DC11
(U8/U9 — import aliasing, module search path) remain, to be scoped against budget.
