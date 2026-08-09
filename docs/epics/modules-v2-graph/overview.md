# Epic: modules-v2-graph — A real import graph, module identity, and principled type tiers

**Archetype**: evolve (replaces the resolution model under the existing module
surface; **Epic B** of the modules-v2 split)

**Status**: launched — run `3e3dbafe-0cd0-4838-bb98-10d3a17fedc5` launched 2026-08-09 (full scope U1–U9; may run in scoped passes)

**Owner**: Ben Payne

**Created**: 2026-08-09 · **Updated**: 2026-08-09

**Name**: kept as `modules-v2-graph` (not NNN-numbered) because the binding
design record and Epic A's docs reference Epic B by exactly this name.

**Source documents**:
[../modules-v2/overview.md](../modules-v2/overview.md) — the shared design
record; its decisions **D1–D17 are binding requirements** and its pain points
**P1–P11** are the problems this epic closes (read it first).
[../modules-v2/review-2026-08-04.md](../modules-v2/review-2026-08-04.md) — the
readiness review (findings F1–F10; F3 sets the codegen-fix-before-cli-demotion
resequencing this epic honors).
[../modules-v2-exports/overview.md](../modules-v2-exports/overview.md) — Epic A
(**complete**): it built the seams this epic stands on (`.bmod` format v4, the
`isFromInterface` ABI flag, and the shared `stampDefiningOrigin()` /
`createGlobalScope()` reused by both `qcc` and `blangd`) and filed the four
known-issues this epic inherits (KI-3, KI-5, KI-16, KI-23).

## Why

Epic A settled **what** crosses a module boundary (opaque types: methods are
the API, fields never cross). Epic B settles **by what rule** names resolve —
the resolution half the design record calls the other side of the same problem.

Today resolution is a **flat merge**: before any file's imports are parsed,
every public symbol from every `.bmod` dependency is injected into one
process-global scope (`qcc.cpp:330-381`). So `import` is advisory (P1),
collisions are silent (P2, the second `addSymbol` just returns false), qualified
access is held together by a hardcoded `buffer`/`collections`/`cli` promotion
list (`qcc.cpp:308-309`, P3) that exists only to dodge a module-prefix
string-ARC double-free (`qcc.cpp:303-307`), generic mangling ignores the
defining module so two same-named exported types would silently collapse onto
one `linkonce_odr` symbol (`CGTypes.cpp:11-21`, P10 — a latent miscompile), and
the type system has **seven** accidental tiers with no stated rule (P11),
including bare-name `addType` registrations that have no definition behind them
(`QModule.cpp:582-584`, degrading diagnostics, D14).

This epic replaces that with a per-module import graph keyed on a **canonical
module identity**, enforced imports with located diagnostics, and **three**
principled type tiers (core/prelude/library). It fixes the codegen root cause so
the special cases can be retired, and it extracts resolution into a reusable
component — the clean seam that **Epic C (`modules-v2-lsp`)** consumes to deliver
cross-module editor features.

## Scope decisions (owner, 2026-08-09)

- **LSP split off.** Cross-module go-to-definition / hover / diagnostics in
  blangd is **Epic C (`modules-v2-lsp`)**, planned after this epic. Epic B builds
  the resolution model as an **extractable component reusable by blangd** (a
  named seam, not inline in `qcc.cpp`) and keeps `test_lsp.sh` green, but does
  **not** wire blangd to resolve across modules. (Original design-record unit 11
  → Epic C.)
- **Both stretch units are IN scope**: import aliasing (`import x as y`, U8) and
  a configurable module search path / std separation (U9). Re-export stays
  excluded (D8).
- **Oversight**: fully autonomous; PR + distinct-secondary-reviewer per unit
  (constitution audit pattern); Principle VI design-audit gates named on the two
  crux units — U1 (canonical identity, the keystone artifact) and U2 (the
  module-prefix codegen fix). Generous limits (see `manifest.yaml`); the epic may
  be executed in scoped passes across several runs, as Epic A was.

## Done condition (epic level)

