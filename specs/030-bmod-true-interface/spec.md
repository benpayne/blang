# Spec: the `.bmod` as a true interface

**Epic**: modules-v2-exports · **Unit**: U2 · **Branch**: `epic/modules-v2-exports-u2`
**Covers**: REQ-002, REQ-007, REQ-009, REQ-011 (emission half)
**Depends on**: U1 (merged, `57d60d7`) · **Speckit**: `030-bmod-true-interface`
**Status**: Draft — one blocking question raised (§9, Q-U2-1)

Binding inputs: design record D1–D17 (`docs/epics/modules-v2/overview.md`),
epic design decisions A1–A7 (`../../docs/epics/modules-v2-exports/design.md`),
U1's audit (`../../docs/epics/modules-v2-exports/audit-u1.md`) and
`known-issues.md` KI-1..KI-7.

---

## 1. Problem

U1 made an imported non-generic `pub struct` constructible and callable, but it
did so with the interface file still shaped like a data dump: full field layout,
every method, no record of what a consumer is actually allowed to do, and no
version on the format. Three specific gaps follow from that, and one is a live
correctness bug.

1. **The format has no version, and the build cache cannot see format changes.**
   `BuildCache::computeKey` (`BuildCache.cpp:121-144`) hashes source contents,
   `blang.toml`, and dep hashes. Nothing else. This epic changes the `.bmod`
   shape in U2, U3 and U5, so a warm cache written by one compiler and read by
   the next serves a stale-format interface. Review finding F6; design A2 puts
   the salt here because U2 is the first format change.

2. **Protocol conformance never crosses the boundary.** `BmodEmitter` emits
   protocol *definitions* but no `impl Protocol for Type` records, so
   `print("{}", foreignValue)` cannot dispatch through `Printable` and a foreign
   type cannot satisfy a generic constraint (review F4, decision D16).

3. **`table pub struct` does not round-trip.** `emitStruct`
   (`BmodEmitter.cpp:216-266`) writes the annotation *before* `pub` —
   `table pub struct Name` — the inverse of source order. It survives only
   through parser leniency, and U5's D15 metadata work makes `table` structs
   load-bearing across the boundary (finding M1/F-C).

Plus two pieces of debt U1 knowingly left, both of which block later units:

4. **`test_build/run_build_tests.sh` is not run by CI** — so done-conditions 1,
   5 and 6, and U1's entire cross-module ABI proof including its ASan leg, are
   verified by nothing automatic (finding F-A, manager ruling Q7).

5. **`mFromInterface` is an ABI predicate being eyed for visibility work.**
   Reusing it would silently exempt the namespaced stdlib — which arrives as
   parsed `.b` source, not a `.bmod` — from every visibility rule U3 and U5 add
   (finding M5).

## 2. Goals

- G1 — a versioned `.bmod` format, with the version salted into the build-cache
  key, proven by a test that bumps the **real** constant.
- G2 — protocol conformance records cross the boundary; `print("{}", x)` on an
  imported `impl Printable` type works end to end.
- G3 — `.bmod` content is pinned by committed **golden files**, not greps.
- G4 — `table` structs round-trip through the interface, with a regression test.
- G5 — the visibility predicate exists and is distinct from the ABI predicate.
- G6 — CI runs the build-system suite, with sanitizer archives provisioned so
  U1's ASan leg actually executes.

## 3. Non-goals

Explicitly out of scope, and why:

- **`pub` on impl members / private-by-default** — U3. U2 keeps shipping every
  non-generic method (interim semantics A3, KI-4). Do not "fix" this here.
- **Dropping field layout from the `.bmod`** — U5. KI-7 records that
  construction is not the only consumer-side layout reader:
  `registerExternalTypes` still calls `getOrCreateStructType`, and the query-row
  (`CGRuntime.cpp:1134-1145`) and `@json` (`:1519-1651`) paths are untouched.
- **The flat-merge resolution path** (`qcc.cpp:330-381`) — Epic B.
- **Any generic factory**; generic structs stay exempt from emission filtering
  (A6).
- **`buffer`/`collections`/`cli` enforcement** — exempt this epic (A7, KI-3).
- **The `BuildCache` filename/separator weakness** — KI-1, filed not fixed.

## 4. Design

### 4.1 Format version

A single constant, emitted as the second line of every `.bmod`:

```
// auto-generated .bmod interface file — do not edit
// blang-bmod-format: 2
```

Chosen as a comment line so that older compilers, which have no knowledge of the
marker, still parse the file rather than failing on unknown syntax — the failure
mode for a version *mismatch* should be a cache miss and a rebuild, not a parse
error in a generated file (which is the P9 experience this epic exists to
eliminate). Version 1 is the implicit format U1 shipped; U2 emits 2.

`BuildCache::computeKey` gains the version as a salt. The DC7 test bumps the real
constant and asserts a warm entry misses, per the manager's ruling on Q8.

### 4.2 Conformance records (D16)

For each exported type with `impl Protocol for Type`, emit the conformance:

```
impl Printable for Counter {
	fn to_string(self) -> string;
}
```

