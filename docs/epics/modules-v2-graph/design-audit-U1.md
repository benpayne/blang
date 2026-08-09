# Design audit — U1: Canonical module identity (keystone)

**Epic**: modules-v2-graph · **Unit**: U1 · **Speckit**: `034-canonical-module-identity`
**Principle VI design-audit gate — reviewed by @auditor BEFORE implementation.**
**Covers**: REQ-001, REQ-002 · **Closes (obligation)**: KI-16 obligation 1
**Binding decisions**: D2, D5, D6, D10; open-question #2 (answered: layered identity)

---

## 1. Problem this unit solves

Type identity, generic symbol mangling, and dedup policy are three faces of one
missing artifact: a **canonical module identity**. Today:

- `mangleGenericName` (`CGTypes.cpp:11-21`) is `baseName + "_" + argName`, with
  **no module component**. Two modules that each export `Box<int>` mangle to the
  identical symbol `Box_int`, emitted `linkonce_odr` (`CGTypes.cpp:348/497`,
  `BmodEmitter.cpp:185`) — the linker silently keeps whichever it saw first
  (**P10, a latent miscompile**). Harmless only because the flat merge permits
  exactly one `Box` today.
- `mDefiningFile` (`Type.h:495`) holds a **source base name** (`stampDefiningOrigin`,
  `QModule.cpp:636-656` strips dir + extension), with **no file→module mapping**
  (**KI-16**). A library split across several `.b` files yields several distinct
  values, so any rule comparing them directly would reject legal intra-library
  access.

## 2. Proposed model

### 2.1 Two-layer identity (open-question #2 answer, already accepted)

| Layer | Value | Used for |
|-------|-------|----------|
| **In-process identity** | `realpath` for path deps; `url@pin` for git deps (D5) | the module graph (U4), dedup key (D6), circular-dep detection (already done by `bcc`) |
| **Serialized identity** | **SHA-256 digest, 12 hex (48 bits)** of a **portable origin string** | mangled symbols (D10), `.bmod` foreign refs (U5), shareable cache keys |

**Digest width (B2, auditor ruling).** The serialized digest is **12 hex nibbles
= 48 bits**, not 8. Rationale: this digest lives in a **shared cross-build /
`.bmod` namespace** — symbols and foreign refs from independently-built libraries
coexist in one link — so its collision resistance must be sized for that shared
namespace, not for a single build. The **within-build hard-error on collision
(§3) is a backstop only**: it catches a collision *inside one compilation* but
cannot see a collision against a symbol baked into a separately-built `.a`/`.bmod`
the linker later pulls in. **The backstop does NOT substitute for width** — width
is the only defense in the cross-build namespace, hence 48 bits.

The **portable origin string** is:
- git dep → `url@pin` verbatim;
- path dep **inside the workspace** → **project-root-relative path**, where
  "project root" is pinned to the **directory of the invoking top-level project's
  `blang.toml`** (the root `bcc build` was invoked against), NOT the current
  working directory and NOT any dependency's own root — so the same dependency
  gets the same relative origin regardless of where inside the tree the build is
  launched;
- path dep **outside that project root** → `realpath`, with **reproducibility
  explicitly waived** and a Known-Issues note (Principle VI: deliberate
  simplification recorded).

**`realpath` failure is a located hard error (non-blocking item, folded).** If
`realpath()` fails (missing path, permission, broken symlink), the driver emits a
located `file:line:col: error:` diagnostic naming the unresolvable dependency and
aborts — it **never** falls back to an empty-string or raw-input identity. An
empty/degenerate identity would silently collapse distinct modules onto one digest
(a P10-class miscompile through the back door), so this path must fail loud, not
soft.

Rationale for two layers: raw absolute paths must never enter a mangled symbol or
a `.bmod` (they are non-portable and would break shared caches across machines);
but `realpath` remains the correct in-process key because it resolves symlinks so
D6 does not fire spuriously on two spellings of one directory.

### 2.2 A `ModuleIdentity` value type

A small, hashable, comparable value (proposed `ModuleIdentity.h`):

```cpp
struct ModuleIdentity {
    std::string canonicalOrigin;   // in-process: realpath | url@pin
    std::string humanName;         // local qualifier for diagnostics (D3/D4)
    std::string digest() const;    // memoized short SHA-256 of the PORTABLE origin
};
```

- **Nominal & owned by the defining module (D2).** Type identity = (defining
  module identity, type name). Two `Box`es from two origins are two types.
- **No dedup across distinct origins (D6).** Identity equality is origin
  equality; the compiler never unifies two origins whose bytes match.
