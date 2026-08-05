# Design record: modules-v2 — Modules & Visibility: a real import graph and an opaque-type export model

**Archetype**: evolve (replaces the resolution and export model under the
existing module surface)

**Status**: **design record — not a runnable epic.** After the 2026-08-04
readiness review ([review-2026-08-04.md](review-2026-08-04.md)), this work was
split into two epics that both inherit this document's decision record
(D1–D17) verbatim:

- **Epic A — [`modules-v2-exports`](../modules-v2-exports/overview.md)**
  (opaque exports: construction ABI, `pub` methods/`init`, private fields,
  stdlib accessor API, P8/P9) — planned, sequences first.
- **Epic B — `modules-v2-graph`** (module identity, per-module scopes, import
  enforcement, type tiers, module-prefix codegen fix, cross-module LSP) — to
  be planned via `/devbot-plan modules-v2-graph` after Epic A completes.

There is no `manifest.yaml` here; nothing launches from this directory.

**Owner**: Ben Payne

**Created**: 2026-08-04 · **Updated**: 2026-08-04

**Source documents**: design review 2026-08-04 (recorded verbatim as the
Decision record below); readiness review
[review-2026-08-04.md](review-2026-08-04.md) (findings F1–F10, verified
against code — its amendments are folded into this document). Every "today"
claim in this document was **reproduced against the compiler** at `master` @
`7e75352`; the reproductions are quoted inline so a reader can re-run them.

## Why

Modules-v1 shipped the whole surface: `import`, dotted names, `pub`
visibility, `.bmod` interface files, `bcc build` with path and git
dependencies, content-addressed caching, and cross-module generic
instantiation. Programs compile and link across module boundaries today, and
that machinery stays.

What did *not* ship is a sound model underneath, and there are two halves to
that.

**Resolution** is a **flat merge**: before any file's imports are even parsed,
every public symbol from every `.bmod` dependency is dumped into a single
process-global scope. So `import` is advisory, collisions are silent,
qualified access is held together with per-module special cases, and the
editor cannot resolve across files at all.

**Export** is inverted. A `pub struct` ships its full field layout into the
interface file but *not its methods* — so a consumer can reach into an
imported struct's internals while being unable to call a single one of its
methods, and an exported type whose field references a private type produces
an interface file that simply does not parse.

These are the same problem seen from two sides: what crosses a module
boundary, and by what rule. Visibility is therefore **integral** to this epic
rather than adjacent to it.

## How it works today

### The v1 language surface (preserved)

- **File = module.** `math.b` is module `math`; one level deep, no hierarchy.
- **Flat namespace.** `import std.io;` is a single flat name — the dot is part
  of the name, not a path separator. Imported symbols are used one qualifier
  deep: `math.add(1, 2)`.
- **Private by default**, `pub` to export (`pub fn` / `pub struct` /
  `pub enum` / `pub protocol`).
- **`.bmod` interface files** carry a library's public declarations; generics
  ship their bodies for on-demand monomorphization, non-generics stay
  signature-only and link from the `.a`.

### Two resolution paths

1. **The namespaced path** (combine mode, stdlib modules). Qualified access
   like `sys.args()` requires both a namespace scope *and* a satisfied
   `isModuleImported` check (`QExpression.cpp:249`). Roughly sound already —
   except for the hardcoded `buffer`/`collections`/`cli` modules promoted back
   into the global scope to dodge codegen bugs.
2. **The flat-merge path** (`.bmod` dependencies under `bcc build`). Every
   public symbol is injected into `gScope` up front — unqualified, with no
   import check at all (`qcc.cpp:330-381`). This is where P1 and P2 live.

Unifying those two paths into one model is half the epic.

### What actually crosses a module boundary

Three findings, each reproduced:

- **Exports are explicit and definition-level.** Private is the default; `pub`
  marks an export, on exactly four kinds — `fn`, `struct`, `enum`, `protocol`.
  The parser, the `.bmod` emitter, and the flat-merge injection all agree.
  Struct fields, methods, and module-level values have no visibility of their
  own.
