# Spec: Aggregate/field ARC matrix (+ seeded S1 fix)

**Epic**: functional-hardening · **Unit**: U1 · **Branch**: `epic/functional-hardening/u1-arc-matrix`
**Covers**: REQ-001 (aggregate/field ARC matrix, leak-clean) + REQ-005 (seeded fix S1: struct-valued field reassignment)
**Speckit**: `arc-matrix` · **Status**: Draft (awaiting spec audit)

## Problem

4 of the 5 bugs surfaced in the 2026-07-19 coverage evaluation were refcount
(ARC) handling in **field/aggregate contexts** — nested-field read, nested-field
write, string-field assignment (fixed), and **struct-valued field reassignment**
(seeded, S1, still broken). The suite tests refcounted types (string, Array,
struct) largely in isolation; it under-samples what happens when a refcounted
value is **stored into a struct field, an `Array<struct>` element, a `Map` value,
or an `Option` payload**, then read back and dropped. Those are exactly the ARC
edge cases that leak or double-free.

This unit builds a behavioral **ARC matrix** over that surface, every test
carrying a stdout golden AND run under `--leak-check`, and fixes the seeded
struct-valued field reassignment bug (S1).

### Confirmed seeded bug S1 (must be fixed, must never be filed)

Reproduces today on this branch's base:

```
struct Inner { int v; }
struct Outer { Inner inner; }
fn main() -> int {
    Outer o = Outer { inner: Inner { v: 1 } };
    o.inner = Inner { v: 99 };
    assert o.inner.v == 99, "struct-field reassignment";   // fails: reads back 1
    return 0;
}
```

Observed: the write `o.inner = Inner { v: 99 }` does not take effect — the field
still reads `1`, the assert fails (exit 1). A struct-valued field assignment
must store the new value (and release the previously-held struct reference,
retain the new one — leak-clean).

## Scope

**In scope**
- ~7 behavioral ARC tests named `test_files/codegen_arc_*.b` (the `--leak-check`
  acceptance glob depends on this prefix — load-bearing invariant), each with a
  committed stdout golden and each leak-clean under `--leak-check`.
- The seeded S1 fix, with its regression test
  `test_files/codegen_struct_field_reassign.b` (+ golden), also covered by the
  leak glob at acceptance.
- Fixing (or, if large/risky/needs-a-language-decision, filing under the bounded
  fix-or-file policy) any additional ARC bug a matrix test surfaces.

**Out of scope**
- Operator, interaction, and stdlib-via-`bcc` matrices (U2/U3/U4).
- New language features; harness changes. Reuse `test_codegen.sh` as-is.
- Deep generic-protocol dispatch.

## Named test cases (the matrix)

Each `codegen_arc_*.b` asserts concrete values internally AND prints a
deterministic line sequence captured by a committed `<name>.expected.out`
golden. All run under `./test_codegen.sh --leak-check` with 0 leaks.

| # | File | Shape covered | Key assertions / drop behavior |
|---|------|---------------|-------------------------------|
| 1 | `codegen_arc_string_field.b` | `string` stored into a struct field, read back, struct dropped; **field reassigned** to a new string (old string released, not leaked) | field reads correct both times; reassignment visible; no leak/double-free |
| 2 | `codegen_arc_struct_field.b` | refcounted (heap) `struct` stored into another struct's field, read through, then whole aggregate dropped | inner field values read correct; drop releases inner once |
| 3 | `codegen_arc_array_of_struct.b` | `Array<struct>` — push heap structs, read element fields, mutate an element field, array dropped | element field reads/mutations correct; each element released once at drop |
| 4 | `codegen_arc_map_struct_value.b` | `Map<string, struct>` (inline-defined Map to avoid the S2 module bug owned by U4) — `set`/`get` struct values, read a field of a fetched struct, map dropped | fetched struct field correct; values released at drop |
| 5 | `codegen_arc_option_struct.b` | `Option<struct>` — `some(structValue)`, `match`/unwrap to a field, `none` arm; dropped | unwrapped field correct; payload released once; `none` path leak-clean |
| 6 | `codegen_arc_nested_writethrough.b` | 2-level nesting (`Outer.mid.inner.v`) — read-through AND write-through of a leaf field; also reassign an intermediate refcounted field | leaf reads/writes correct through both levels; intermediates released once |
| 7 | `codegen_arc_self_assign.b` | self-assignment of a refcounted field (`o.s = o.s`) and of a struct field (`o.inner = o.inner`) | value unchanged; **no double-free** (retain-before-release ordering); leak-clean |

**Plus the seeded fix regression test:**

| S1 | `codegen_struct_field_reassign.b` | struct-valued field reassignment (`o.inner = Inner { v: 99 }`), the confirmed S1 repro | after reassignment `o.inner.v == 99`; old inner released; leak-clean |

