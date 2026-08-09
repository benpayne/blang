# U5 spike — the un-named foreign-generic instantiation (done-condition 7's sharpest corner)

**Epic**: modules-v2-graph · **Unit**: U5 · **Covers**: REQ-009 · **Depends on**: U1 (merged), U4 (merged).
**Principle VI bounded spike review** — routed to @auditor WITH its fixture BEFORE broad implementation.

The design record calls this the "sharpest corner": a binary calls a `pub` function/method
returning a **foreign generic** (`Box<T>`) from a module it does **NOT** import, and must
monomorphize the body shipped (transitively) in the `.bmod` and link — D7 use-capability
across an un-named module.

## 1. The spike fixture (committed) — `test_build/{boxq,midx,usebox}`

```
boxq (Q)   pub struct Box<T> { T v; }  impl { pub init(T); pub fn get(self)->T; }   // DEFINES the generic
midx (X)   import boxq;  pub fn get_box(int n) -> Box<int> { return Box<int>(n); }  // RETURNS the foreign generic
usebox (A) import midx;  Box<int> b = get_box(7); b.get();                          // USES it — never imports boxq
```

`usebox` imports **midx only** — never `boxq`. It receives a `Box<int>` from `get_box` and
calls its `pub` method `.get()` (use-capability), which forces it to obtain `Box`'s
definition and monomorphize `Box<int>` — from a module it cannot name.

## 2. Reproduced failure (today, format 4)

```
$ (cd usebox && bcc build)
midx.bmod:4:30: error: Expected '{' to begin function body
main.b:7:6: error: Failed parse varible          (Box<int> b = ...)
```

`midx.bmod` is exactly:
```
// blang-bmod-format: 4
pub fn get_box(int n) -> Box<int>;
```

It **names `Box<int>` with no definition, no reference to boxq, and no identity**. When
`usebox` parses `midx.bmod`, `Box` is unknown, so the interface is unparseable and the
program fails at the type. This is the gap done-condition 7 closes.

## 3. Three coupled gaps (root causes)

1. **No foreign-type reference in the `.bmod` (the crux).** `midx.bmod` writes `Box<int>`
   as if `Box` were local. It carries no way to say "`Box` is a **foreign** type owned by
   module `boxq` (identity `<digest>`, human name `boxq`)". So a reader that did not import
   `boxq` cannot resolve it.
2. **No transitive `.bmod` closure.** `bcc` passes only **direct**-dep `.bmod`s to the
   consumer's `qcc` (`depBmodFiles`, `bcc.cpp:1214/1273/1292`). `usebox`'s direct dep is
   `midx`, so it never receives `boxq.bmod` — even though it needs `Box`'s generic **body**
   (shipped in `boxq.bmod`) to monomorphize `Box<int>`. The **build graph** (transitive
   `.bmod` closure, D-record §Identity) is not assembled.
3. **No transitive `.a` closure (link side).** Likewise only `midx.a` is linked. `Box<int>`'s
   instantiation lives in `midx.a` (linkonce_odr, from `get_box`), so **if** the consumer's
   own monomorphization mangles it identically (U1 digest), `midx.a`'s instance dedups and
   linking works without `boxq.a`. But the transitive `.a` closure must be assembled for the
   general case (a foreign type instantiated only in the defining lib).

## 4. Proposed mechanism (for the auditor to weigh)

1. **`.bmod` foreign-type refs by identity (format 4→5).** When an exported signature
   references a type NOT defined in the emitting module, the emitter records a **foreign-ref
   header**: the type's **human name** + its **defining module's 12-hex identity digest** (U1)
   + enough to reconstruct it (for a generic: its body ships in the defining module's `.bmod`,
   reached transitively). The reading module renders it through **its own** qualifier (D3), and
   Sema resolves `Box` to the foreign definition. **Bump `BlangBmod::kFormatVersion` 4→5**;
   the salt already flows to `BuildCache::computeKey` (`BuildCache.cpp:144-145`,
   `formatSalt = "bmod-format:" + version`) — a cache-invalidation test proves warm entries miss.
2. **Transitive `.bmod` closure (build graph).** `bcc` assembles every `.bmod` needed to
   typecheck a dependency — not just direct deps — and passes the closure to `qcc` (with each
   dep's `--bmod-origin` so U1 digests are stamped correctly). `usebox` thereby receives
   `boxq.bmod` (Box's body + boxq's digest) and monomorphizes `Box<int>` mangled with **boxq's**
   digest — identical to `midx.a`'s instance → linkonce_odr dedup.
3. **Transitive `.a` closure (link graph).** `bcc` links the transitive `.a` closure so a
   foreign type instantiated only in its defining lib still resolves.
4. **U1 interlock (already designed).** The digest is carried in the `.bmod` foreign-ref
   (§2.3 of design-audit-U1), so the consumer reproduces the defining module's mangling
   without importing it — the un-named instantiation links.

## 5. Done-condition-7 checkpoint

- **7a (transitive dep):** `A → X → Q`, `A` uses a `Q` type via `X` without importing `Q` —
  builds and runs. (This spike fixture.)
- **7b (the sharpest corner):** the un-named foreign-generic `Box<T>` — monomorphizes, links,
  runs. Same fixture, generic case.
- Cross-module generics still link; cache-invalidation test proves the 4→5 bump.
- `.bmod` foreign refs parse standalone (the epic's standing `bmod_parses` check).

## 6. Questions for the auditor (bounded)

1. **Foreign-ref representation:** carry the foreign type as a **declaration reconstructed
   from identity** (name + digest, body reached transitively) vs. **embedding** the foreign
   generic's body directly into the referencing `.bmod`. Recommendation: **reference by
   identity + transitive closure** (D-record: two graphs, never conflated; embedding would
   duplicate bodies and defeat dedup/versioning). Is that the right call?
2. **Transitive closure ownership:** assemble it in `bcc` (it already builds deps recursively,
   so it can propagate the closure upward) vs. teach `qcc` to chase `.bmod` imports. Recommendation:
   **`bcc` assembles + passes the closure** (qcc stays a pure per-invocation compiler; matches
   the U1 `--bmod-origin` plumbing).
3. **Security (untrusted `.bmod`):** the reader parses foreign-ref headers from a `.bmod` it did
   not author — must reject a malformed/incoherent foreign ref with a located error, never crash
   (constitution Quality Gate 7). Confirm the spike's threat model is right for the U5 security review.
4. **Link-side:** is dedup-via-U1-digest (§4-part-2) sufficient so `boxq.a` is not strictly
   required when `midx.a` already carries `Box<int>`, or must the transitive `.a` closure always
   link `boxq.a`? Recommendation: assemble the transitive `.a` closure (robust) but rely on
   linkonce_odr dedup for correctness.
