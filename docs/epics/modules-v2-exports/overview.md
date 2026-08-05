# Epic: modules-v2-exports — Opaque exports: methods are the API, fields never cross

**Archetype**: evolve (inverts the export model under the existing module
surface; Epic A of the modules-v2 split)

**Status**: launched

**Owner**: Ben Payne

**Created**: 2026-08-04 · **Updated**: 2026-08-04

**Source documents**:
[../modules-v2/overview.md](../modules-v2/overview.md) — the shared design
record whose decisions **D1–D17 are binding requirements** for this epic
(read it first; it carries the reproductions, pain points P8/P9, and the
rationale). [../modules-v2/review-2026-08-04.md](../modules-v2/review-2026-08-04.md)
— the readiness review whose findings F1/F2/F4/F5/F6/F8 this epic's units
implement.

## Why

Cross-module struct use in BLang today is exactly backwards: a `pub struct`
ships its full field layout into the `.bmod` interface but **none of its
methods** — a consumer can reach into an imported struct's internals but
cannot construct it (`Counter(5)` fails to parse) or call anything on it
(`c.bump()` → `type 'Counter' has no method 'bump'`). And an exported type
whose signature references a private type emits a `.bmod` that doesn't parse,
failing at the *consumer's* build inside a generated file (design record
P8, P9 — both reproduced).

This epic inverts that: methods and `init` become the API (explicit `pub`),
member variables become always-private, struct literals become module-private,
and the `.bmod` becomes a true interface. The keystone constraint (review F1)
is that today's construction ABI is caller-allocating — the consumer computes
struct size and generates the destructor from the field list — so the epic
leads with a construction-ABI change: cross-module construction through a
library-emitted factory. Data-contract structs (`table`, `@json`) keep working
cross-module via compiler-facing field metadata that source can never name
(D15).

Epic B (`modules-v2-graph` — module identity, import enforcement, type tiers,
cross-module LSP) builds on the export model this epic settles. This epic
touches emission, codegen, Sema, and the stdlib API — **not** the resolution
path (the flat merge stays until Epic B).

## Done condition (epic level)

All of the following hold on `master` after the epic's PRs merge, each backed
by a committed test that CI runs:

1. A `test_build/` fixture pair (lib + bin across a `.bmod` boundary) where
   the bin does `Counter c = Counter(5);` and `c.bump()` against the lib's
   non-generic `pub struct` — builds and runs with expected output via
   `test_build/run_build_tests.sh`.
2. A struct literal for an imported type (`Counter { count: 5 }`) and a field
   access on an imported type (`c.count`) are each a located
   `file:line:col: error:` at the consumer build — `fail/xmodule/`
   lib+consumer fixture pairs with `.expected` patterns, green in both build
   modes via `run_tests.sh`.
3. `pub` parses on methods and `init` in `impl` blocks; a method without
   `pub` is unreachable from another module (`fail/xmodule/` fixture) and
   callable inside its own module (positive fixture).
4. A non-`pub` type in an exported signature — function param/return, method
   signature, or enum variant payload — is a located error at the **library**
   build (`fail/sema` fixtures with `.expected` patterns), including the
   design record's P9 reproduction (a `pub struct` referencing a non-`pub`
   type) — the scenario that previously produced a consumer-side
   generated-file syntax error.
5. `print("{}", x)` on an imported struct that `impl Printable` works
   end-to-end — a `test_build/` lib+bin fixture with an exact-output check in
   `test_build/run_build_tests.sh` — conformance records cross the `.bmod`
   (D16).
6. From a consumer module, `query T |> where { .field == ... }` and
   `to_json(x)` on an imported `@json table struct` compile and run — a new
   sqlite-backed lib+bin fixture in `test_build/` — while the same consumer
   naming that field in ordinary source is a located error (`fail/xmodule/`
   fixture) — D15 both ways.
7. `BuildCache` keys incorporate a `.bmod` format version: a test proves a
   format-version bump invalidates a warm cache entry.
8. `tools/check_no_field_reachins.sh examples/ test_build/` exits 0 (the
   committed script greps a maintained list of opaque stdlib/imported-struct
   fields), and all examples compile and their integration scripts pass
   using the new accessor/method surface.
