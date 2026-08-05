# Plan: U2 — the `.bmod` as a true interface

**Spec**: [spec.md](spec.md) · **Unit**: U2 · **Branch**: `epic/modules-v2-exports-u2`

Ordered so each step is independently reviewable and leaves the tree green.
Q-U2-1 (spec §9) blocks only step 7; everything else proceeds.

| # | Step | Files | Proves |
|---|------|-------|--------|
| 1 | Format-version constant, emitted as a comment line | `BmodEmitter.{h,cpp}` | G1 |
| 2 | Salt the version into the cache key | `BuildCache.{h,cpp}`, `bcc.cpp` | G1, DC7 |
| 3 | Cache-key unit test over the real `computeKey` via an injectable `formatVersion` (default = shipped constant) | `BuildCacheTest.cpp`, `CMakeLists.txt` | DC7 |
| 4 | `pub table struct` emission order + round-trip fixture | `BmodEmitter.cpp`, `test_build/tablelib` | G4 |
| 5 | Conformance records (`impl P for S`) | `BmodEmitter.{h,cpp}` | G2 |
| 6 | Imported-`Printable` E2E fixture with exact output | `test_build/` | DC5 |
| 7 | private-`init` marker — **ruled: deferred to U3** (representation defined in spec §9, with the format-2 vs format-3 qualifier) | `spec.md` | — |
| 8 | Golden `.bmod` files + `--update-goldens` | `test_files/golden/bmod/`, `run_build_tests.sh` | G3 |
| 9 | `mDefiningFile` origin predicate, populated not enforced (named for what it holds; U3 maps file → module and covers the LSP path) | `Type.h`, `qcc.cpp` | G5 |
| 10 | Prefix-aware factory coverage (KI-5 dead code) | `test_build/` | MINOR-B |
| 11 | `build-system` CI job + sanitizer provisioning | `.github/workflows/ci.yml` | G6 |
| 12 | Docs (Principle I) + KI updates | `CLAUDE.md`, `docs/` | gate 5 |

## Sequencing notes

- **Step 1 before 2**: the salt needs a constant to read.
- **Step 3 tests the real `computeKey`**, not a reimplementation (manager ruling
  on Q8). The seam is an optional `formatVersion` parameter defaulting to the
  shipped constant, so the test can vary the version *and* assert the default is
  the shipped one — an end-to-end warm-cache test would need the compiler rebuilt
  with a different constant mid-suite.
- **Step 8 last among emission changes**: goldens written before steps 4–5 would
  be rewritten twice, hiding which change moved them.
- **Step 9 lands unused.** The predicate is populated and asserted only for
  correctness of population; U3/U5 enforce. Landing it early keeps U3 and U4 (in
  parallel) from both inventing one.
- **Step 11 last**: the CI job should go green on the first run, so it lands once
  the suite it runs is final.

## Risk checks carried from the spec

- Verify conformance *checking* (`QImplBlock.cpp:149-240`) accepts a bodyless
  member as satisfying a protocol before emitting records (spec §6).
- Assert `.bmod` emission is deterministic (emit twice, diff) before committing
  goldens.

## Not in this unit

`pub` on impl members (U3), dropping layout (U5), flat merge (Epic B), generic
factory, `buffer`/`collections`/`cli` enforcement, `BuildCache` filename
weakness (KI-1).
