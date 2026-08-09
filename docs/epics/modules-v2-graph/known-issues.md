# Known issues — epic modules-v2-graph (Epic B)

Deliberate simplifications and deferrals surfaced by this epic's work, recorded per
constitution Principle VI ("a deliberate simplification MUST be recorded in Known
Issues with its rationale"). Each says what it is, why it was not done here, and
who owns it.

Filed at U1; extended by subsequent units. (Inherited KI-3/KI-5/KI-16/KI-23 from
Epic A are tracked in the epic overview's "Inherited known-issues" table and closed
by their listed units.)

---

## KG-1 — generic FUNCTION identity mangling is deferred (types done, functions unchanged)

**Filed**: U1. **Owner**: follow-on (not this done-condition).

U1 keys generic mangling on module identity for generic **types** (structs): the
digest is looked up from the defining `StructDefinition` in `mangleGenericName`.
Generic **functions** (`sort<T>`, `largest<T>`) are *not* stamped — a generic
function keeps its identity-free mangling (`largest_int`), because a
`FunctionDefinition` carries no module digest in U1 and the done-condition (REQ-002)
targets same-named exported **types** (the miscompile risk: two types with
different layouts/dtors collapsing).

**Why safe to defer.** Two modules exporting a same-named generic function collapse
onto one `linkonce_odr` symbol, but a generic function is monomorphized from the
body shipped in the `.bmod`, so the collapsed symbol is the *same code* in the
common case — a benign dedup, not a layout/dtor miscompile. The dangerous case (two
*different* generic-function bodies under one name across modules) is not reachable
today: the flat merge (P1) permits only one unqualified name, and qualified access
for `.bmod` deps does not exist until U6. When U6 lands qualified cross-module
access, revisit whether function identity mangling is needed; the mechanism is a
straightforward extension (give `FunctionDefinition` the same digest field and look
it up in `mangleGenericName`).

**Guard**: `test_build` keeps `myapp`'s `nm` assertion that `largest_int` is a
single weak symbol — the shared-origin function dedup this relies on stays proven.

---

## KG-2 — the module-identity digest keys on `realpath`; cross-machine reproducibility is waived

**Filed**: U1. **Owner**: follow-on (in-workspace portable-origin refinement).

The serialized digest keys on the **`realpath`** of a module's project dir (D5's
own fallback: "the realpath with reproducibility explicitly waived"). This is
**correct on one machine** — the dep's own build (`--module-origin`) and any
consumer's reference to it (`--bmod-origin`) compute the identical absolute path, so
`linkonce_odr` dedup holds across the module boundary — but the digest, and hence
the mangled symbols inside a `.a`, are **not reproducible across machines / checkout
locations**. A shared object cache keyed partly on these symbols would miss across
machines.

**Why this form (not project-root-relative) in U1.** The design's portable form
(project-root-relative inside the invoking top-level project) is a cross-machine
cache-sharing nicety. Making it *correct* is subtle: the dep's own build and the
consumer's reference must compute the **same** relative origin, but each `bcc`
invocation has its own top-level root, so a naive project-relative origin would
differ between the two sides and **break dedup** — the exact regression U1 must not
cause. Choosing `realpath` keeps dedup provably correct now; the portable refinement
is a follow-on that must preserve dep/consumer digest agreement (likely by threading
the invoking top-level root through the recursive build so both sides relativize
against the same root).

**Interaction with the cache**: `BuildCache::computeKey` already hashes source
content + toml + dep hashes + the `.bmod` format-version salt; it does not hash the
origin digest, and the warm-cache correctness of the U1↔U5 window is handled by the
co-ship invariant (design-audit-U1 B1) — a `bcc clean` between U1 and U5 on
`master`. No behavioral cache change is introduced by U1.

---

## KG-4 — `Set<int>` (value-type hashed Set) miscompiles: hashes with `__blang_hash_string`

**Filed**: U3 (surfaced writing the D13 zero-import fixture). **Owner**: not this epic
— pre-existing, epic-UNRELATED; file only.

A `Set<int>` fails IR verification — its `find_slot`/`add` monomorphization calls
`__blang_hash_string(i32)` (a `ptr`-parameter function) on an `int` key: `Call
parameter type does not match function signature`. The hashed `Set<K>`
(`stdlib/collections.b`) was only ever exercised with `Set<string>` (`codegen_set.b`),
so the value-type key path (which should dispatch to an integer hash, as hashed `Map`
does for value keys) was never covered. Reproduces identically via the normal
`import collections;` path on `master` — **independent of U3** (U3 only made the
first `Set<int>` get written). The D13 fixture uses `Set<string>` to avoid it.

**Why not fixed here.** It is a collections/codegen monomorphization bug (value-type
key hashing in `Set`) with no trace to U3's tier work; fixing it means teaching
`Set`'s hash dispatch the same value-vs-string split `Map` already has. A regression
fixture (`Set<int>` add/has/length) should land with the fix.

---

## KG-5 — an unknown type in a declaration reports at the operator, not the type token

**Filed**: U3 (D14 fixture). **Owner**: follow-on (diagnostic quality); pre-existing.

Deleting `Buffer`'s bare-name registration (D14) makes `Buffer b = ...` (without the
prelude) fail at the declaration — the intended win (it no longer parses via a hollow
registration and fails later at a symbol lookup). But the message is the generic
`Failed parse varible` located at the **declaration operator** (`=`), not
`unknown type 'Buffer'` at the **type token**, which is the design note's (§6)
aspiration. This is the compiler's **pre-existing** behavior for *every* unknown-type
declaration (identical for `test_files/fail/bad_type.b`'s `bogus x`), not
Buffer-specific: `Type::Parse` returns null for an unknown user-type `SYMBOL` and the
var-decl path throws `Failed parse varible` at the current token.

**Why not fixed here.** Improving it well means either a change to the hot
`Type::Parse` null path (which many callers depend on — forward refs, generic-arg
recursion — so a broad regression risk) or threading a saved type-token location
through the var-decl path; both are diagnostic-quality work orthogonal to the tier
declaration. The D14 **substance** (an undefined type fails at declaration-time type
resolution, not at a later symbol lookup) is delivered and locked by
`fail/sema/buffer_without_prelude.b`. A follow-up should report `unknown type '<name>'`
at the type token uniformly (updating `bad_type.b` too).

---

## KG-3 — done-condition-1 fixture uses UNQUALIFIED calls (qualified `.bmod`-dep access is U6)

**Filed**: U1. **Not a defect — a sequencing note.**

`test_build/boxapp` calls `boxed_a(...)` / `boxed_b(...)` **unqualified**, not
`boxa.boxed_a(...)`. In the current (pre-U6) model, `.bmod` dependency symbols are
**flat-merged** into the global scope and used unqualified (P1); qualified
`module.name` access for `.bmod` deps — and the enforcement that makes an unimported
symbol an error — is **U6**. The fixture correctly proves U1's claim (two same-named
generic **types** get distinct symbols) without depending on U6's resolution model;
the two libraries' distinct function names never collide, and the binary never names
`Box`, so the flat-merge `Box` name collision (P2) is irrelevant to this test. When
U6 lands, the fixture's calls can move to the qualified form.
