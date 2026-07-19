# Spec: Interaction matrix (feature combinations in value contexts)

**Epic**: functional-hardening · **Unit**: U3 · **Branch**: `epic/functional-hardening/u3-interaction-matrix`
**Covers**: REQ-003 (interaction matrix — feature combinations in value contexts)
**Speckit**: `interaction-matrix` · **Status**: Draft (awaiting spec audit)
**Serialization**: after U1 (merged) per OQ-U1-1 — this unit edits `CGStruct.cpp`; branch is cut from `master` (already has U1 + U2). Rebase onto `master` before merge.

## Problem

The 2026-07-19 coverage evaluation found the suite is ~85% isolation, ~13%
interaction — and the interaction tests that exist were added *reactively* after
bugs. This unit builds a behavioral **interaction matrix** over feature
combinations in value contexts (match-bind→field/method, `Option`/`Result`
unwrap→field, method-chain→field, array-of-struct element mutate, for-in over
aggregates using element fields), each with a stdout golden, and fixes the bugs
those combinations surface.

### Grounding probes (run on this branch's base — `master` @ c2eb656, real behavior)

Most interaction shapes **work** today (largely thanks to U1's ARC/field fixes)
and this unit adds golden coverage for them. Three combinations are **broken**
and this unit fixes them:

**Working (add golden coverage):**
- non-generic method-chain→field: `box.get().x` → correct.
- function-return→field: `make().y` → correct.
- `Option` unwrap→field via `match some(pt) { pt.x }` → correct.
- `Result` unwrap→field via `match ok(p) { p.x }` → correct.
- match-bind→method: `match some(pt) { pt.sum() }` → correct.
- array-of-struct element mutate: `arr[0].x = 42` → correct.
- for-in over `Array<struct>` using element fields: `for p in pts { sum += p.y }` → correct.

**Broken (fix in this unit):**
- **B1 — generic method-chain→field returns empty.** `m.get("a").x` (where
  `Map<string,Point>.get` returns the monomorphized `Point`) prints **empty**,
  while `Point p = m.get("a"); p.x` prints the correct value. So a
  **generic** method-call result used **directly** in a field access is
  mis-typed/mis-read. (This is the bug U1 deferred to U3 — a generic-return-type
  / monomorphization issue in method-chain field access, not ARC.)
- **B2 — int-literal → `long` method argument ICEs.** `obj.method(11)` where the
  parameter is `long` emits invalid IR → `internal compiler error: generated IR
  failed verification` (`Call parameter type does not match function signature!
  i32 11 ... i64`). The int→long width promotion done for free-function calls is
  missing at **method-call** argument sites. (Raised by U4 as **OQ-U4-A**;
  assigned here as a method-call interaction bug.)
- **B3 — string-returning method call as a `==` operand compares wrong.**
  `x.get() == "hi"` is **false** even though `x.get()` yields `"hi"`; storing to a
  `string` variable first (`string s = x.get(); s == "hi"`) is correct. String
  operand detection (`isStringType`) does not recognize a `MethodCallExpression`
  returning `string`, so `==` uses non-string (pointer/int) comparison instead of
  `__blang_string_equals`. (Raised by U4 as **OQ-U4-B**; assigned here.)

## Scope

**In scope**
- ~5 behavioral `test_files/codegen_ix_*.b` tests, each with a committed stdout
  golden, covering the interaction shapes above (working AND fixed).
- **Fix B1, B2, B3.** B1 and B2 land in `CGStruct.cpp` (method-call
  codegen / generic return handling); B3 lands in `CGExpressions.cpp`
  (`isStringType`). These are U3's serialized `CGStruct.cpp` edits (U1 merged, so
  the soft conflict is resolved; rebase onto `master` at merge).
- Fixing (or, if large/risky, filing under the bounded fix-or-file policy) any
  further interaction bug a matrix test surfaces.

