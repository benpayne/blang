# Spec: Operator matrix (bitwise / shift / % / compound / short-circuit)

**Epic**: functional-hardening · **Unit**: U2 · **Branch**: `epic/functional-hardening/u2-operator-matrix`
**Covers**: REQ-002 (operator matrix — unguarded primitives get behavioral goldens)
**Speckit**: `operator-matrix` · **Status**: Draft (awaiting spec audit)

## Problem

The 2026-07-19 coverage evaluation found the **unguarded primitives** have
essentially **zero behavioral test coverage**: bitwise (`& | ^`), shift
(`<< >>`), `%`, compound `%=`/`^=`, and `&&`/`||` short-circuit are exercised
only incidentally (if at all) and none carries a stdout golden. This unit builds
an **operator matrix** — one behavioral golden test per operator family — over
that surface, and fixes (or files) anything that misbehaves.

### Grounding probe (run on this branch's base — real current behavior)

I compiled small probe programs through the **real `bcc` driver** before writing
this spec. Results:

- **Bitwise `& | ^`, shift `<< >>` on `int`**: correct. `12 & 10 == 8`,
  `12 | 10 == 14`, `12 ^ 10 == 6`, `12 << 2 == 48`, `12 >> 1 == 6`,
  `-8 >> 1 == -4` (arithmetic shift right on signed `int` — correct).
- **`%` and compound `%=` / `^=`**: correct. `12 % 10 == 2`, `6 %= 4 → 2`,
  `12 ^= 10 → 6`.
- **`&&` / `||` short-circuit is BROKEN** — this is the unit's primary fix.
  `false && side(1)` **calls** `side(1)` (prints its side effect); `true ||
  side(2)` **calls** `side(2)`. The final boolean *value* is correct (`false` /
  `true`), but the RHS **side effect is not skipped**. Root cause confirmed in
  `CGExpressions.cpp::genOperationsExpression` (lines 390–391): both operands are
  evaluated eagerly at function entry *before* the operator is dispatched, so the
  `&&`/`||` cases at lines 489/499 only combine two already-computed booleans —
  there is no branch, no short-circuit.