9. `./run_tests.sh` and `./test_codegen.sh` fully green in both build modes;
   `./test_codegen.sh --leak-check` clean; `./test_lsp.sh` green;
   `test_build/run_build_tests.sh` green; docs updated
   (`docs/language_design.md` + `CLAUDE.md`) per Principle I.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | Cross-module construction works without consumer-side layout knowledge: library-emitted factory (`StructName_new`-style) wrapping alloc-with-dtor + init; consumer release via `__blang_rc_release` using the stored dtor pointer (review F1) | P1 | Done cond. 1; ASan/`--leak-check` clean on the fixture |
| REQ-002 | An imported non-generic `pub struct` is constructible (`Counter(5)`) and its `pub` methods callable (`c.bump()`) across a `.bmod` boundary (P8) | P1 | Done cond. 1 |
| REQ-003 | `pub` is parsed on methods and `init` in `impl` blocks; unmarked methods/`init` are module-private (D9) | P1 | Done cond. 3 fixtures |
| REQ-004 | Member variables are always private; struct literals are module-private; field access and struct literals on imported types are located errors (D9, D7) | P1 | Done cond. 2 fixtures |
| REQ-005 | The `.bmod` is a true interface: exported names + method signatures; field layout not source-resolvable, retained only as compiler-facing metadata where D15/ABI requires. Generic structs exempt from emission filtering (full layout + all bodies keep shipping; visibility enforced in Sema) | P1 | Done cond. 2 + 6; `.bmod` content inspected by a build test; existing cross-module generic tests stay green |
| REQ-006 | A private type in an exported signature (incl. enum variant payloads, D17) is a located error at the library build (P9) | P1 | Done cond. 4 fixtures |
| REQ-007 | Protocol conformance records cross the boundary: Printable dispatch, generic constraint checks, and `impl ForeignProtocol for MyStruct` work cross-module (D16) | P2 | Done cond. 5 E2E test |
| REQ-008 | Imported `@json`/`table` structs remain serializable/queryable from consumer modules via compiler-facing metadata; generated-function access has one canonical spelling (D15) | P1 | Done cond. 6 fixtures |
| REQ-009 | Build-cache correctness across `.bmod` format changes: format-version salt in `BuildCache::computeKey` (review F6) | P2 | Done cond. 7 test |
| REQ-010 | stdlib net/fs public API redesigned for opaque types (accessors/methods for `HttpRequest`, `HttpRequestLine`, `HttpParsedHeaders`, `FileInfo`, etc.); `Map`/`Set` gain `pub init` (replacing struct-literal construction at call sites); examples + affected codegen goldens migrated (review F8, audit AF-1) | P1 | Done cond. 8; examples' integration tests; migrated goldens green |
| REQ-011 | Enum export semantics per D17: variants/payloads ship in `.bmod`; matching foreign enums works cross-module | P2 | Positive fixture in done cond. 1's pair + done cond. 4 payload fixture |

## Non-goals

- **Anything from Epic B**: module identity/mangling, per-module scopes,
  import enforcement, collision diagnostics, the type-tier/prelude manifest,
  the module-prefix string-ARC fix, cross-module LSP. The flat merge stays.
- **Exportable member variables or generated accessors** — an author who
  wants a field exposed writes the accessor (design record non-goal).
- **Re-export, type aliases, nested module naming** (design record
  non-goals).
- Any change to ARC semantics, ownership, or the concurrency model beyond
  the construction-ABI factory (REQ-001).
- **Any change to generic emission or monomorphization.** Generic structs
  are **exempt from emission filtering**: their full field layout and ALL
  method bodies (pub and private) keep shipping verbatim in the `.bmod` —
  consumer-side monomorphization requires them (pub methods call private
  helpers, e.g. `Map.set` → `find_slot`/`rehash`). Visibility for generic
  types is enforced purely as a Sema resolution rule (consumer source cannot
  name a private field/method); emission is unchanged. The U1 factory
  applies **only to non-generic structs**.

## Companion documents

