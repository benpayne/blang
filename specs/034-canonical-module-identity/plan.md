# U1 implementation plan: canonical module identity

**Epic**: modules-v2-graph · **Unit**: U1 (keystone) · **Covers**: REQ-001, REQ-002
**Design artifact** (Principle VI) — companion to
`docs/epics/modules-v2-graph/design-audit-U1.md` (auditor SIGNED OFF 2026-08-09,
with the one non-blocking clarity item folded below). **Binding**: D2, D5, D6, D10.

This plan grounds the design audit in the implementation and records the auditor's
non-blocking clarity item plus a regression-critical mechanism derived during recon.

## 1. The two sources of a defining-module digest (auditor clarity item — FOLDED)

A generic type's mangled symbol must carry the **defining module's** 12-hex digest
(D2/D10). There are exactly **two** sources for that digest, and they must not be
conflated — stating the split keeps **done-condition 1 decoupled from U5**:

- **(a) Own module + directly-imported deps → digest supplied driver-side (U1).**
  When compiling a module, the driver (`bcc`/`qcc`) has already resolved the module
  itself and every **direct** dependency to a canonical origin (`realpath`/
  `url@pin`). So the driver can compute each such module's digest and stamp its
  definitions — **including a dep's definitions that arrived via the dep's `.bmod`**
  — without the digest being carried *inside* the `.bmod`. **Done-condition 1 is
  achievable in U1 alone** because both boxlib deps are *direct* deps of the binary:
  the driver knows both origins.
- **(b) Un-named transitive foreign generics → digest from the `.bmod` carrier
  (U5).** When a consumer monomorphizes a generic from a module it never imported
  (done-condition 7b, the "sharpest corner"), the driver has no direct edge to that
  module, so the digest must be **carried in the `.bmod`** (U5's foreign-ref
  format). U1 states this contract; U5 ships the carrier.

## 2. Regression-critical mechanism (derived at recon — MUST hold)

`mStructDefMap` (`CodeGen.cpp:77/401`) and scope `addSymbol` are keyed by bare
**name**, so two same-named generics cannot coexist in one front-end compilation
(P2 drops the second). Therefore:

- **Done-condition 1's fixture instantiates each `Box<int>` INSIDE its own
  library**, not in the consumer. Each library's `.a` carries its own
  `linkonce_odr Box_<digest>_int`; the binary links both. The collapse P10 warns
  about is at **link** time, and U1's identity-in-mangling makes
  `Box_<digestA>_int` ≠ `Box_<digestB>_int` — distinct symbols, `nm`-verifiable.
  The consumer need not *name* `Box` (per-module scopes to name two same-named
  types is U4/U6), so the front-end name collision never arises.

- **Cross-module generic dedup must not regress (the co-ship consequence).**
  Existing `test_build/mathlib`+`myapp` link because mathlib's `.a` and the
  consumer both emit `Pair_int` and `linkonce_odr` dedups them. Once mangling
  includes identity, mathlib emits `Pair_<digestMath>_int`; the **consumer must
  emit the identical symbol** or the dedup breaks (duplicate/undefined at link).
  Since mathlib is a **direct dep**, source (a) applies: the driver passes
  mathlib's origin so the consumer **stamps mathlib's `Pair` (from its `.bmod`)
  with mathlib's digest**, reproducing `Pair_<digestMath>_int`. This is why U1
  needs `bcc`→`qcc` **dep-origin plumbing**, not only self-origin. Without it, U1
  would break cross-module generics — the failure the co-ship invariant (B1)
  predicts, and the reason U1 and U5 co-ship.

## 3. Implementation shape

1. **Identity/digest in shared frontend plumbing** (`Frontend.h`/`QModule.cpp`,
   next to `stampDefiningOrigin`): `moduleDigest(portableOrigin)` = 12 hex of
   SHA-256 (reuse `sha256.{h,c}`). `realpath` failure → **located hard error**,
   never empty identity.
2. **Origin resolution in the driver** (`bcc`/`qcc` main): compute the portable
   origin (project-root-relative inside the invoking top-level project's
   `blang.toml` dir, else realpath + KI note) for the compiled module **and each
   direct dep `.bmod`**; pass dep origins to `qcc` (new association: each `.bmod`
   input carries its origin).
3. **Stamp definitions with their module digest**: extend the origin stamp so each
   `StructDefinition`/`FunctionDefinition`/`EnumDefinition` carries its defining
   module's digest (own module for source defs; the dep's origin for `.bmod` defs).
4. **`mangleGenericName` incorporates the defining def's digest** — looked up from
   the definition (so it is uniform for own-module and driver-stamped foreign
   defs), applied identically at every call site (`CGStruct.cpp`,
   `CGStatements.cpp`) and every emit site (`CGTypes.cpp:348/497`,
   `BmodEmitter.cpp:185`), recursing into nested type args.

## 4. Done-condition 1 fixture (`test_build/`)

Two path-dep libs `boxlibA`/`boxlibB`, each `pub struct Box<T>` + a `pub fn` that
instantiates `Box<int>` **internally** and returns a primitive (so the consumer
never names `Box`). Binary `boxapp` depends on both, calls both fns. Fixture script
asserts via `nm`/IR that the two `Box_..._int` symbols are **distinct** (P10 gone),
plus a **shared-origin dedup** companion (one lib instantiated twice → one symbol),
plus a **`bcc clean` warm-cache guard** documenting the U1↔U5 window (B1). Keep
`mathlib`/`myapp` green (the cross-module-dedup regression guard, §2).

## 4a. Co-ship consequence (manager-confirmed 2026-08-09)

The `bcc`→`qcc` dep-origin plumbing in §2 is **not a scope expansion** — it is the
expected consequence of the U1/U5 co-ship (B1) invariant and is squarely in-scope
under clarity-note **source (a)** (own module + directly-imported deps stamped
driver-side). It is the only correct way to land the mangling change without
regressing cross-module generics (mathlib/myapp).

**Digest origin key (U1 decision, correctness-first).** The serialized digest keys
on **`realpath` of the module's project dir** — a value both the dep's own build
(`bcc --module-origin <realpath(projectDir)>`) and any consumer referencing it
(`bcc --bmod-origin <depBmod>=<realpath(depDir)>`) compute identically on one
machine, so `linkonce_odr` dedup is preserved. **Cross-machine reproducibility is
waived** for this pass (the design's own fallback for out-of-workspace deps) and
recorded in Known Issues; the in-workspace project-root-relative refinement is a
follow-on that must preserve dep/consumer digest agreement. In single-file/combine
mode (no `bcc`), the origin defaults to `realpath(inputFile)` per module — no
cross-`.bmod` reference exists there to disagree with.

## 5. Gates

`run_tests.sh` (both modes), `test_codegen.sh` (+`--leak-check`), `test_lsp.sh`,
`test_build/run_build_tests.sh` all green; the new fixture's `nm` assertions pass;
`mathlib`/`myapp` still link (dedup preserved). Docs: `CLAUDE.md` +
`docs/language_design.md` (identity/mangling note) per Principle I.