All of the following hold on `master` after the epic's PRs merge, each backed by
a committed test that CI runs:

1. **Module identity keys mangling — no `linkonce_odr` collapse.** A
   `test_build/` fixture with two path-dependency modules that each export a
   same-named generic type (e.g. `Box<int>`), both instantiated by one binary,
   builds/links/runs with the two treated as **distinct types with distinct
   mangled symbols** (verified by `nm`/IR inspection in the fixture's script) —
   the P10 miscompile is gone.
2. **Three declared type tiers.** The hardcoded `isGlobalTypeLib` promotion list
   (`qcc.cpp:308-309`) is replaced by a declared, compiler-shipped **prelude
   manifest** whose membership is exactly `{Map, Set, Buffer}` (types only); the
   bare-name `addType` registrations with no definition behind them
   (`QModule.cpp:582-584`) are **deleted** (a program using such a name with no
   backing definition now fails at the *type*, proven by a `fail/sema` fixture
   asserting the located error is at the type, not a later symbol lookup); and
   `cli` is an ordinary qualified library module (`cli.has_flag`-style access),
   not promoted. Because `stdlib/collections.b` is a **mixed module** — it defines
   the prelude types `Map`/`Set` *and* the free function `sort` — the manifest
   assigns tiers **per name** (types → prelude; `sort` → qualified
   `collections.sort`), and the bare `sort(...)` call sites (e.g.
   `examples/wordfreq/main.b`) are migrated to `collections.sort(...)` with their
   goldens. `run_tests.sh`/`test_codegen.sh` green in both modes.
3. **Module-prefix codegen fixed; special cases retired; `--leak-check` clean.**
   The string-ARC double-free under namespaced-module codegen
   (`qcc.cpp:303-307` rationale) is fixed; the `buffer`/`collections`/`cli`
   global-scope promotions are removed and namespacing is uniform; a
   `test_codegen`/`test_build` fixture exercises a namespaced module's internal
   string-returning call path and is `--leak-check` clean (**KI-3 closed**).
4. **Per-module graph via a standalone resolver (behavior-neutral).** Resolution
   runs through per-module export scopes connected by import edges, implemented as
   a **standalone resolver component** (a named class, not inline in `qcc.cpp`
   `main()`), exercised by a dedicated `ctest` independent of the driver. This
   unit keeps the global fallback so it is **behavior-neutral** — all prior gates
   stay green with no behavior change. (Removal of the global injection block is
   done-condition 6, at import enforcement.)
5. **The resolver is the single shared entry point (Epic C seam).** One resolver
   entry point is invoked from **both** `qcc.cpp` and `lsp/Compile.cpp` (both call
   sites grep-verifiable), and a `ResolverReuseTest` `ctest` constructs the
   resolver exactly as `lsp/Compile.cpp` does and asserts a fixture resolves
   identically to the `qcc` path — so Epic C wires cross-module resolution without
   re-plumbing. (blangd stays single-file this epic.)
6. **Imports are enforced; use vs. name capability (D7); injection removed.**
   Using a symbol whose module is not imported is a located `file:line:col:
   error:` (`fail/sema` fixtures, both build modes); imported names are written
   qualified (`module.name`, D1); **use-capability** (receiving/holding/passing/
   returning a foreign type and calling its `pub` methods) needs no import, while
   **name-capability** (declaring a variable of it, annotating a param, storing
   it in a struct, constructing one) requires the import — each direction has a
   positive and a negative fixture. The up-front global-symbol injection block
   (`qcc.cpp:330-381`) is **removed** — resolution now goes through the graph.
7. **Foreign type references in `.bmod`; transitive build graph; the un-named
   generic path.** The `.bmod` format references a type owned by another module
   **by identity, rendered through the reading module's qualifier** (it has no
   such mechanism today), and carries each module's human-facing name for
   diagnostics; the transitive `.bmod` closure is assembled so use-capability
   holds for indirectly-reached types. Two `test_build/` fixtures: (a) a
   transitive dependency (`A → X → Q`, `A` uses a `Q` type via `X` without
   importing `Q`) builds and runs; **(b) the sharpest corner — a binary calls a
   `pub` method returning a foreign generic (`Box<T>`) from a module it does NOT
   `import`, monomorphizes the body shipped in the `.bmod`, links, and runs**
   (D7 use-capability across an un-named module). Cross-module generics still
   build and link (format bump 4→5; cache invalidation proven).