- **A `pub` struct exports its entire layout but none of its methods.** Given
  a struct with an `init` and a method, the emitted `.bmod` contains only the
  field list; a consumer's `Counter(5)` fails to parse and `c.bump()` reports
  `type 'Counter' has no method 'bump'`. Cross-module struct use therefore
  works *only* by reaching into fields — the precise inverse of what an
  interface should offer.
- **Exported signatures can reference private types, producing an unusable
  interface file.** A `pub struct` with a field of non-`pub` type emits a
  `.bmod` naming that type without declaring it — and the library still builds
  green, exit 0, no warning. The failure lands on the *consumer* as a syntax
  error inside a generated file they never wrote
  (`leaky.bmod:5:8: error: Expected field type in struct definition`),
  followed by a cascade blaming their own correct code.

**The finding that shapes the fix**: struct-typed fields are stored as
**pointers**, not inline — `struct Outer { Inner thing; int count; }` compiles
to `%Outer = type { ptr, i32 }`. A field's type therefore contributes nothing
to its owner's layout beyond a pointer's width, so a private field's *type*
never needs to reach the consumer.

**Correction (review F1)**: the original draft claimed this made the export
model possible "without opaque handles, ARC changes, or codegen upheaval."
That over-claimed. Construction is **caller-allocating** today: at a
`Counter(5)` site the *consumer's* codegen computes the struct's alloc size
(`CGStruct.cpp:640-642`), generates its **destructor** from the field list
(`CGStruct.cpp:660`), calls `__blang_rc_alloc_dtor(size, dtor)`, then
`Counter_init(...)` (`CGStruct.cpp:667-668`). The pointer-field finding covers
field *type identity* but not field *count and kinds* — without layout the
consumer can compute neither size nor dtor. The export model therefore
requires a **construction-ABI change**: cross-module construction goes through
a library-emitted factory (`StructName_new(args) -> ptr` wrapping
alloc-with-dtor + init, declared in the `.bmod`); the consumer's scope-exit
release calls `__blang_rc_release`, which invokes the dtor pointer stored at
allocation. This is Epic A's crux and its first spike.

### Type tiers, as they accidentally exist

There is no single "builtin vs library" line — there are **seven** mechanisms,
each added to solve a different immediate problem:

| Tier | Mechanism | Members |
|---|---|---|
| A · keywords | Reserved words; can never be user names | `int`, `char`, `string`, `bool`, `float`, `double`, `long`, `short`, `byte`, `chan`, `cstring`, `carray` |
| B · bare names | `addType(new Type("X"))` — a name with **no definition behind it** | `Task`, `Array`, `Buffer` |
| C · real builtins | Compiler-constructed AST | `Option`, `Result`, `Printable`, `print`/`println`/`to_json` |
| D · hardcoded | Methods dispatched by string compare in codegen, C runtime backing | `string`, `Array` |
| E · promoted | Hardcoded name list in the driver (`qcc.cpp:308`) | `buffer`, `collections`, `cli` |
| F · namespaced | Namespace scope, qualified access | `net`, `fs`, `timer`, `math`, `time`, `random`, `env`, `sys`, `io` |
| G · libraries | `.bmod`, flat-merged | everything else |

`Buffer` occupies **three of these at once**: its name is a compiler builtin
(B), its definition is ordinary BLang in `stdlib/buffer.b`, and it is
force-promoted into the global scope (E). And tier B is not merely redundant —
it is harmful. Using `Buffer` without `buffer.b` in the build fails with
`Failed to find symbol 'Buffer'` at the *literal*, because the registered name
let the declaration parse; an unregistered name fails immediately at the
*type*, which is the better diagnostic. The registration buys nothing but a
later, worse error.

## Pain points