| File | Purpose |
|------|---------|
| [workplan.md](workplan.md) | units U1–U5, dependencies, done conditions, budgets |
| [design.md](design.md) | design spec: seams, contracts, epic-local decisions A1–A5 |
| [evaluation.md](evaluation.md) | harnesses, audit plan, regression baseline |
| manifest.yaml | machine-readable run definition |
| [../modules-v2/overview.md](../modules-v2/overview.md) | shared design record (D1–D17) — binding |
| [../modules-v2/review-2026-08-04.md](../modules-v2/review-2026-08-04.md) | readiness review (F1–F10) |

## Constraints & context for the manager

- Constitution applies: `.specify/memory/constitution.md` (v1.2.0). Note
  Principle VI: each unit needs its design artifact **before** implementation,
  and the stdlib API work (U4) explicitly requires a design-audit checkpoint.
- The decision record D1–D17 in `../modules-v2/overview.md` is **binding** —
  decisions are not to be relitigated; raise a question if new information
  seems to invalidate one.
- **U1 is a spike-first crux.** If the factory ABI hits an unforeseen wall
  (e.g. generic instantiation interaction), stop and raise a question before
  widening scope.
- Interim semantics between U2 and U3 (review F10): until `pub` exists on
  methods, U2 ships **all** methods in the `.bmod`; U3 then flips to
  private-by-default. State this in the U2 PR so the reviewer doesn't flag a
  visibility hole.
- `--combine` mode enforces the same visibility rules as the `.bmod` path
  **for namespaced stdlib modules** (`net`, `fs`, `timer`, ... — each has its
  own scope). The combine-promoted modules (`buffer`, `collections`, `cli`)
  are parsed into the user's own scope (`qcc.cpp:308-313`) — no module
  boundary exists to enforce against until Epic B's per-module scopes — and
  are therefore **exempt from module-private enforcement in this epic**,
  recorded in Known Issues per Principle VI (audit AF-1). Owned by U3
  (enforcement) and U4 (`Map`/`Set` `pub init` + call-site migration).
- Consumer-side located errors need to know a symbol's defining origin. U3
  introduces a **lightweight defining-origin marker** on symbol/struct
  definitions (bmod-injected vs. source file) — explicitly NOT Epic B's
  canonical module identity; a flag/string, not a graph node (audit AF-6).
- Danger zones: `CGStruct.cpp` construction path (U1), `BmodEmitter.cpp`
  (U2), `Sema.cpp` visibility checks (U3/U5), `runtime/` untouched except as
  U1's factory requires — any runtime or ARC-adjacent change gates on
  `--leak-check`.
- Never touch: the flat-merge injection in `qcc.cpp` (Epic B's seam), the
  LSP server beyond keeping `./test_lsp.sh` green.
- All work lands via PR with a distinct reviewer hire per the constitution's
  audit pattern; no direct commits to `master`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| 1 | Canonical cross-module spelling for generated data-contract functions (e.g. `Todo_from_json`) under D1 — module-qualified name, or builtin dispatch like `to_json`? | U5 only | open — architect proposes in U5's speckit spec; the devbot manager approves against D1/D15, raising a question to the owner only if the proposal conflicts with a recorded decision | |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-08-05 | 92b1f2a0-c643-4fa9-a60a-fbdc6c305783 | relaunched (redo) | root cause of turn-0 pauses fixed in devbot: claude-opus-5 runs adaptive thinking when `thinking` is omitted and it shares the max_tokens budget — team-plan JSON truncated mid-string every retry; provider now pins thinking disabled + 16k cap; prior run left `interrupted` at 0 turns/0 tokens by the backend restart |
| 2026-08-05 | 370bedc3-7619-4e21-bdcd-af18b007c893 | paused → resumed | auto-paused at turn 0 (alert: controller `plan_team` returned invalid output after retry — transient); resumed, status running |
| 2026-08-05 | 370bedc3-7619-4e21-bdcd-af18b007c893 | launched | fully_autonomous; limits 120 turns / 16h / 5M tokens; planning docs committed at d980c37 |
| 2026-08-05 | — | review passed | `/devbot-review` + fresh-context audit (AF-1..AF-10); all findings fixed: generic-emission exemption, promoted-module exemption + Map/Set pub init, fail/xmodule harness, reach-in script committed, done conditions sharpened; mechanical checks green (sync verbatim, traceability, deps, manifest) |
| 2026-08-04 | — | epic created | Split from modules-v2 per review; D15–D17 recorded in design record |