This is existing syntax — `ParseImplBlock` already handles `impl P for S`
(`QImplBlock.cpp:36-55`) — so no parser change is needed, and bodyless members
already parse and lower to `declare` after U1.

### 4.3 Golden `.bmod` files

Committed under `test_files/golden/bmod/<project>.bmod`, compared byte-for-byte
by `run_build_tests.sh`, with an `--update-goldens` flag mirroring
`test_codegen.sh`. This is what makes each format change visible in review
(finding F-F). U3 will update them when `pub` filtering lands — expected signal,
not churn (KI-4).

### 4.4 `table` round-trip

Emit `pub table struct Name`, matching source order, and add a `table`-struct
library + consumer fixture whose `.bmod` is re-parsed. The existing order is a
latent break, not a cosmetic one.

### 4.5 The visibility predicate (M5)

Add a **module-origin string** to definitions, separate from `mFromInterface`:

| Predicate | Question | Set for |
|---|---|---|
| `mFromInterface` (U1) | did this arrive through a `.bmod`? — **ABI** | `.bmod` structs only |
| `mDefiningModule` (U2) | which module defines this? — **visibility** | every parsed definition, `.b` and `.bmod` alike |

U2 introduces and populates it; U3/U5 enforce against it. It is a string, not a
graph node — Epic B owns canonical identity (D5).

### 4.6 CI job

A `build-system` job running `test_build/run_build_tests.sh`, with the package
list from audit F-A **plus** sanitizer provisioning:

```yaml
- run: cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined
- run: cmake --build build-asan --parallel
```

Without it U1's ASan leg prints `SKIP` forever while the suite reports green
(MAJOR-6). The leg already fails rather than skips under `CI=true`.

## 5. Test plan

| # | Test | Proves |
|---|---|---|
| 1 | Golden `.bmod` for `counterlib` and a new `tablelib` | G3, G4 |
| 2 | `table` lib + consumer builds and queries | G4 |
| 3 | Imported `impl Printable` type printed E2E, exact output | G2, DC5 |
| 4 | Format-version bump invalidates a warm cache entry | G1, DC7 |
| 5 | `.bmod` carries a conformance record (golden) | G2 |
| 6 | Prefix-aware factory branch covered (KI-5, MINOR-B) | closes dead code |
| 7 | All U1 fixtures still green | no regression |

## 6. Risks

- **The conformance record changes what the impl parser sees.** `impl P for S`
  with bodyless members must not re-run conformance *checking* against a
  signature-only record and reject it. Mitigation: verify
  `QImplBlock.cpp:149-240` treats a bodyless member as satisfying the protocol.
- **Golden `.bmod`s are order-sensitive.** `BmodEmitter::emit` has a fixed
  order (structs → enums → protocols → functions), but method order follows
  `mMethods`. Mitigation: assert determinism by emitting twice and diffing.
- **The cache salt invalidates every warm cache once.** Expected and desired.

## 7. Definition of done

Overview done-conditions 1, 5 and 7 pass; `run_build_tests.sh` green **and run
by CI with sanitizer archives**; goldens committed; `table` round-trip fixture
green; module-origin predicate landed unused-by-enforcement; full gate list green
in both build modes; docs updated (Principle I); KI entries updated.

## 8. Traceability

REQ-002 → G2/G3 (interface completeness) · REQ-007 → G2 · REQ-009 → G1 ·
REQ-011 → enum variants keep shipping unchanged (D17; ABI invariant recorded in
audit b.1 — variant/payload lists are layout, not just API).

## 9. Open question — BLOCKING, raised not guessed

**Q-U2-1 — Can the private-`init` marker be specified in U2 at all?**

The manager's U2 scope includes "record the factory as a struct attribute plus
private-`init` presence" (ruling Q3), so that a consumer's `Counter(5)` against a
private constructor says *"constructor of 'Counter' is private"* rather than
*"type 'Counter' has no constructor"*.

The difficulty is sequencing, and it is real rather than a preference:

- The natural, no-new-syntax representation is `pub init(...)` vs `init(...)` —
  exactly D9's model.
- **`pub` does not parse inside `impl` blocks until U3** (`QImplBlock.cpp:125`),
  which is decision A3 and KI-4.
- Therefore in U2 **no `.bmod` can contain a private `init`**: every init is
  exported under the interim semantics. A marker emitted in U2 would have no
  producer, no fixture could exercise it, and Principle II would be satisfied
  only by a test asserting a hand-written `.bmod` — testing the parser, not the
  behaviour.

**My recommendation** (not applied without a ruling): U2 defines the
representation in this spec — `pub init` present ⇒ externally constructible;
`init` present without `pub` ⇒ declared but private — and **U3 lands both the
producer and the diagnostic**, in the same unit that makes a private `init`
expressible. U2 still bumps the format version, so U3's addition is a further
bump (M4 already requires one per unit).

The alternative — introducing a bmod-only marker syntax in U2 purely to hold the
information a unit later would express with `pub` — adds a second way to say the
same thing, which Principle I ("one right way") exists to prevent.

**I have not guessed.** Implementation proceeds on everything else; this item
stays open until ruled on.