- Reuses `sha256.{h,c}` (already in-tree, FIPS-180-4, no deps).
- **Split of responsibility (non-blocking item, folded — KI-16 obligation-2
  discipline).** *Origin resolution* (turning a `blang.toml` dep key + the
  invoking project root into the in-process `realpath`/`url@pin`) lives in the
  **driver** (`bcc`/`qcc` main), where dep origins and the project root are
  already known. *Digest computation* (portable-origin string → 12-hex SHA-256)
  and the `ModuleIdentity` value type live in **shared frontend plumbing**
  (alongside `stampDefiningOrigin` in `QModule.cpp`/`Frontend.h`), so `qcc` and
  `blangd` compute an identical digest from identical inputs and cannot diverge —
  the same discipline KI-16 obligation 2 already established for origin stamping.
- **Reproducible at an un-naming consumer (D7/U5 interlock — folded).** The
  defining-module digest of a foreign type must be **reconstructible at a consumer
  that never `import`ed the defining module** (D7 use-capability: it monomorphizes
  a foreign generic body it can use but not name). Therefore the digest must be
  derivable **entirely from data carried in the `.bmod`** (the defining module's
  portable origin / precomputed digest, shipped by U5's foreign-ref format) — it
  may **never** depend on defining-site-only state (the consumer's own project
  root, the defining module's local dep table, or anything not in the interface).
  Concretely: U5's `.bmod` foreign-ref carries the defining module's **already-
  computed 12-hex digest** (plus its human name for diagnostics), and the consumer
  mangles the monomorphized instance with that carried digest verbatim. This is
  the U1↔U5 contract that keeps the un-named foreign-generic instantiation
  (done-condition 7b, the "sharpest corner") link-correct. **U1 states the
  contract; U5 ships the carrier.**

### 2.3 Threading identity through the three risks

1. **Type identity** — give `mDefiningFile`'s successor a real module mapping.
   This unit introduces the identity but is **behavior-neutral on resolution**:
   the field is populated, nothing new *reads* it for visibility yet (KI-16's
   standing constraint — the visibility rule keying on it is U6). Only mangling
   (below) consumes it in U1.
2. **Generic mangling (D10)** — `mangleGenericName` incorporates the defining
   module's **12-hex digest**: `Box_int` → `Box_<digest12>_int` (must apply to
   nested type args recursively, and must be **byte-identical** on the emit side
   `CGTypes.cpp:348/497` and `BmodEmitter.cpp:185`, and at any consumer that
   monomorphizes a foreign generic — using the digest **carried in the `.bmod`**,
   never recomputed from consumer-side state, per the §2.3 D7/U5 interlock). Two
   same-named exported generics now get **distinct symbols**; the P10 collapse is
   gone.
3. **Dedup policy (D6)** — no dedup; distinct origins → distinct identities →
   distinct mangled symbols.

## 3. Failure modes considered (auditor rubric: general case + failure modes)

- **Same origin reached twice in one build** (e.g. a dep and the top-level both
  monomorphize `Box<int>` from the same library). Same identity → same digest →
  same `linkonce_odr` symbol → correctly deduped at link (this is the behavior we
  must *keep*; the U1 fixture must prove a shared-origin instantiation still
  dedups, not just that distinct origins diverge).
- **Digest collision** — at **48 bits (12 hex, B2)** a collision across a build's
  or a link's origins is astronomically unlikely; the **within-build** hard error
  (§2.1) is a backstop, and **width is the defense in the cross-build/`.bmod`
  namespace** where the backstop cannot see separately-built symbols.
- **Path outside workspace** — reproducibility waived, recorded in Known Issues;
  in-process build still correct (realpath), only cross-machine cache-sharing is
  affected.
- **Symlinked dep** — `realpath` canonicalizes, so D6 does not misfire.
- **`realpath` unresolvable** — located hard error, never empty identity (§2.1).
- **`.bmod` already on disk from a warm cache** — resolved below (§3.1, B1).

### 3.1 B1 — U1↔U5 cache seam (auditor ruling: option (a), co-ship invariant + proof)

**Resolution (option (a)).** U1's mangling change is **cache-covered only by U5's
`.bmod` format-version salt**. The invariant, written into the design record:

> **Co-ship invariant.** No compiler release may contain U1's mangling change
> without U5's format-version salt. On `master`, in the window between U1 landing
> and U5 landing, warm build caches are stale and a **`bcc clean` is required**
> before a cross-module build. (U1 and U5 are expected to co-ship in the same
> release; the window exists only on intermediate `master` commits.)

**Why no separate cache-key salt is needed at U1 — the link-edge proof.** U1's
mangling change alters symbol strings for **exactly one category**: monomorphized
**generic instances**. Walking every cross-module link edge:

1. **Non-generic cross-module symbols link by their *unmangled* base name.** A
   `.bmod` library dep is built by `bcc build` **with an empty module prefix**
   (KI-5: library projects run prefix-free), and non-generic public functions are
   marked `extern` and linked from the `.a` by their plain name
   (`qcc.cpp:350-352`). `mangleGenericName` is **never** in this path — U1 does not
   touch a single non-generic cross-module symbol. No stale non-generic edge
   exists.
2. **Generic instances are re-emitted per consumer as `linkonce_odr`.** Every
   consumer that uses a generic monomorphizes it locally from the body shipped in
   the `.bmod` and emits it `linkonce_odr` (`CGTypes.cpp:348/497`,
   `BmodEmitter.cpp:185`). After U1 both the library's own instantiation *and*
   every consumer derive the mangled name from the **same defining-module digest**
   (carried in the `.bmod` per the §2.3 D7/U5 interlock), so they agree and dedup
   at link. There is **no precomputed generic symbol string** referenced across a
   link edge that isn't re-emitted by the referrer itself.
3. **The only stale artifact is a warm *object/archive* from before U1** — a
   cached `.o`/`.a` containing a generic instance under the *old* mangling, linked
   against a fresh consumer emitting the *new* mangling → duplicate-or-undefined
   symbol. This is precisely a **cache-staleness** condition, and it is exactly
   what U5's format-version salt invalidates (`BuildCache::computeKey`,
   `BuildCache.cpp:128`, already wired by Epic A). The co-ship invariant covers the
   intermediate-`master` window with `bcc clean`.

