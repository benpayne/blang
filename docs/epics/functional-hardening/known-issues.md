# Known issues — functional-hardening

This file records matrix bugs that the epic's behavioral tests surfaced but that
were **deferred** rather than fixed, under the fix-or-file policy
([workplan.md](workplan.md) · [design.md](design.md)).

## Policy (machine-enforced)

- Every deferred bug is a structured `### KI-N` entry with a fenced `Repro:`
  block and a `Justification:` line.
- Acceptance FAILS if this file holds **more than 3** entries
  (`grep -c '^### KI-' ≤ 3`). Beyond 3, the hire must raise an Open Question to
  the manager rather than bulk-defer.
- The two **seeded bugs** — S1 (struct-valued field reassignment) and S2 (`Map`
  via `bcc` / `import collections;`) — are **required fixes** and may **never**
  appear here. Both were fixed (S1 in U1 `CGStruct.cpp`; S2 in U4
  `qcc.cpp`/`bcc.cpp`).

## Status at close-out (U5)

**0 deferred bugs.** Every test the four matrix units (U1 ARC, U2 operators, U3
interactions, U4 stdlib-via-`bcc`) authored **passes** with a committed stdout
golden; the ARC matrix (`codegen_arc_*.b`) plus the seeded reassignment test are
leak-clean under `--leak-check`; both seeded bugs are fixed. The bugs surfaced by
the matrices were fixed in-unit, not deferred:

- U1: seeded S1 struct-valued field reassignment (CGStruct.cpp).
- U3: B1 generic method-chain→field, B2 int-literal→`long` method argument, B3
  string-returning method call as `==` operand (CGStruct.cpp / CGExpressions.cpp).
- U4: seeded S2 `Map` via `import collections;` through the real `bcc` driver.
- U5: the pre-existing full-suite leak in `codegen_db_query.b` (discarded
  `query T;` `Array<T>` rvalue never released) — fixed in CGRuntime.cpp
  (`genQueryExpression` now `trackTempArray`s the returned array so a discarded
  result is released at statement end). This was a bounded, low-risk fix
  mirroring the existing temp-array pattern, so it was fixed rather than filed or
  quarantined.

No `### KI-N` entries are present, so `grep -c '^### KI-'` is `0` (≤ 3).