That is **7 `codegen_arc_*.b` + 1 `codegen_struct_field_reassign.b` = 8 new
`codegen_*.b` tests** toward the epic's ≥ 20 target (85 → 93 after U1).

## The S1 fix (implementation sketch — reviewer confirms during code audit)

The parser/sema accepts `o.inner = Inner { v: 99 }`; codegen for a struct-valued
field assignment (`CGStruct.cpp` `genFieldAssignment`, the LHS-is-field-access
path) must:
1. Evaluate the RHS struct value (a heap pointer for a user struct).
2. Store it into the field slot (currently the store appears to be dropped or
   mistargeted — the field still reads the old pointer).
3. ARC discipline: **release** the struct reference previously held in the field
   slot, **retain** the new one (or transfer the RHS temporary's owned refcount
   into the slot without an extra retain), so the net refcount is balanced — no
   leak, no double-free. Verified under `--leak-check`.

**Soft-conflict note (manifest `soft_conflicts: [U1, U3]`):** this fix is
expected to land in `CGStruct.cpp`, which U3 (interaction matrix) may also touch.
Per the workplan, if the fix lands in `CGStruct.cpp` the manager serializes U1
and U3. This is flagged to the manager as an Open Question up front (see below).

## Fix-or-file policy (this unit)

Any ARC bug a matrix test surfaces beyond S1 is, in order of preference:
1. **Fixed** — test passes, leak-clean, committed into the suite; or
2. **Filed** — a structured `### KI-N` entry in
   `docs/epics/functional-hardening/known-issues.md` (fenced `Repro:` block +
   `Justification:` line); the failing test is NOT committed into the passing
   suite. Filing is only for large/risky/language-decision fixes and is raised
   to the manager as an Open Question first. Global cap: ≤ 3 `### KI-` entries
   across the whole epic. **S1 may never be filed.**

## Acceptance (this unit — reviewer re-runs independently)

```bash
# builds clean, both modes
cmake --build build -j"$(nproc)"
cmake --build build-parse -j"$(nproc)"

# suites green, both modes
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh
./test_codegen.sh                      # all pass, incl. the 8 new tests w/ goldens

# the ARC matrix exists and is leak-clean (0 leaks; glob non-empty)
test -n "$(ls test_files/codegen_arc_*.b 2>/dev/null)"
./test_codegen.sh --leak-check test_files/codegen_arc_*.b test_files/codegen_struct_field_reassign.b

# seeded S1 fixed (+ golden), and NOT present in known-issues
./test_codegen.sh test_files/codegen_struct_field_reassign.b
! grep -q 'struct_field_reassign\|struct-valued field reassign' docs/epics/functional-hardening/known-issues.md 2>/dev/null

# fix-or-file bounded
ki=$(grep -c '^### KI-' docs/epics/functional-hardening/known-issues.md 2>/dev/null || echo 0); test "$ki" -le 3
```

## Success criteria

- **SC-001**: `ls test_files/codegen_arc_*.b` is non-empty (≥ 7 files) and all
  pass under `./test_codegen.sh` with committed goldens.
- **SC-002**: `./test_codegen.sh --leak-check test_files/codegen_arc_*.b
  test_files/codegen_struct_field_reassign.b` exits 0 with `Leaks: 0`.
- **SC-003**: `codegen_struct_field_reassign.b` passes (S1 fixed) and S1 does
  not appear in `known-issues.md`.
- **SC-004**: `codegen_*.b` count is ≥ 93 (baseline 85 + 8); both `run_tests.sh`
  modes, `test_codegen.sh`, and `ctest` stay green.
- **SC-005**: any additional ARC bug is fixed or filed (≤ 3 total structured
  `### KI-` entries); nothing committed failing.

## Open Questions (raised to the manager before implementation)

- **OQ-U1-1**: The S1 fix is expected to land in `CGStruct.cpp`
  (`genFieldAssignment`). Manifest records a soft conflict `[U1, U3]` on that
  file. Requesting the manager serialize U1 before U3's `CGStruct` edits (or
  confirm U3 has not yet touched it), and that U3 rebase onto U1's merge. No
  action needed from me beyond this flag; proceeding with U1 as the earlier
  unit.

## Assumptions

- Existing `test_codegen.sh` golden + `--leak-check` machinery is reused
  unchanged (Non-goal: no new harness).
- Inline-defined `Map` (copied into the test file, as `codegen_map.b` does) is
  used for the `Map<_,struct>` ARC test so U1 does not depend on U4's S2 fix.
- The S1 fix is correctness-only; default (non-sanitizer) build output for
  existing tests is unchanged.
