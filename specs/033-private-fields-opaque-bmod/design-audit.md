# Design audit — U5 private fields, opaque `.bmod`, D15 metadata

**Principle VI artifact**: the `spec.md` design was reviewed by the secondary reviewer
`rev` **before** implementation of the enforcement flip. This file records the audit
outcome so the code review and the epic-close functional review can trace it.

## Verdict

- **Round 1**: `CHANGES-REQUESTED` — one blocking finding (point 1); points 2–5 APPROVED.
- **Round 2** (point-1 re-audit): **APPROVE-FOR-IMPLEMENTATION**. Principle VI gate satisfied.

## Blocking finding (point 1) and its resolution

The draft keyed the field/literal visibility rules on a proposed `isImported()` accessor
overloading `mFromInterface`, with a false "from-interface == imported" claim. It is
false for combine-mode namespaced stdlib (`net`/`fs`/`timer`): those parse from `.b`
source under `--combine`, so `qcc.cpp:463` never calls `setFromInterface(true)` for them
— they are cross-module yet `isFromInterface() == false`. `Type.h` (443-446, 467-475)
forbids overloading `mFromInterface` for visibility.

Resolved by the **option-A reframe** (rev ruled it sufficient and lower-risk than
threading scope-context through U5a):

1. Removed `isImported()` and the false equivalence; the rules key on the existing,
   honestly-named `isFromInterface()` = "arrived through a parsed `.bmod`".
2. Stated explicitly that U5's field/literal enforcement is **`.bmod`-path-only** this
   epic; combine-mode namespaced-stdlib field/literal privacy stays **grep-gated**
   (`check_no_field_reachins.sh`), not Sema-enforced — owned by Epic B (KI-16 / KI-23).
3. Reconciled the overview's combine-mode constraint; filed **KI-23** naming the Type.h
   contract contradiction and assigning the combine-mode gap to Epic B; fixed §10.
4. Softened §4.4 — the existing `BuildCacheTest.cpp:61-66` generic format-version test
   already covers the 3→4 bump; no new test needed.

Approved points 2–5 (data-contract `.bmod` retention realized as in-body fields for
`table`/`@json` structs; OQ#1 symmetric-builtin proposal; Q-U4-1 construction spelling
option a; the P9 plain-field fixture flipping to a `pass` fixture rather than being
deleted) were carried into implementation unchanged.

## Implementation refinements recorded post-audit

- **Generic imported structs excluded from Rules 1 & 2 in U5a** (spec §5, §4.2): coupled
  to the Q-U4-1 construction spelling (U5b), since a generic struct has no non-literal
  construction form yet and enforcing would break `test_build/myapp` with no migration
  path. A phasing refinement, not a scope cut.
- **`genToJsonCall` declares the imported `_to_json` extern** (CGRuntime.cpp) so an
  imported `@json` struct is serializable cross-module — a pre-existing untested gap
  (todo_app is single-file) that DC-6 is the first to exercise (SC-2).