| # | Problem | Consequence | Severity |
|---|---|---|---|
| **P1** | **Imports aren't enforced.** Public symbols are injected globally before imports parse. | `import` is decorative for resolution; the dependency graph the source claims is not the one the compiler uses. | critical |
| **P2** | **No collision handling.** Two dependencies exporting `add` both call `addSymbol`; the second silently returns false and is dropped. | Arbitrary winner, no diagnostic. A dependency can shadow another's API by name with zero warning. | critical |
| **P3** | **Qualified access is special-cased.** `buffer`, `collections`, `cli` are hardcoded into the global scope to dodge a module-prefix codegen bug (string-ARC double-free) and generic-type invisibility. | A growing exception list; every new stdlib module risks the same trap. "Modules are namespaced" isn't actually true. | high |
| **P4** | **The LSP is single-file.** blangd compiles each document alone because resolution needs the whole combine step. | No cross-module diagnostics, definition, or hover — the ceiling on the language server until this is fixed. | high |
| **P5** | **Global mutable injection.** Every file re-injects every dependency's symbols into shared global state. | Order-dependent, hard to reason about, and impossible to give a module its own private-but-not-exported tier. | medium |
| **P6** | **No source-level import aliasing.** A qualifier is fixed by the dependency key with no `import x as y`. | Mostly dissolved by the decisions — qualified access removes collisions (D1/D4), re-export is excluded (D8). Residue is convenience only. | low |
| **P7** | **Stdlib resolution is hardcoded.** `import timer;` maps to `stdlib/timer.b` inside `bcc`; no module search path. | A user module named `timer` collides with stdlib; no way to add resolution roots. | medium |
| **P8** | **Methods don't cross module boundaries.** A non-generic `pub struct`'s `init` and methods are omitted from the `.bmod` entirely; only fields are emitted. | Imported types can't be constructed or operated on — only poked at through their fields. The interface exposes implementation and hides API, exactly backwards. | critical |
| **P9** | **Private types leak into exported signatures.** A `pub` definition may reference a non-`pub` type; the emitter writes the reference without the declaration and exits 0. | A broken interface file, discovered at the wrong time (consumer build), in the wrong file (generated), with the wrong message (a syntax error) — and the author who caused it never sees it. | critical |
| **P10** | **Generic mangling ignores the defining module.** `mangleGenericName` builds symbols from the bare type name plus type args — `Map<string,int>` → `Map_string_int` — and instances are emitted `linkonce_odr`. | Harmless today (the flat merge permits only one `Map`), but the moment two modules can export same-named types it is a **silent miscompile**: two unrelated types collapse onto one symbol and the linker keeps whichever it saw first. | critical |
| **P11** | **Seven overlapping type tiers with no stated rule.** `Buffer` spans three. | No principled answer to "should this be qualified?", so the promotion list grows by exception. Bare-name registration also degrades diagnostics by letting undefined types parse. | high |

## Goals

- Per-module **export scopes** and an explicit **import graph**; resolution
  walks edges, not one global soup.
- `import` becomes **enforced** — using an unimported symbol is a located error.
- **Located diagnostics** for name collisions, unknown imports, and unused
  imports, through the existing `DiagnosticEngine`.
- One **uniform** qualified-access rule — retire the
  `buffer`/`collections`/`cli` special cases.
- A **shared resolution service** both `qcc` and `blangd` use, unlocking
  cross-module editor features (P4).
- An **export model where types are opaque** across boundaries: methods are
  the API, fields never cross (P8, P9).
- A canonical **module identity** keying type identity, symbol mangling, and
  dedup (P10).
- **Three principled type tiers** replacing seven accidental ones, so "should
  this be qualified?" has an answer (P11).
- Keep `.bmod`, content-addressed caching, and cross-module generics working
  end to end.

## Non-goals

- **Nested module *naming*** — qualifiers stay one level deep (`std.io` is one
  flat name, not `io` inside `std`). Name shape only; per-module isolation is
  a goal above.
- **Type aliases.** Wrapping a type in a struct covers the need, and an alias
  is a second way to name a thing.
- **Exportable member variables**, in any form — including a future keyword or
  implicitly generated accessors. Fields are private, period; an author who
  wants one exposed writes the accessor.
- **A package registry or dependency version-solving** beyond today's pinned
  git/path deps — *including deduplicating the same library reached by two
  paths*. Two origins are two modules; unifying them is a package-manager
  concern.
- **Re-export in any form.** Injecting a qualifier into a consumer's namespace
  is the one operation that reintroduces a global-uniqueness requirement
  (see D7/D8).