**Conclusion:** the proof holds → **option (a)**, no U1-local `computeKey` touch.
Had step 2 revealed a cross-module edge referencing a precomputed mangled string
the referrer does not re-emit, the proof would fail and we'd take **option (b)**
(add a compiler/epic salt to `computeKey` at U1 + a warm-cache regression test);
it does not, so we do not. **The U1 fixture must include a negative guard: a warm
cache built pre-mangling-change, then a rebuild, must not silently link a stale
instance — exercised as the `bcc clean` step in the fixture script**, documenting
the window rather than pretending it doesn't exist.

## 4. What U1 does NOT do (scope discipline)

- Does **not** remove the flat merge (U6), build the module graph (U4), or enforce
  imports (U6). Identity is populated and consumed only by mangling here.
- Does **not** change ARC, the export model, or `.bmod` format (U5 bumps 4→5).
- Does **not** re-key any visibility rule on identity (KI-23/KI-16 obligation is
  U6) — only satisfies KI-16 *obligation 1* (the mapping exists).

## 5. Done condition & evidence

- `test_build/` fixture: two path-dep modules each exporting a same-named generic
  type, both instantiated in one binary → builds/links/runs; the fixture script
  asserts **distinct mangled symbols** via `nm`/IR grep (and a companion assertion
  that a shared-origin instantiation still dedups).
- Identity-model design artifact committed under `specs/034-…` before code.
- All standard gates green in both build modes; `test_lsp.sh` green (blangd builds
  against the shared `stampDefiningOrigin`/identity plumbing — no functional
  change).

## 6. Auditor items — dispositions (rev. after CHANGES-REQUESTED, 2026-08-09)

**B1 — U1↔U5 cache seam. CLOSED (option (a)).** §3.1 records the co-ship invariant
(no U1 mangling without U5's salt; `bcc clean` in the intermediate-`master` window)
and the link-edge proof (non-generics link by unmangled name and never touch
`mangleGenericName`; generic instances are `linkonce_odr` re-emitted per referrer
from the `.bmod`-carried digest; the only stale artifact is a warm object/archive,
covered by U5's format salt). No U1-local `computeKey` touch. Fixture carries a
`bcc clean` warm-cache guard.

**B2 — digest width. CLOSED.** 12 hex / 48 bits (§2.1, §2.3). Within-build
hard-error kept as a **backstop only**; explicitly stated it does not substitute
for width in the shared cross-build/`.bmod` namespace.

**Non-blocking items — FOLDED:**
- `realpath()` failure → **located hard error, never empty-string identity** (§2.1).
- **"project root" pinned** to the directory of the invoking top-level project's
  `blang.toml`, not CWD, not any dep's own root (§2.1).
- **Digest computation in shared frontend plumbing** (KI-16 obligation-2
  discipline) while **origin resolution stays in the driver** (§2.2).
- **Defining-module digest reproducible at an un-naming consumer** — derivable only
  from `.bmod`-carried data (D7/U5 interlock), never defining-site-only state
  (§2.3).

**Status:** all changes-requested items closed in this doc. The same content is
mirrored into the `034-canonical-module-identity` speckit plan. **Holding U1
implementation for @auditor re-review of these plan updates** before the mangling
change lands.