**Out of scope**
- ARC/aggregate (U1), operator (U2), stdlib-via-`bcc` (U4) matrices.
- Deep generic-protocol dispatch; new language features; harness changes.
- `--leak-check` is not a required gate for this unit (no ARC-specific changes;
  B1's fix is type/monomorphization, not refcounting). Tests still must not leak,
  but the acceptance gate is behavioral goldens.

## Named test cases (the matrix)

Each `codegen_ix_*.b` asserts concrete values internally (binary exits 0 only when
correct) AND prints a deterministic line sequence captured by a committed
`<name>.expected.out` golden. All deterministic (no network/threading).

| # | File | Shape covered | Key assertions / golden |
|---|------|---------------|-------------------------|
| 1 | `codegen_ix_method_chain_field.b` | method-chain→field: non-generic (`box.get().x`), function-return→field (`make().y`), and **generic** (`m.get(k).x` where `Map<_,Point>.get` returns `Point`) — the B1 fix | all three read the correct field; generic case **fails pre-fix** (empty), passes post-fix |
| 2 | `codegen_ix_match_bind.b` | `Option` unwrap→field, `Result` unwrap→field, match-bind→**method** (`pt.sum()`), and a `none`/`err` arm | unwrapped fields/methods correct on all arms |
| 3 | `codegen_ix_array_struct.b` | `Array<struct>` element field **read** and **mutate** (`arr[i].x = v`), and **for-in over `Array<struct>`** summing an element field | element reads/mutations correct; for-in sum correct |
| 4 | `codegen_ix_method_str_cmp.b` | string-returning method call used **directly** as a `==` operand AND inside string interpolation (`"{x.get()}"`) — the B3 fix | `x.get() == "hi"` is **true** (fails pre-fix), interpolation prints the string; also `!=` |
| 5 | `codegen_ix_method_long_arg.b` | int-literal and int-variable passed to a `long` method parameter (the B2 fix), plus a `long` local passed through, and a mixed int/long expression arg | method returns correct `long`; **ICEs pre-fix**, passes post-fix |

That is **5 `codegen_ix_*.b`** tests. Running epic total after U1+U2+U4+U3:
93 + 6 + 3 + 5 = **107 ≥ 105** (target 85 + 20).

## The fixes (implementation sketch — reviewer confirms at code audit)

- **B1 (generic method-chain→field), `CGStruct.cpp`:** when a method call's
  resolved (monomorphized) return type is a user struct, the returned temporary
  must be given that concrete struct type so a subsequent `FieldAccessExpression`
  on it resolves the field offset correctly (today the generic return is
  mis-typed, so the field read lands on the wrong slot / a null temp). Mirror the
  non-generic method-return field-access path, using the monomorphized return
  type.
- **B2 (int→long method arg), `CGStruct.cpp`:** at method-call argument
  generation, apply the same integer width promotion (`SExt`/`ZExt` for `byte`,
  matching the free-call path in `CGExpressions.cpp::genCallExpression` ~line
  306) so an `int` argument widens to a `long`/`i64` parameter instead of
  emitting a type-mismatched call.
- **B3 (string-returning method call in comparison), `CGExpressions.cpp`:**
  extend `isStringType(Expression*)` to return true for a `MethodCallExpression`
  whose resolved return type is `string` (analogous to the existing
  `CallExpression`-returns-string handling), so `==`/`!=` route to
  `__blang_string_equals`.

Each fix is correctness-only; default build output for existing tests is
unchanged. All three land in method-call / expression codegen; none changes ARC
semantics. Because B1/B2 touch `CGStruct.cpp` (the U1↔U3 soft-conflict file),
this unit is serialized after U1 (merged) and rebases onto `master` before merge.

## Fix-or-file policy (this unit)

Any interaction bug a matrix test surfaces beyond B1–B3 is, in order of
preference: **Fixed** (test passes, committed) or **Filed** (a structured
`### KI-N` entry in `docs/epics/functional-hardening/known-issues.md` — fenced
`Repro:` + `Justification:`; the failing test is NOT committed passing; raised to
the manager as an Open Question first). Global cap ≤ 3 `### KI-` entries across
the epic.

## Acceptance (this unit — reviewer re-runs independently)

```bash
# builds clean, both modes
cmake --build build -j"$(nproc)"
cmake --build build-parse -j"$(nproc)"

# suites green, both modes
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh
./test_codegen.sh                      # all pass, incl. the 5 new codegen_ix_*.b tests w/ goldens
ctest --test-dir build

# the interaction matrix exists with goldens
test -n "$(ls test_files/codegen_ix_*.b 2>/dev/null)"
for f in test_files/codegen_ix_*.b; do test -f "${f%.b}.expected.out"; done

# fix-or-file bounded
ki=$(grep -c '^### KI-' docs/epics/functional-hardening/known-issues.md 2>/dev/null || echo 0); test "$ki" -le 3
```

## Success criteria

- **SC-001**: `ls test_files/codegen_ix_*.b` non-empty (≥ 5) and all pass under
  `./test_codegen.sh` with committed goldens.
- **SC-002**: B1 (generic method-chain→field), B2 (int→long method arg, no ICE),
  and B3 (string-method-call in `==`) are fixed — each corresponding test fails
  pre-fix and passes post-fix.
- **SC-003**: both `run_tests.sh` modes, `test_codegen.sh`, and `ctest` stay
  green; `codegen_*.b` count increases by ≥ 5 (to ≥ 107 with U1+U2+U4).
- **SC-004**: any further interaction bug is fixed or filed (≤ 3 total `### KI-`);
  nothing committed failing.

## Open Questions / coordination

- **Resolves OQ-U4-A and OQ-U4-B**: the two method-call bugs U4 surfaced (int→long
  method arg ICE; string-method-call in `==`) are adopted here as B2/B3 — they are
  method-chain→value-context interactions, this unit's domain. No separate KI is
  filed for them (they are fixed here).
- **Rebase note**: cut from `master` (U1 + U2). If U4 merges first, rebase onto
  the new `master` (U4 touches `qcc.cpp`/`bcc.cpp`, not `CGStruct.cpp`, so no
  conflict is expected) and re-run gates before merge.

## Assumptions

- Existing `test_codegen.sh` golden machinery reused unchanged.
- Fixes are correctness-only; inline test structs use the `impl` block form
  (matching `codegen_map.b`).