- Any change to ARC, ownership, or the concurrency model.
- LSP completion — that belongs to the separate language-server epic.

## Decision record

Settled in design review. These are **requirements for the planner and
architect, not options to revisit** — each carries the reasoning that decided
it, so a later reader can tell whether new information actually invalidates one.

**D1. Imported names are always qualified — `module.name`, types and functions
alike.**
BLang targets machine generation, where provenance in the token stream is
self-checking: an LLM emitting `collections.Map` can't misremember an import
forty lines up. It is also the only single-rule option — every alternative
needs a second rule or a collision escape hatch. The flat namespace makes it
cheap: the qualifier is always exactly one short segment, never a deep path.

**D2. Type identity is nominal and owned by the defining module.**
A type is identified by where it is defined, never by the route taken to reach
it. Two modules exporting `Map` define two distinct types, and the type system
already distinguishes them — only the spelling was ever ambiguous.

**D3. Qualifiers never compose in source; composed forms exist only as display
names.**
`A.collections.Map` is never writable syntax — that would break the flat
namespace and make identity path-dependent. But diagnostics, hover, and error
text may render it, because a display name is *derived from* canonical
identity rather than establishing it.

**D4. Qualifiers are module-local bindings; module names need not be globally
unique.**
The `blang.toml` dependency key is a local alias chosen by the consumer.
`collections` inside A and `collections` inside B bind to different modules
with no conflict, because import tables are per-module. This is what makes a
flat namespace safe without a registry.

**D5. Module identity is the canonical origin — `realpath` for path deps,
`url@pin` for git.**
Not the name (that's the local binding) and not a content hash (which would
churn on every edit and rewrite `.bmod` files constantly). `bcc` already
canonicalizes paths for circular-dependency detection, and git deps already
resolve to a directory keyed by the pin.

**D6. No deduplication across distinct origins.**
Two paths are two modules even if the bytes match — they may be different
versions, and the compiler cannot know. Trying to unify them requires version
solving, a package-manager concern and an explicit non-goal. When values of
two same-named types meet, the mismatch is real and the diagnostic just has to
explain it well.

**D7. Use-capability is implied; name-capability is explicit.**
Receiving, holding, passing, returning, and calling `pub` methods on a foreign
type requires *no* import — the interface is already in the build graph.
Declaring a variable of it, annotating a parameter, storing it in your own
struct, or constructing one requires an explicit import with your own
qualifier. **Rule of thumb: anything that puts the type in a declaration needs
the import.** `var` inference covers most consuming code, and the
private-fields decision makes the split clean — "use" is exactly "call pub
methods," with no field case to adjudicate.

**D8. No re-export keyword.**
D7's capability is implied by the signature, so nothing needs declaring or
injecting — and injection is precisely what would reintroduce global
uniqueness and unresolvable collisions. A library returning a foreign type
does not force its consumers to declare that dependency, so its choice of
container is not a permanent public commitment.

**D9. Member variables are always private; methods and `init` require explicit
`pub`.**
Private means module-visible. Keeping `pub` explicit on methods preserves the
language's single rule — private by default — which yields private methods
immediately and means a `priv` keyword is never needed.

**D10. Symbol mangling incorporates module identity.**
Required for correctness, not tidiness: without it, two distinct
`Map<string,int>` types from different modules mangle to the same
`linkonce_odr` symbol and the linker silently keeps one (P10).

**D11. No type aliases.**
Wrapping a type in a struct covers the need. Consequence: with aliases out and
re-export out, a consumer's only renaming lever is the local dependency key —
which is sufficient, because that is where naming conflicts actually arise.

**D12. Three type tiers, with stated admission criteria: core, prelude,
library.**
The seven accidental tiers collapse by separating two questions currently
welded together: *does the compiler need to know this type?* and *is this name
always in scope?*
- **Core** — anything the compiler must know: it has dedicated syntax,
  participates in an ARC/ABI contract, or is required by a semantic rule. The
  primitives, `string`, `Array<T>`, `Option`/`Result`, `chan<T>`, the FFI
  types, `Printable`, and `print`/`println`/`to_json`. Adding to it is a
  language change.
