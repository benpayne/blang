# Design: functional-hardening

**Epic**: [overview.md](overview.md)

Product/architecture level. The hires pick exact test cases in their per-unit
speckit specs; this fixes the matrices, the fix-or-file mechanism, and the
seeded bugs.

## Context (from the 2026-07-19 coverage evaluation)

Suite today (live counts): `test_files/codegen_*.b` = 85, of which **78 have a
stdout golden** (51 non-empty, 27 empty-but-behavioral via internal
`assert`/exit-code ladders), 7 quarantined (non-deterministic net/threading).
Harness: `test_codegen.sh` (exact golden compare, `--selfcheck` teeth,
`--leak-check` fatal-on-leak), `ctest` runtime units, `fuzz_parse`. Strong.

The gap is **structural**, not neglect:
- **~85% isolation, ~13% interaction.** Interaction tests (`nested_field_access`,
  `array_of_structs`, `field_assign`, `method_chain`, …) were added *reactively*
  after bugs — their comments document the exact failures that prompted them.
- **Unguarded primitives:** bitwise/shift operators have **zero** behavioral
  tests; generic protocol constraints behaviorally untested; `%`, `&&/||`
  short-circuit, compound `%=`/`^=` are thin.
- **Aggregate/field ARC:** 4 of the 5 session bugs were refcount handling in
  field/aggregate contexts (nested field read/write, string-field assignment).
- **Driver integration hidden by the harness:** the harness combines stdlib
  manually, so `Map` "passes" in the suite while being **unusable via `bcc`**.

Empirical confirmation: 16 casual feature-combination programs → 2 broke
(struct-field reassignment; `Map` via `bcc`) — both real, both reproduce today.

## The four matrices

| Matrix | Shape covered | Notes |
|--------|---------------|-------|
| **Aggregate/field ARC** (U1) | refcounted values into struct fields (incl. reassignment), `Array<struct>`/`Map<_,struct>`/`Option<struct>` store→read→drop, nested write-through, self-assign | run under `--leak-check`; leaks/double-frees are fixes |
| **Operator** (U2) | `& \| ^ << >>`, `%`, `%= ^=`, `&& \|\|` short-circuit + side-effect order, signed vs `byte` | closes zero-coverage primitives; small, high-certainty |
| **Interaction** (U3) | match-bind→field/method, `Option`/`Result` unwrap→field, method-chain→field, array-elem mutate, for-in over aggregates | value-context combinations |
| **Stdlib-via-`bcc`** (U4) | `Map`/`collections` through `bcc`, untested `net`/`fs` utilities | tests go through the real driver, not just `--combine` |

Every deterministic test carries a stdout golden (load-bearing: the recent bug
passed its asserts while printing empty — only a golden caught it). ARC tests
additionally run under `--leak-check`.

## Fix-or-file mechanism

A matrix test that fails is resolved one of two ways, so the suite is always
green and nothing is silently dropped:
- **Fixed** → the test passes (leak-clean if ARC), committed into the suite.
- **Filed** → a structured entry in `docs/epics/functional-hardening/known-issues.md`:
  a `### KI-N` heading with a fenced `Repro:` block and a `Justification:` line;
  the failing test is **not** committed into the passing suite (it lives as the
  repro in the doc). Filing is only for large/risky fixes or ones needing a
  language decision, and is raised to the manager as an Open Question first.

**Machine backstop (not just prose):** acceptance FAILS if
`grep -c '^### KI-' known-issues.md` > **3** — beyond three unfixed matrix bugs
is a bulk-defer, which must instead be an Open Question to the manager. The two
seeded bugs are required fixes and may never appear in `known-issues.md`. This
gives the epic a **bounded end** even if a deep bug appears — the same
discipline test-validation used for fuzzing findings — without the
quarantine-everything escape hatch.

## Seeded bugs (required fixes, reproduce today)

| # | Bug | Repro | Owner |
|---|-----|-------|-------|
| S1 | Struct-valued field reassignment reads inconsistently | `o.inner = Inner{v:99}` then `assert o.inner.v == 99` fails (prints 99, assert fails) | U1 |
| S2 | `Map` from the `collections` **module** is unusable (deeper than first thought) | A program using `Map<string,int> { keys: [], values: [] }` fails whether `Map` comes via `import collections;` through `bcc` ("compilation failed") **or** via `qcc --combine stdlib/collections.b` (`error: Failed parse varible` at the `Map<K,V> x` declaration). Only an **inline-defined** `Map` works (that is why `codegen_map.b` — which copies the struct in-file — passes). So `collections.b` missing from `bcc.cpp:279` is *necessary but not sufficient*: U4 must also diagnose why a generic struct from a combined/imported module isn't resolved in a variable declaration. Use the full literal (`{ keys: [], values: [] }`) — `Map<..>{}` is a separate runtime problem. | U4 |

## Key decisions

| # | Decision | Rationale | Rejected |
|---|----------|-----------|----------|
| D1 | Bounded fix-or-file, not fix-everything | A single gnarly ARC bug shouldn't stall the whole epic; deferrals are documented, not hidden | Fix-everything (open-ended scope) |
| D2 | Goldens mandatory on deterministic tests | The recent bug passed its asserts while printing empty — only the golden catches display bugs | Assert-only (misses the display half) |
| D3 | ARC matrix runs under `--leak-check` | The bug class is refcounting; leaks/double-frees only show under ASan/LSan | Exit-code-only (misses the leak half) |
| D4 | Stdlib tested through `bcc`, not just `--combine` | The `Map` bug hid precisely because the harness bypasses the driver | Harness-only stdlib tests |
| D5 | Additive only — reuse the existing harness | goldens/selfcheck/leak-check/ctest already exist; this is test authoring + fixes | Build new harness |

## Invariants — must not break
- Existing green suites (`run_tests.sh` both modes, `test_codegen.sh`, `ctest`,
  `--leak-check`) stay green at every unit boundary.
- **ARC-matrix naming is load-bearing:** U1's ARC tests are named
  `test_files/codegen_arc_*.b`; the `--leak-check` acceptance glob depends on
  this prefix, and a different name silently makes the leak gate check nothing.
- No new test committed in a failing state; deferrals live in `known-issues.md`
  (≤ 3, structured `### KI-N`).
- Default (non-sanitizer) build output unchanged; fixes are correctness-only.