- **`byte` unsigned semantics are partial**: `byte` is an unsigned 8-bit type
  (per the compiler's documented `byte` design — `zext` on cast). But `>>` on a
  `byte` uses **arithmetic** shift (`CreateAShr`), and a `byte` prints as a
  **signed** `i8`. So `byte b = 200; b >> 1` yields `-28` (signed `-56 >> 1`)
  instead of `100`, and `byte 255` prints as `-1`. See "byte handling" below for
  the fix-or-file decision.
- **Out of scope (not operator-correctness bugs, will be avoided in tests):**
  hex integer literals are unsupported (`0xF0` lexes to `0`) — a lexer feature,
  not this unit; extended compound bitwise assigns `&= |= <<= >>=` do **not
  parse** (`Failed parse varible`) — a parser feature. The workplan names only
  `%=`/`^=`, which are supported. Tests use **decimal literals only** and only
  the `%=`/`^=` compound forms.

## Scope

**In scope**
- ~6 behavioral `test_files/codegen_op_*.b` tests, each with a committed
  `<name>.expected.out` stdout golden, covering: bitwise `& | ^`; shift
  `<< >>` (incl. signed arithmetic-right); `%` (incl. negative operands);
  compound `%=` and `^=`; `&&`/`||` short-circuit **with side-effect ordering**;
  `byte` unsigned bitwise/shift.
- **Fix the `&&`/`||` short-circuit bug** so RHS side effects are skipped when
  the result is already determined by the LHS. This lands in
  `CGExpressions.cpp` (NOT `CGStruct.cpp` — no soft-conflict with U1/U3).
- The `byte` unsigned-shift/print discrepancy: **fix if small and safe**
  (logical shift + unsigned print for `byte`), otherwise **file** under the
  bounded fix-or-file policy.

**Out of scope**
- ARC / aggregate matrices (U1), interaction matrix (U3), stdlib-via-`bcc` (U4).
- Hex/binary/octal integer literals (lexer feature); extended compound bitwise
  assignment operators `&= |= <<= >>=` (parser feature). Neither is required by
  the workplan (`%= ^=` only) and both are separate features, not correctness
  bugs in the operators this unit covers.
- Float bitwise (nonsensical) and any new operators.

## Named test cases (the matrix)

Each `codegen_op_*.b` asserts concrete values internally (so the binary exits 0
only when correct) AND prints a deterministic line sequence captured by a
committed `<name>.expected.out` golden. All are deterministic (no
network/threading), so none is quarantined.

| # | File | Shape covered | Key assertions / golden content |
|---|------|---------------|---------------------------------|
| 1 | `codegen_op_bitwise.b` | `& | ^` on `int` with several operand pairs, precedence with `+`/`==`, and unary `~` if supported | `12 & 10 == 8`, `12 | 10 == 14`, `12 ^ 10 == 6`, a masking example (`0b`-free decimal masks), each printed and asserted |
| 2 | `codegen_op_shift.b` | `<< >>` on `int` including a **negative** LHS (`-8 >> 1 == -4`, signed arithmetic right) and a shift that builds a power of two (`1 << 30`) | printed + asserted; golden shows `-4`, `48`, `1073741824`, etc. |
| 3 | `codegen_op_modulo.b` | `%` on `int` incl. a **negative** dividend (`-7 % 3`), and `%` inside a larger expression | printed + asserted; golden fixes the C-semantics result of negative `%` (`SRem`) |
| 4 | `codegen_op_compound.b` | compound `%=` and `^=` (the two supported extended compound assigns), applied repeatedly, mixed with `+=` | printed + asserted; `6 %= 4 → 2`, `12 ^= 10 → 6`, chained updates |
| 5 | `codegen_op_short_circuit.b` | `&&`/`||` **side-effect ordering** — a helper `fn touch(int) -> bool` that prints `"touch N"` and returns true. Four cases: `false && touch(1)` (RHS must **NOT** run), `true && touch(2)` (RHS **must** run), `true || touch(3)` (RHS must **NOT** run), `false || touch(4)` (RHS **must** run). Also asserts the final boolean value of each. | **This is the teeth test for the fix.** Golden contains exactly `touch 2` and `touch 4` (from the two cases whose RHS must run) and **not** `touch 1`/`touch 3`. Fails pre-fix (all four `touch N` print); passes post-fix. |
| 6 | `codegen_op_byte.b` | `byte` unsigned bitwise (`&`,`|`,`^`) and unsigned right shift — `byte 200 >> 1 == 100`, `byte 240 | byte 15 == 255`, printed as unsigned | printed + asserted with the **correct unsigned** results. If the byte fix is deferred (filed), this test is NOT committed passing; its repro lives in `known-issues.md` and the byte-shift case is dropped from the committed matrix. |

That is **6 `codegen_op_*.b`** tests toward the epic's ≥ 20 target (93 → 99
after U2, contingent on the byte decision — at minimum 5 land if byte is filed).

## The short-circuit fix (implementation sketch — reviewer confirms at code audit)

`CGExpressions.cpp::genOperationsExpression` currently evaluates **both**
operands (`left`, `right`) at lines 390–391 before dispatching on the operator.
For `&&`/`||` this is wrong: it forces the RHS. The fix special-cases `&&` and
`||` at the **top** of the function, before the eager operand evaluation:

1. Read `ops->mOp`. If it is `&&` or `||`, handle it here and return — do **not**
   fall through to the eager `left`/`right` evaluation.
2. Evaluate the LHS, coerce to `i1` (`!= 0` for int, `!= 0.0` for float).
3. Create three basic blocks (`rhs`, `merge`) in the current function; branch on
   the LHS bool:
   - `&&`: if LHS true → `rhs`, else → `merge` (result `false`).
   - `||`: if LHS true → `merge` (result `true`), else → `rhs`.
4. In `rhs`, evaluate the RHS, coerce to `i1`, branch to `merge`. Record the
   predecessor block *after* RHS codegen (nested control flow can change the
   current block).
5. In `merge`, a `phi i1` selects between the short-circuit constant
   (`false` for `&&`, `true` for `||`) from the entry block and the RHS bool from
   the `rhs` predecessor.
6. Return the phi (an `i1`, matching the existing logical-op result type).