- **Prelude** — ordinary BLang library code that is automatically in scope,
  unqualified, everywhere. Candidates: `Map<K,V>`, `Set<T>`, `Buffer`.
- **Library** — the default: imported and qualified.

**D13. The prelude is a fixed, compiler-shipped list that users and libraries
cannot extend.**
This is what reconciles the tier with D1: prelude names are never *imported*,
they are base scope like `int`, so qualification loses no ground and no
exception is carved. The closed list is the bright line preventing decay back
into the flat merge. A module's own definition shadows a prelude name — the
precedent exists, since a user-defined `Option`/`Result` shadows the builtin.
Membership is close to a one-way door (it consumes a global name
permanently), so the bar is: used by most programs, unlikely to be wanted by
user code, needs no compiler knowledge, stable API.

**D14. Every name in scope has a definition behind it; bare-name registration
is deleted.**
Tier B buys nothing and costs diagnostics: registering `Buffer` as a name with
no definition lets a declaration parse and defers the failure to a worse
position with a worse message. Names come from the compiler (core) or from
parsed prelude source — never from a registration with nothing behind it.

*The following decisions were added 2026-08-04 from the readiness review
([review-2026-08-04.md](review-2026-08-04.md)) and the owner's rulings.*

**D15. Data-contract annotations opt field metadata into the interface — as
compiler-facing metadata, never as source-nameable fields.** (Review F2;
owner's ruling.)
A `table` or `@json` annotation declares that the struct's shape *is* its data
contract — DB columns, JSON keys. For such structs (and for whatever minimal
ABI metadata the construction model needs), the `.bmod` carries field
metadata in a compiler-facing section that query codegen, `@json` generation,
and Sema's `.field` validation may read. Visibility is a **resolution** rule:
user source outside the defining module can never name a field, even when
metadata is present. Consequence: an imported `table struct` is queryable and
an imported `@json` struct serializable from a consumer module; editing a
field of an annotated struct correctly bumps the interface hash (the data
contract changed), while un-annotated structs keep the rebuild-avoidance win.

**D16. Protocol conformance records cross the boundary.** (Review F4.)
The `.bmod` carries `impl Protocol for Type` conformance records for exported
types, so `print("{}", foreignValue)` (Printable dispatch), generic
constraint checking (`sort<T: Comparable>` with a foreign `T`), and
`impl ForeignProtocol for MyStruct` work cross-module.

**D17. Enums export their variants and payload types; construction is
name-capability, matching is use-capability.** (Review F5.)
An enum's variants and payloads *are* its API — the deliberate inverse of the
struct rule. `.bmod` keeps shipping variant/payload declarations (which also
keeps the recursive-enum boxing helpers generable at use sites). P9
enforcement covers variant payload types: a non-`pub` type in an exported
variant's payload is a located error at the library build. Under D7: matching
a foreign enum (`some(x)` patterns) is *use* — the subject's type carries the
enum; constructing `mod.Enum.variant(x)` is *name* and requires the import.

## Proposed direction

A sketch for the architect to refine, not a prescription.

- **Module graph.** Each source file and each `.bmod` becomes a node owning
  its export scope. An `import` statement adds an edge. Name resolution walks
  self-scope, then the export scopes of imported modules (via their
  qualifier) — never a shared bucket.
- **Extract a resolution service.** Pull module/name resolution out of the
  `qcc` driver into a component both the compiler and `blangd` call. This is
  the load-bearing move: it's what lets the editor resolve imports (P4).
- **Diagnostics through the existing engine.** Collision, unknown-import,
  unused-import, and use-without-import all become `file:line:col: error:`
  lines.
- **Fix the codegen root cause.** Repair the module-prefix string-ARC
  double-free that forced the `buffer`/`collections`/`cli` promotions. The
  riskiest change; isolate it.

### Isolation and transitivity

Take `A` importing `X` and `Y`, where `X` imports `Q` and `Y` imports `P`, and
both `Q` and `P` export `add`:

- **Imports are non-transitive.** `A` has edges to `X` and `Y` only. `Q` and
  `P` are never in `A`'s resolution space — a module's own dependencies stay
  its private business.
- **Qualification isolates siblings.** Even if `X` and `Y` *both* export
  `add`, `A` writes `X.add()` and `Y.add()`. The qualifier is the isolation,
  which is why one level is enough.
- **Conflicts require unqualified entry.** A genuine collision can only happen
  when symbols land in a scope unqualified — exactly what today's flat merge
  does. Since nothing is ever injected into a consumer's namespace (D8), that
  condition never arises.

### Export model

Rests on the pointer-field finding: because a struct-typed field costs one
pointer regardless of its type, a private field's type never needs to reach
the consumer.

- Member variables are always private (module-visible).
- Methods require explicit `pub`; `init` is an ordinary method needing
  `pub init` to be reachable. A `pub struct` whose `init` is private is
  constructible only inside its module — a factory-only or handle-style type,
  with no extra mechanism.
- Struct literals become module-private automatically: with every field
  private, `Counter { count: 5 }` can't be written outside the defining
  module, so external construction has exactly **one** form: `Counter(5)`.
- The `.bmod` becomes a true interface — exported names and method
  signatures. Field layout is not source-resolvable; it survives only as
  compiler-facing metadata where a data contract or the construction ABI
  demands it (D15).
- Types in exported signatures must themselves be exported, enforced with a
  located error at the *library* build.

```blang
// counter.b — the defining module
pub struct Counter {
    int count;                        // always private; no pub on fields, ever
}

impl Counter {
    pub init(int start) { self.count = start; }
    pub fn bump(self) -> int { return self.count + 1; }
    fn reset(self) { self.count = 0; }   // no pub = private to this module
}

// consumer.b
import counter;

fn main() -> int {
    Counter c = Counter(5);           // via pub init — the one external form
    return c.bump();                   // c.count and Counter{...} not visible here
}
```

Beyond fixing P8 and P9 this buys a build-system win: a private field's type
is no longer part of the interface, so changing internals doesn't move the
`.bmod` hash and downstream dependents stop rebuilding.

### Identity, naming, and what crosses

The resolution half rests on one artifact: a **canonical module identity**
(D5). Three otherwise-separate risks collapse into it — type identity, symbol
mangling (P10), and dedup policy (D6) are the same question asked three ways.
Build it once, early, and key everything on it.

Two graphs then coexist and must not be conflated:

- **The build graph** is the transitive closure — every `.bmod` needed to
  typecheck what you depend on, whether or not you name it. This grants D7's
  use-capability, and it is what today's direct-deps-only propagation fails to
  assemble.
- **The name graph** is direct declarations only, each bound to a locally
  chosen qualifier. This grants naming and construction.

The `.bmod` must therefore reference foreign types by *identity*, rendered
through the reading module's own qualifier — a capability it lacks entirely
today. It must also carry each module's human-facing name, so a diagnostic can
say *"add `collections` to your dependencies"* rather than quoting an origin
hash.

## Proposed work units

> **Superseded by the split (2026-08-04).** Units 2–4 (plus the F1
> construction-ABI change, the F8 stdlib-accessor unit, F6 cache versioning,
> and D16/D17 emission) became **Epic A**
> ([`modules-v2-exports/workplan.md`](../modules-v2-exports/workplan.md)).
> Units 1 and 5–13 become **Epic B** (`modules-v2-graph`, planned after A) —
> with the review-F3 resequencing: the module-prefix string-ARC fix (old unit
> 10) moves ahead of the `cli` demotion (old unit 6), which otherwise ships a
> known double-free. The list below is retained as the original decomposition
> rationale.

A starting decomposition — the planner will re-cut these. Visibility
sequences before resolution because the resolution work assumes a settled
export model: what crosses a boundary has to be decided before the rules for
how it crosses. Every unit carries the standard gates: `run_tests.sh` and
`test_codegen.sh` green in both build modes, `--leak-check` clean on runtime
changes, LSP goldens where blangd is touched.

1. **Canonical module identity.** Establish identity as the resolved origin
   (D5) and thread it through type identity, generic symbol mangling (P10),
   and dedup policy (D6). Leads because three risks collapse into this one
   artifact. Include a regression proving two same-named types from different
   modules no longer share a `linkonce_odr` symbol.
2. **Emit method signatures in `.bmod`.** Ship `init` and method signatures
   for non-generic exported structs so imported types are constructible and
   callable at all (P8). Standalone bug fix, reviewable independent of any
   visibility change.
3. **Parse `pub` in `impl` blocks; enforce signature export.** Add the missing
   `pub` syntax for methods and `init` (none exists today), and make a private
   type in an exported signature a located error at the library build (P9),
   with `fail/sema` fixtures.
4. **Fields become private; drop layout from `.bmod`.** Flip member variables
   to always-private, stop emitting field layout, make struct literals
   module-private. Migrate every consumer that reaches into an imported
   struct's fields onto `init`/accessors — the bulk of the corpus churn.
5. **Foreign type references in `.bmod`; transitive build graph.** Give the
   interface format a way to reference a type owned by another module (it has
   none today), keyed by identity and rendered through the reader's qualifier.
   Propagate the transitive `.bmod` closure so the build graph is complete,
   which is what grants use-capability (D7).
6. **Type tiers: core, prelude, library.** Replace the `isGlobalTypeLib`
   promotion list with a declared prelude manifest, delete bare-name
   registration (D14), and move `cli` out of the promoted set into an ordinary
   qualified module — it is free functions, not types. Lands before import
   enforcement because it determines what legitimately needs no import.
7. **Module graph & per-module export scopes.** Introduce module nodes with
   their own export scopes and an import-edge structure. Resolve through the
   graph while keeping the global fallback — behavior-neutral.
8. **Enforce import edges.** Flip resolution to walk only imported modules;
   using an unimported symbol becomes a located error. Migrate the test corpus.
9. **Collision & import diagnostics.** Located errors for duplicate exported
   names, unknown-module imports, and unused imports (warning) — through
   `DiagnosticEngine`, with `fail/sema` fixtures.
10. **Fix module-prefix codegen; retire the special cases.** Repair the
    string-ARC double-free under namespaced codegen, then remove the
    global-scope exceptions so namespacing is uniform. Highest-risk change.
11. **Shared resolution service → cross-module LSP.** Extract resolution into
    a component `qcc` and `blangd` share; wire blangd to resolve imports
    against sibling `.b`/`.bmod`, delivering cross-module diagnostics,
    definition, and hover. New LSP golden fixtures.
12. *(stretch)* **Source-level import aliasing.** `import x as y` where the
    `blang.toml` key alone is awkward. Re-export deliberately excluded (D8).
    Genuinely optional — under D1/D4 there is no collision this resolves.
13. *(stretch)* **Module search path & std separation.** Replace the hardcoded
    `stdlib/<name>.b` mapping with configurable resolution roots.

## Risks

- **Test-corpus churn.** Enforcing imports means many fixtures that lean on
  the global merge need real `import` lines. Large, mechanical, reviewable —
  budget for it.
- **Field-access migration is the largest churn.** Because methods never
  crossed module boundaries, every existing cross-module consumer reaches into
  fields — currently the *only* way to use an imported struct. Unit 4 breaks
  all of it at once. Measure the blast radius across `test_build/`, the
  stdlib, and the examples first, and land unit 2 before it so there is a
  migration target.
- **Field-driven codegen may need metadata users can't see.** `table struct`
  queries validate `.field` references and generate SQL *in the consumer's
  module*, and `@json` serializes by field. If a consumer can no longer see
  fields, those paths may still need field metadata. Verify both before unit 4
  — the most likely place the private-fields rule snags.
- **Generic instantiation across an un-named module is the sharpest corner.** A
  consumer calling a method on a foreign generic must monomorphize bodies from
  a module it never imported and cannot name (D7). The mechanism should work —
  bodies ship in the `.bmod`, instances are `linkonce_odr` — but this is where
  identity, mangling, and the build graph meet at once. **Spike it early.**
- **The codegen fix is a crux.** The special cases exist *because* the
  module-prefix string-ARC path is buggy. Unit 10 must land with
  `--leak-check` proof; if it slips, uniform namespacing is blocked.
- **Diagnostics must be deterministic, not merely readable.** The display-name
  renderer (D3) needs a stated rule for which name wins when a type is
  reachable by several routes, because the LSP goldens pin exact diagnostic
  text. The "you may use but not name this type" error is the entire UX of D7.
- **The `.bmod` format changes shape more than once**, invalidating the
  content-addressed cache. Expect a full rebuild of dependencies at each step;
  confirm cache keys incorporate a format version.
- **Scope creep.** "Real modules" pulls toward a package registry and toward
  completion. Both are explicit non-goals; hold the line.

## Done condition (epic level)

- An imported `pub struct` is constructible and callable: `Counter(5)` and
  `c.bump()` work across a module boundary (they do not today).
- Member variables are private everywhere; no `.bmod` carries field layout,
  and a struct literal for an imported type is a compile error.
- Methods and `init` take an explicit `pub`; an unmarked method is private and
  unreachable from another module.
- A private type in an exported signature is a located error at the *library*
  build — never a syntax error inside a generated file at the consumer's.
- Two modules exporting a same-named type coexist: distinct types, distinct
  mangled symbols, no silent `linkonce_odr` collapse.
- A consumer can call methods on a foreign type it never imported, and gets a
  located, actionable error the moment it tries to *name* or construct one.
- Type tiers are core/prelude/library with a declared manifest; no hardcoded
  promotion list and no name registered without a definition behind it.
- Resolution runs through a per-module graph; the up-front global-symbol
  injection is gone.
- Using a symbol without importing its module is a located compile error,
  verified by `fail/sema` fixtures in both build modes.
- Name collisions, unknown imports, and unused imports emit located
  diagnostics.
- No hardcoded module exceptions remain in the driver; namespacing is uniform
  and `--leak-check` clean.
- blangd resolves imports: cross-module diagnostics, go-to-definition, and
  hover work, pinned by LSP goldens.
- All prior gates stay green; `.bmod` + git/path deps + cross-module generics
  still build and link end to end.

## Open questions

Both resolved 2026-08-04 (review §c, accepted by the owner as working
answers; Epic B's planning may only revisit them with new information):

1. **Exact prelude membership** — **answered**: exactly `{Map, Set, Buffer}`,
   **types only**. This is today's force-promoted set minus `cli`, so
   migration for these names is zero. `Buffer` is load-bearing: stdlib
   exported signatures take `Buffer` (`stdlib/net.b:339`), so library-tier
   `Buffer` would force `import buffer` on every `net` user. Free functions
   are never prelude: `sort` becomes `collections.sort`.
2. **The identity token for path dependencies** — **answered**: layered.
   `realpath` remains the in-process graph identity (D5 stands; it also
   resolves symlinks so D6 doesn't fire spuriously). But raw absolute paths
   are never serialized into mangled symbols, `.bmod` foreign references, or
   shareable cache keys — those use a short SHA-256 digest (8–12 hex) of a
   portable origin string: `url@pin` verbatim for git deps; for path deps,
   the project-root-relative path when the dep lies inside the workspace,
   else the realpath with reproducibility explicitly waived (recorded in
   Known Issues per Principle VI).

## Status log

- **2026-08-04** — Readiness review performed
  ([review-2026-08-04.md](review-2026-08-04.md)): verdict
  ready-with-amendments; findings F1–F10, all "today" claims verified against
  code. Owner decisions: split into Epic A (`modules-v2-exports`) + Epic B
  (`modules-v2-graph`); F2 resolved as compiler-facing metadata (D15). D15–D17
  added; F1 correction folded into the pointer-field finding; both open
  questions answered. This document reclassified as the shared design record.
- **2026-08-04** — Draft created from design review. Scope expanded mid-review
  from resolution-only to resolution + visibility + type tiers, after
  reproductions showed the export model is inverted (P8, P9) and the tier
  structure incoherent (P11). 14 decisions recorded; 2 open questions remain.
  Ready for `/devbot-plan`.