8. **Collision & import diagnostics.** Duplicate exported names across imported
   modules, unknown-module imports, and unused imports each emit a located
   diagnostic through the `DiagnosticEngine` (unused-import is a warning);
   `fail/sema` fixtures with `.expected` patterns, deterministic display-name
   rendering (D3).
9. **Combine-mode field privacy is Sema-enforced (KI-23 closed).** A consumer
   reaching into a namespaced-stdlib struct's private field in combine mode is a
   located Sema error (the rule keys on "defined in a different module than the
   use site," using module identity — not the `.bmod`-arrival-only
   `isFromInterface()` heuristic), proven by a `fail/xmodule` or `fail/sema`
   fixture; the reach-in grep gate stops being the only guard.
10. **Import aliasing (stretch).** `import x as y;` binds the module to the local
   qualifier `y`; a positive `test_build`/codegen fixture and a `fail/sema`
   negative (re-export still unavailable, D8).
11. **Module search path / std separation (stretch).** The hardcoded
    `stdlib/<name>.b` mapping is replaced by configurable resolution roots so a
    user module can shadow/replace a stdlib name deterministically; a
    `test_build` fixture with a custom resolution root resolves a user `timer`
    over stdlib `timer` (P7).
12. **No regressions; docs updated.** `./run_tests.sh` and `./test_codegen.sh`
    fully green in both build modes; `./test_codegen.sh --leak-check` clean;
    `./test_lsp.sh` green (blangd untouched beyond keeping it building against the
    extracted resolver); `test_build/run_build_tests.sh` green; `.bmod` +
    git/path deps + cross-module generics still build and link end to end; docs
    updated (`docs/language_design.md` + `CLAUDE.md`) per Principle I; GitHub CI
    green on `master`.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | Canonical module identity: resolved origin (`realpath` for path deps, `url@pin` for git, D5); a short SHA-256 digest of a portable origin string is the serialized form used in mangled symbols, `.bmod` foreign refs, and shareable cache keys (open-question #2 answer); type identity is nominal and owned by the defining module (D2); no dedup across distinct origins (D6) | P1 | Done cond. 1 |
| REQ-002 | Generic symbol mangling incorporates module identity (D10) so two same-named exported types get distinct symbols — the P10 `linkonce_odr` miscompile is eliminated | P1 | Done cond. 1 |
| REQ-003 | Three type tiers core/prelude/library with a declared, fixed, compiler-shipped prelude manifest `{Map, Set, Buffer}` (types only), users/libraries cannot extend it (D12/D13); `cli` demoted to a qualified library module | P1 | Done cond. 2 |
| REQ-004 | Every name in scope has a definition behind it; bare-name `addType` registration deleted (D14) — undefined names fail at the type with a better diagnostic | P1 | Done cond. 2 fixture |
| REQ-005 | The module-prefix string-ARC double-free is fixed; the `buffer`/`collections`/`cli` global-scope promotions are retired; namespacing is uniform; `--leak-check` clean (P3; closes KI-3) | P1 | Done cond. 3 |
| REQ-006 | Resolution runs through a per-module export-scope graph with import edges, implemented as a standalone reusable resolver component (behavior-neutral, global fallback retained at U4) (P1/P5) | P1 | Done cond. 4 + unit test |
| REQ-007 | The resolver component is constructed by both `qcc` and `blangd` (the Epic C seam) without driver-only coupling, proven by a `ResolverReuseTest` | P2 | Done cond. 5 |
| REQ-008 | Using a symbol whose module is not imported is a located error; imported names are qualified (D1); use-capability vs name-capability split enforced (D7); the up-front global-symbol injection is removed | P1 | Done cond. 6 fixtures |
| REQ-009 | `.bmod` references foreign types by identity rendered through the reader's qualifier and carries each module's human name; the transitive build graph grants use-capability for indirectly-reached types, incl. the un-named foreign-generic instantiation path; cross-module generics still link (format bump + cache-invalidation test) | P1 | Done cond. 7 |
| REQ-010 | Located diagnostics for duplicate exported names, unknown-module imports, and unused imports (warning); deterministic display names (D3, P2) | P1 | Done cond. 8 fixtures |
| REQ-011 | Combine-mode namespaced-stdlib field/literal privacy is Sema-enforced via module identity, not grep-gated (closes KI-23) | P2 | Done cond. 9 fixture |
| REQ-012 | Import aliasing `import x as y` (stretch); re-export remains excluded (D8) | P3 | Done cond. 10 |
| REQ-013 | Configurable module search path / std separation replacing hardcoded `stdlib/<name>.b` (stretch, P7) | P3 | Done cond. 11 |
| REQ-014 | No regressions across all prior gates; `.bmod`/deps/cross-module generics build & link; docs updated (Principle I); CI green | P1 | Done cond. 12 |

## Non-goals

- **Cross-module LSP** — go-to-definition, hover, and diagnostics across
  modules in blangd is **Epic C (`modules-v2-lsp`)**. This epic only keeps
  blangd building against the extracted resolver and `test_lsp.sh` green.
- **LSP completion** — a separate language-server concern (design record).
- **Nested module *naming*** — qualifiers stay one level deep (`std.io` is one
  flat name); per-module isolation is a goal, deep-path naming is not (D-record
  non-goal).
- **Type aliases** (D11) and **re-export in any form** (D8) — both would
  reintroduce global-uniqueness requirements.
- **A package registry, version-solving, or dedup of the same library reached
  by two paths** (D6) — two origins are two modules.
- **Any change to ARC, ownership, or the concurrency model** beyond the
  module-prefix codegen *fix* (REQ-005), which repairs an existing double-free
  rather than changing ARC semantics.
- **Anything Epic A already delivered** — the opaque-export model, `pub` on impl
  members, private fields, the construction-ABI factory, `.bmod` method
  signatures, D15/D16/D17 emission. This epic does not revisit them.

## Inherited known-issues (from Epic A, this epic owns)

| KI | What | Closed by |
|----|------|-----------|
| KI-3 | `buffer`/`collections`/`cli` exempt from module-private enforcement (promoted into user scope; no boundary). Root cause is the double-free at `qcc.cpp:303-307`. | U2 (codegen fix) + U3 (tiers/demote) + U6 (per-module enforcement) |
| KI-5 | Factory symbol prefix-free on the consumer side (`__Socket_new`) — the defining module's prefix isn't carried in the `.bmod`. | U1 (identity) + U5 (foreign refs carry identity) |
| KI-16 | `mDefiningFile` holds a source base name with no file→module mapping. | U1 (identity) + U4 (module nodes) |
| KI-23 | Combine-mode namespaced-stdlib field privacy is grep-gated, not Sema-enforced (rule keys on `.bmod`-arrival only). | U6 (rule keys on module-of-definition via identity) |

## Companion documents

| File | Purpose |
|------|---------|
| [workplan.md](workplan.md) | units U1–U9, dependency map, per-unit done conditions, budgets |
| [design.md](design.md) | design spec: seams (current file:line anchors), contracts, epic-local decisions |
| [evaluation.md](evaluation.md) | harnesses, audit plan, regression baseline |
| manifest.yaml | machine-readable run definition |
| [../modules-v2/overview.md](../modules-v2/overview.md) | shared design record (D1–D17, P1–P11) — binding |

## Constraints & context for the manager

- Constitution applies: `.specify/memory/constitution.md` (v1.2.0). Principle VI
  (design-audit-before-implementation) is **named on U1 and U2**; every unit
  gets a speckit spec + spec audit + code review + a distinct secondary-reviewer
  PR merge.
- The decision record **D1–D17** in `../modules-v2/overview.md` is **binding** —
  not to be relitigated; raise a question if new information seems to invalidate
  one. The two open questions there are already answered (prelude = `{Map, Set,
  Buffer}` types-only; identity = `realpath` in-process + short SHA-256 of a
  portable origin string when serialized).
- **U1 (identity) is the keystone** — type identity, mangling (P10), and dedup
  (D6) collapse into it; build it once, early, key everything on it.
- **U2 (module-prefix codegen fix) is a spike-first crux** and is **security-
  dimension mandatory** (ARC/double-free). It must land **before** U3 retires the
  `cli` promotion (review F3) — demoting `cli` before the fix would ship the
  known double-free. `--leak-check` gates it.
- **Do NOT touch** the Epic-A export model (opaque types, factory, `.bmod`
  signature emission), ARC semantics, ownership, or the concurrency model.
- The **`.bmod` format changes shape** (foreign refs, module names): bump
  `BlangBmod::kFormatVersion` (currently **4**, `BmodFormat.h:57`) and confirm
  the change flows into `BuildCache::computeKey` — Epic A already wired the salt.
- Danger zones: `qcc.cpp` driver loop (flat-merge injection at 330-381, promotion
  list at 308-309, double-free rationale at 303-307); `CGTypes.cpp:11-21`
  (`mangleGenericName`); `QModule.cpp:582-628` (`createGlobalScope` tiers/builtins
  — mind the KEEP-IN-SYNC contract with `BmodEmitter` at `QModule.cpp:607-613`);
  `QExpression.cpp:251` (namespaced-access gate). Keep `lsp/Compile.cpp` building.
- Large mechanical **test-corpus churn** is expected at U6 (import enforcement);
  budget for it, it is reviewable.
- All work lands via PR with a distinct reviewer hire; no direct commits to
  `master`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| 1 | Exact display-name tie-break rule (D3) when a foreign type is reachable by several routes — which qualifier renders in a diagnostic? Needs a stated deterministic rule (LSP goldens in Epic C will pin it). | U7 | open — architect proposes a deterministic rule in U7's speckit spec; manager approves against D3; raise to owner only if it forces a D-record change | |
| 2 | Whether retiring the global-symbol injection (U4/U6) can stay behavior-neutral behind a fallback, or must flip atomically with import enforcement (U6). Sequencing detail for the architect. | U4/U6 | open — architect resolves in U4/U6 specs; the design record's "resolve through the graph while keeping the global fallback — behavior-neutral" (record §Proposed work units, unit 7) is the default | |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-08-09 | 3e3dbafe-0cd0-4838-bb98-10d3a17fedc5 | launched | Full-scope launch (U1–U9). endpoint `http://localhost:8201`, dir `a6b2f628-…`; fully_autonomous, 160 turns / 36h / 16M tokens, no_progress_threshold 10. May run in scoped passes (~U1–U4/U5 per pass). |
| 2026-08-09 | — | review passed | `/devbot-review` + fresh-context audit (no blockers). Applied F1–F9: `sort`/`collections.b` mixed-file split + call-site migration owned by U3 (F1); injection-removal moved from done-cond 4 to done-cond 6/U6 (F2); un-named foreign-generic spike + fixture added to U5 — the design record's "sharpest corner" (F3); done-cond 5 concretized to a `ResolverReuseTest` + both-call-sites grep (F4); `cli.has_flag` example fix (F5); budget-hint denomination clarified (F6); U6a/U6b downstream deps stated (F7); U5 design-audit gate added (F8); done conditions renumbered 1–12, `4b` promoted to `5` (F9). Done-condition sync verbatim; traceability REQ-001..014 complete. Status → ready. Next: `/devbot-launch modules-v2-graph`. |
| 2026-08-09 | — | epic planned | Planned after Epic A (`modules-v2-exports`) completed + CI-green. Owner decisions: LSP split to Epic C; both stretch units in scope; fully-autonomous/generous. Recon verified current seams at `master` `125fb0f`. Next: `/devbot-review modules-v2-graph`. |