The existing `op == "&&"` / `op == "||"` cases at lines 489/499 (which combine
two pre-evaluated booleans with `CreateAnd`/`CreateOr`) become dead and are
removed. Correctness-only; default build output for existing tests unchanged
(only side-effect *ordering* changes, and no existing test relies on the RHS of a
short-circuit running).

## byte handling (fix-or-file decision, recorded for the reviewer)

`byte` is unsigned. Two discrepancies: (a) `>>` on a `byte` should be a
**logical** shift (`CreateLShr`), not arithmetic; (b) a `byte` value should print
as **unsigned** (0–255). The intended path is to **fix both** if the change is
localized and low-risk (operand-type-aware shift selection in
`genOperationsExpression`, mirroring the existing `isByteExpression` helper used
elsewhere for casts; unsigned widening on the print/to_string path for `byte`).
If either fix turns out to touch broad codegen or needs a language decision
(e.g. whether `byte` prints unsigned everywhere), it is **filed** as a single
`### KI-N` entry with a repro + justification and raised to the manager as an
Open Question first — NOT bulk-deferred. The `codegen_op_byte.b` shift case is
then dropped from the committed suite (its bitwise `&|^` cases, which are
correct today, may still ship). Global KI cap across the epic: ≤ 3.

## Fix-or-file policy (this unit)

Any operator bug a matrix test surfaces is, in order of preference:
1. **Fixed** — the test passes, committed into the suite; or
2. **Filed** — a structured `### KI-N` entry in
   `docs/epics/functional-hardening/known-issues.md` (fenced `Repro:` block +
   `Justification:` line); the failing test is NOT committed into the passing
   suite. Filing is only for large/risky/language-decision fixes and is raised to
   the manager as an Open Question first. Global cap: ≤ 3 `### KI-` entries
   across the whole epic.

## Acceptance (this unit — reviewer re-runs independently)

```bash
# builds clean, both modes
cmake --build build -j"$(nproc)"
cmake --build build-parse -j"$(nproc)"

# suites green, both modes
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh
./test_codegen.sh                      # all pass, incl. the new codegen_op_*.b tests w/ goldens
ctest --test-dir build                 # runtime units stay green

# the operator matrix exists with goldens
test -n "$(ls test_files/codegen_op_*.b 2>/dev/null)"
for f in test_files/codegen_op_*.b; do test -f "${f%.b}.expected.out"; done

# short-circuit has teeth: the golden proves the skipped RHS never printed
grep -q 'touch 2' test_files/codegen_op_short_circuit.expected.out
grep -q 'touch 4' test_files/codegen_op_short_circuit.expected.out
! grep -q 'touch 1' test_files/codegen_op_short_circuit.expected.out
! grep -q 'touch 3' test_files/codegen_op_short_circuit.expected.out

# fix-or-file bounded
ki=$(grep -c '^### KI-' docs/epics/functional-hardening/known-issues.md 2>/dev/null || echo 0); test "$ki" -le 3
```

## Success criteria

- **SC-001**: `ls test_files/codegen_op_*.b` is non-empty (≥ 5 files) and all
  pass under `./test_codegen.sh` with committed goldens.
- **SC-002**: a short-circuit golden proves the RHS side effect is **skipped**
  when it should be (`touch 1`/`touch 3` absent) and **run** when it should be
  (`touch 2`/`touch 4` present); the test fails pre-fix and passes post-fix.
- **SC-003**: bitwise `& | ^`, shift `<< >>`, `%`, and compound `%=`/`^=` each
  have a passing golden test.
- **SC-004**: `codegen_*.b` count increases by ≥ 5 (93 → ≥ 98); both
  `run_tests.sh` modes, `test_codegen.sh`, and `ctest` stay green.
- **SC-005**: the `byte` discrepancy is fixed (unsigned shift + print) or filed
  as one structured `### KI-` entry (≤ 3 total); nothing committed failing.

## Assumptions

- Existing `test_codegen.sh` golden machinery is reused unchanged (no new
  harness). `--leak-check` is not required for this unit (no ARC changes; these
  are value-type operator tests).
- The short-circuit fix is correctness-only for *values*; only side-effect
  ordering changes, and no existing test relies on a short-circuited RHS running.
- Decimal integer literals only (hex unsupported); `%=`/`^=` are the only
  extended compound assigns exercised.
```
