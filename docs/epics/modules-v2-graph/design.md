# Design spec: modules-v2-graph

**Epic**: [overview.md](overview.md) · **Archetype**: evolve · **Constitution**:
`.specify/memory/constitution.md` (v1.2.0)

This is a product/design-level spec. Deep engineering design is each unit's
speckit `plan.md` at run time (Principle VI: U1 and U2 carry a design-audit gate
*before* implementation). The decision record **D1–D17** in
[../modules-v2/overview.md](../modules-v2/overview.md) is binding; this spec does
not restate it, only grounds it in current code.

## Context — the seams, at `master` `125fb0f` (post-Epic-A)

Verified by recon 2026-08-09. Epic A moved several anchors from the design
record's `7e75352` baseline; these are current.

| Seam | Current location | Notes |
|------|------------------|-------|
| Flat-merge injection | `qcc.cpp:330-381` | Inline block in `main()`'s file loop (NOT a function). Injects every `isPublic()` symbol from `bmodMap` into process-global `gScope`: functions `:344-360`, structs `:361-370` (+ `addType`), enums `:371-378`, protocols `:379-384`; `bmodMap.clear()` guards inject-once. **U6 removes this.** |
| Namespaced-access gate | `QExpression.cpp:251` | `scope->isModuleImported(identName)` (defined `Type.h:214-220`, over `mImportedModules`). The qualified-access path (`sys.args`, `net.X {}`). |
| Bare-name `addType` (no def) | `QModule.cpp:582-584` | `Task`/`Array`/`Buffer` registered as names with nothing behind them. **U3 deletes (D14).** |
| Promotion list | `qcc.cpp:308-309` | `isGlobalTypeLib = buffer|collections|cli`; used `:310-312` to parse them into the user scope. **U3 replaces with a prelude manifest; U2 unblocks demoting `cli`.** |
| Module-prefix double-free rationale | `qcc.cpp:303-307` | The comment explaining why `cli` is promoted (string-ARC double-free in namespaced codegen). **U2 fixes the root cause.** |
| Core builtins | `QModule.cpp:585-628` | `print`/`println`/`to_json`, `Printable`, `Option`/`Result`. KEEP-IN-SYNC with `BmodEmitter` at `QModule.cpp:607-613`. **U3's `core` tier.** |
| Generic mangling | `CGTypes.cpp:11-21` (`mangleGenericName`) | Pure `baseName + "_" + argName`; **no module identity** → P10. `linkonce_odr` emission at `CGTypes.cpp:348/497`, `BmodEmitter.cpp:185`. **U1/U2 add identity.** |
| Struct factory name | `mangleStructFactoryName` | Carries a codegen module prefix on emit but is **prefix-free on the consumer side** (KI-5). **U5 carries identity into the interface.** |
| `.bmod` format version | `BmodFormat.h:57` = **4** | Marker emitted `BmodEmitter.cpp:541`; validated on read `qcc.cpp:433/443`; salted into cache key `BuildCache.cpp:128`. **U5 bumps to 5.** |
| ABI provenance flag | `isFromInterface`/`mFromInterface` `Type.h:456-457/514`; set `qcc.cpp:463` | Contract (`Type.h:452-482`): ABI-only, **must not be overloaded for visibility**. Sema privacy rules currently key on it (`Sema.cpp:1069/1106/1512`) — **U6 re-keys on module identity to close KI-23.** |
| Defining-origin marker | `mDefiningFile` `Type.h:495-496/516`; `stampDefiningOrigin()` `QModule.cpp:636-660` | Shared by `qcc` (`qcc.cpp`) and `blangd` (`lsp/Compile.cpp:49`). Holds a source base name, **no file→module map** (KI-16). **U1 gives it the mapping.** |
| Pass structure | `Lexer → Parser → Sema → CodeGen` | Resolution is **entirely inline in `qcc.cpp` `main()`**; the only shared/extracted pieces are `createGlobalScope()` and `stampDefiningOrigin()` (both `QModule.cpp`). **No resolver component exists — U4 creates one.** |
| blangd | `lsp/` (`Compile.cpp:13-56`) | Single-file; builds its own `gScope` + file scope, `Module::Parse` + `stampDefiningOrigin` + `Sema::analyze`; **no `bmodMap`, no CodeGen, no cross-module resolution.** Harness `test_lsp.sh`, goldens `test_files/lsp/*.lsp.jsonl`. Epic B keeps it building against the extracted resolver; Epic C wires it. |

