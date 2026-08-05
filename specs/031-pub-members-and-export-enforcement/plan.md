# Plan: U3 — `pub` on impl members; private-by-default; P9 enforcement

**Spec**: [spec.md](spec.md) · **Unit**: U3 · **Branch**: `epic/modules-v2-exports-u3`

Ordered so the tree stays green at each step and the golden churn (KI-4) lands
once, in a step whose whole purpose is that churn.

| # | Step | Files | Proves |
|---|------|-------|--------|
| 1 | Parse `pub` on `fn`/`init`/`static fn` in impl blocks; extend the rejection diagnostic | `QImplBlock.cpp` | G1 |
| 2 | Positive parse fixtures + a same-module private-method call | `test_files/pass/` | G1, test 4 |
| 3 | `.bmod` emission filter — `pub` members only, **non-generic structs only** | `BmodEmitter.cpp` | G2 |
| 4 | Bump `kFormatVersion` to 3; private-`init` producer (`pub init` vs `init`) | `BmodFormat.h`, `BmodEmitter.cpp` | G3 |
| 5 | Private-`init` diagnostic at the construction site | `CGStruct.cpp`, `Sema.cpp` | G3, test 5 |
| 6 | `fail/xmodule/` harness: find-exclusion, inline leg, canonical-gate arm | `run_tests.sh` | G5 |
| 7 | `fail/xmodule/` fixtures (non-`pub` method; private `init`) | `test_files/fail/xmodule/` | DC3 |
| 8 | P9 enforcement, one surface item at a time (§4.4 items 1–5) | `Sema.{h,cpp}` | G4 |
| 9 | `fail/sema` fixtures per P9 item + the design record's reproduction | `test_files/fail/sema/` | DC4 |
| 10 | F-1 fixture: user-defined `pub`/private members with a **negative absence leg** | `test_build/` | F-1 |
| 11 | `mDefiningFile` → module mapping + populate on the blangd path | `Type.h`, `qcc.cpp`, `lsp/Compile.cpp` | G6 |
| 12 | Regenerate every golden `.bmod`; explain each diff in the PR | `test_files/golden/bmod/` | KI-4 |
| 13 | Docs (Principle I) + KI/status updates | `CLAUDE.md`, `docs/` | gate 5 |

## Sequencing notes

- **Step 1 before 3** — and this ordering is load-bearing, not tidiness.
  Finding F-B: generic structs ship methods as verbatim source slices, so the
  moment any generic method is written `pub`, the slice contains `pub fn`. A
  consumer that cannot yet parse `pub` in an impl block fails to read the whole
  interface. The parser must accept it before anything emits it.
- **Step 3 is the trap.** The filter applies to **non-generic** structs only.
  Generic structs keep full layout and ALL bodies (A6) — their `pub` methods call
  private helpers, so filtering them breaks every cross-module generic. Step 8's
  test 8 guards this; do not "tidy" it.
- **Step 4 with step 3, not before** — the format version documents a shape
  change, so it moves in the step that changes the shape.
- **Steps 6 before 7** — the harness before the fixtures that need it, so a
  failing fixture means a failing rule rather than a missing runner.
- **Step 8 incrementally, one P9 surface item per commit.** Five distinct
  declaration sites; landing them together makes the diff unreviewable and any
  false positive hard to attribute.
- **Step 12 last.** Goldens regenerated before steps 3–4 settle would be rewritten
  twice, and the churn would hide which change moved what.

## The golden-churn discipline (KI-4)

Every committed golden `.bmod` moves in this unit. That is the intended signal.
The PR must show, per golden, **which step moved it and why** — a golden diff
nobody explains is a format change nobody reviewed.

`test_build/sizeapp`'s `secret = 0` expectation will stop compiling at step 3;
U2 documented that break in place as intended. Fix by marking the method `pub`
or deleting the call — do not weaken the filter to preserve it.

## Not in this unit

Flat merge (Epic B); generic factory; generic emission filtering;
`buffer`/`collections`/`cli` enforcement; field layout / D15 (U5); KI-15 foreign
conformance records (U5); KI-8/KI-10 print renderer (U4, before U5's migration).