## Design principles (epic-local, grounding the D-record)

1. **Identity first, keyed everywhere (D2/D5/D6/D10).** One artifact — the
   canonical module identity — resolves type identity, mangling, and dedup. The
   in-process graph identity is `realpath`/`url@pin`; the *serialized* identity
   (mangled symbols, `.bmod` foreign refs, shareable cache keys) is a short
   SHA-256 digest of a **portable** origin string (never a raw absolute path).
   This is open-question #2's layered answer, already accepted.

2. **Two graphs, never conflated (D-record §Identity).** The **build graph** is
   the transitive `.bmod` closure (grants D7 use-capability); the **name graph**
   is direct import declarations only (grants naming/construction). U5 builds the
   first; U6 enforces the second.

3. **One qualified-access rule (D1/D3/D4).** Imported names are `module.name`;
   qualifiers are module-local bindings that need not be globally unique;
   composed forms (`A.collections.Map`) are display-only, never writable syntax.

4. **Retire, don't accumulate (D12/D13/D14, P3/P11).** Seven accidental tiers →
   three declared ones. The prelude is a fixed compiler-shipped list
   (`{Map, Set, Buffer}`, types only) that nothing can extend — the bright line
   against decay back to the flat merge. Every in-scope name has a definition
   behind it.

5. **Fix the root cause before removing the workaround (F3).** The promotions
   exist *because* of the double-free; U2 lands and is `--leak-check`-proven
   before U3 demotes `cli`.

6. **Extract the resolver as a real seam (P4/P5).** Resolution becomes a
   standalone component both `qcc` and `blangd` construct — behavior-neutral this
   epic, load-bearing for Epic C. This is the single most important structural
   move; it is what makes cross-module editor features possible without
   re-plumbing.

## What must not break (regression guard)

- Same-module program behavior; existing codegen goldens (except tests
  deliberately migrated onto real `import` lines at U6 — each such golden/fixture
  change called out in its PR).
- The Epic-A export model: opaque types, `pub` impl members, private fields, the
  construction-ABI factory, `.bmod` method-signature + conformance + D15/D16/D17
  emission.
- Cross-module generic instantiation; `.bmod` + content-addressed caching +
  git/path deps building and linking end to end.
- `blangd` keeps building and `test_lsp.sh` stays green (blangd is not
  functionally changed this epic beyond constructing the extracted resolver).
- Quiet clean compiles; the single-located-diagnostic format; ARC/ownership/
  concurrency semantics (U2 *repairs* an existing double-free, it does not change
  ARC rules).

## Risks (from the design record, still live)

- **The codegen fix is a crux** (U2). The special cases exist because the
  module-prefix string-ARC path is buggy; U2 must land with `--leak-check` proof
  or uniform namespacing (U3) is blocked.
- **Generic instantiation across an un-named module** (D7) is the sharpest
  corner — a consumer monomorphizes bodies from a module it never imported.
  Bodies ship in the `.bmod` and instances are `linkonce_odr`; U1's identity must
  make those symbols distinct without breaking the un-named instantiation. Spike
  in U5.
- **Field-driven codegen needs metadata users can't see** — `table`/`@json`
  already resolved via Epic A's D15 metadata; U6's re-keying of field privacy
  must not regress it.
- **Corpus churn at U6** — enforcing imports rewrites many fixtures; large,
  mechanical, reviewable; split U6a/U6b if needed.
- **`.bmod` reshapes** (U5) — invalidates the content-addressed cache; the
  format-version salt (Epic A) must catch it.
- **Scope creep toward a package manager** — dedup across origins, version
  solving, and a registry are explicit non-goals; hold the line (D6).
