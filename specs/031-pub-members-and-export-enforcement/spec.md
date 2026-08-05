# Spec: `pub` on impl members; private-by-default; P9 export enforcement

**Epic**: modules-v2-exports · **Unit**: U3 · **Branch**: `epic/modules-v2-exports-u3`
**Covers**: REQ-003, REQ-006, REQ-011 (enforcement half)
**Depends on**: U2 (merged, `a00bb20`) · **Speckit**: `031-pub-members-and-export-enforcement`
**Status**: Draft

Binding inputs: design record D1–D17 (`docs/epics/modules-v2/overview.md`),
epic decisions A1–A7 (`../../docs/epics/modules-v2-exports/design.md`),
`known-issues.md` KI-1..KI-15, and the epic's standing checks SC-1/SC-2
(`../../docs/epics/modules-v2-exports/overview.md`).

---

## 1. Problem

Three gaps, all of which U1 and U2 deliberately left open.

1. **`pub` does not parse inside `impl` blocks.** `QImplBlock.cpp:125` accepts
   only `fn` / `init` / `static fn`. So there is no way to say which methods are
   API, and U2 ships **every** non-generic method into the `.bmod` (KI-4). The
   interface currently over-exports by construction.

2. **A private type may appear in an exported signature (P9).** The library
   builds green and the failure lands on the *consumer*, as a syntax error inside
   a generated file they never wrote. This is one of the two pain points the
   epic exists to fix, and it is still open.

3. **A non-`pub` protocol named in a conformance record has no owner.** U2's
   emitter *skips* that record so it does not emit an unreadable interface
   (`BmodEmitter.cpp`), but nothing rejects the combination at the library build.

## 2. Goals

- G1 — `pub` parses on `fn`, `init` and `static fn` inside `impl` blocks.
- G2 — method/`init` visibility is **private by default** (D9); the `.bmod`
  emits only `pub` members **for non-generic structs**.
- G3 — the private-`init` producer *and* its diagnostic land together, per the
  Q-U2-1 ruling; format bumps to **3**, where `pub init` means externally
  constructible and a bare `init` means declared-but-private.
- G4 — P9 enforcement at the **library** build, as a located error, over the
  full exported-signature surface (§4.4).
- G5 — a `fail/xmodule/` fixture class in `run_tests.sh` for consumer-side
  rejections (DC3).
- G6 — `mDefiningFile`'s two inherited obligations discharged (§4.6).

## 3. Non-goals

- **The flat-merge resolution path** (`qcc.cpp:330-381`) — Epic B. Untouched.
- **Any generic factory** (A6). Generic structs keep **full field layout and ALL
  method bodies** — pub and private alike — because consumers monomorphize and
  pub methods call private helpers (`Map.set` → `find_slot`/`rehash`).
  **Visibility for generics is a Sema resolution rule only; emission is
  unchanged.** This is the single easiest thing in this unit to get wrong.
- **`buffer`/`collections`/`cli` enforcement** — exempt this epic (A7, KI-3).
- **Dropping field layout / D15 metadata** — U5.
- **KI-15** (foreign-protocol conformance records) — U5.
- **KI-8/KI-10** (print/interpolation renderer) — U4, before U5's migration.

## 4. Design

### 4.1 `pub` in impl blocks

`ParseImplBlock`'s member loop accepts an optional leading `pub` before `fn`,
`init` and `static fn`, setting the member's existing `mIsPublic`. The rejection
diagnostic at `QImplBlock.cpp:125` gains `pub` to its accepted list.

### 4.2 Private by default; emission filter

`BmodEmitter::emitStructInterface` emits only members with `isPublic()`, **for
non-generic structs only**. A struct with no `pub` members emits no `impl` block
at all.

Consequence, expected and required (KI-4): **every committed golden `.bmod`
changes**, and `test_build/sizeapp`'s `secret = 0` line stops compiling — U2
documented that break in-place as intended.

### 4.3 The private-`init` representation (format 3)

Per the Q-U2-1 ruling, U2 defined the representation and U3 lands producer and
diagnostic together:

| Emitted | Meaning (format ≥ 3) |
|---|---|
| `pub init(...)` | externally constructible; the factory is callable |
| `init(...)` | declared, but **private** — construction is module-only |
| *(absent)* | the type declares no constructor at all |

The distinction exists so a consumer's `Counter(5)` says **"constructor of
'Counter' is private"** rather than "type 'Counter' has no constructor". An LLM
consumer can self-correct from the first and cannot from the second
(Principle VI). U1 already built the diagnostic seam in
`genConstructExpression`; U3 supplies the fact it branches on.

**`kFormatVersion` → 3.** A format-2 `.bmod` must not be read under format-3
rules: in format 2 an unmarked `init` is *exported*.

### 4.4 P9 enforcement surface

At the **library** build, in Sema, in all build modes. A non-`pub` type
referenced by any of these is a located `file:line:col: error:`:

1. `pub fn` parameter or return type
2. `pub` method / `pub init` parameter or return type
3. **Every `pub struct`'s field types**, for as long as the emitter ships field
   layout for that struct.

   **AMENDED by manager ruling (2026-08-05)** from the original "only `table` /
   `@json` structs". The sequencing argument is decisive: **U5 is the unit that
   REMOVES the layout**, so scoping this to annotated structs would leave the
   design record's own P9 reproduction live for exactly the interval U3 exists
   to close it — and close it later, by accident. While a plain `pub struct`'s
   fields are in the `.bmod`, their types cross the boundary.

   The data-contract distinction is still real, but it is the **U5** case, not
   the U3 scope limit: once layout is dropped, only `table`/`@json` structs keep
   field metadata (D15), and only those fields still need to be exported.
4. exported enum variant payload type (D17)
5. **a protocol named in an exported conformance record** (U2's deferral —
   without this it has no owner)
6. **the methods backing an exported conformance record must be `pub`** — on
   **non-generic** structs.

   Rule chosen (BLOCKER-2, option (a)): reject at the library build rather than
   implicitly exporting the method. Implicit export would make a method's
   visibility depend on a conformance declared elsewhere in the file, so reading
   `fn to_string(...)` would no longer tell you whether it crosses the boundary;
   rejecting keeps `pub` meaning exactly one thing — "this is API" — and keeps
   the error at the library that caused it rather than at the consumer that
   cannot fix it.

   **Non-generic only.** A generic struct ships ALL its method bodies (A6), so
   the `pub` filter never runs on it and a non-`pub` conformance method IS
   reachable. The premise does not hold there, and rejecting would force `pub`
   onto helpers D9 says are private.

Located at the offending *declaration*, not at a use site.

### 4.5 `fail/xmodule/` harness

Per audit F-E, already scouted: add `-not -path '*/fail/xmodule/*'` to the
`fail/` find (the precedent `fail/warn/` set); add an inline leg modelled on the
`fail/warn/` loop; **add an arm to the canonical `file:line:col` gate** at
`run_tests.sh:146-153`, or DC2/DC3's located-error requirement is not enforced.
Each fixture is a directory holding `lib.b`, `consumer.b`, `consumer.b.expected`;
the runner emits `lib.bmod` into a `mktemp -d`, then compiles
`consumer.b lib.bmod` and matches the pattern.

### 4.6 `mDefiningFile`'s two inherited obligations (M-3)

Both are required before any rule may key on it:

1. **Map file → module.** `mDefiningFile` holds a *file* base name, so a
   multi-file library yields several values. A module-private rule comparing
   those strings directly would **reject legal intra-library access**. U3 must
   resolve file → module (project name for a library, module name for a
   namespaced stdlib module).
2. **Populate it on the blangd path** (`lsp/Compile.cpp`), which does not set it
   today. A Sema rule keyed on an unpopulated field would diverge between `qcc`
   and the LSP — the same rule reporting different diagnostics in the editor and
   the compiler.

## 5. Test plan

| # | Test | Proves |
|---|------|--------|
| 1 | `pub fn` / `pub init` / `pub static fn` parse in an impl block | G1 |
| 2 | Unmarked method is absent from the `.bmod`; `pub` one is present (golden) | G2 |
| 3 | `fail/xmodule/` — calling a non-`pub` method cross-module is a located error | G2, DC3 |
| 4 | Positive — a private method is callable *inside* its own module | G2, DC3 |
| 5 | `fail/xmodule/` — constructing through a private `init` says *private*, not *no constructor* | G3 |
| 6 | `fail/sema` — one fixture per P9 surface item in §4.4 (five) | G4, DC4 |
| 7 | The design record's P9 reproduction: a `pub struct` referencing a non-`pub` type | G4, DC4 |
| 8 | Cross-module generics stay green; generic `.bmod`s still carry private helpers | non-goal guard |
| 9 | All goldens regenerated; every library still satisfies SC-1 | KI-4, SC-1 |

### Constraint F-1 (binding; manager ruling 2026-08-05)

> Every construct a unit adds to or changes in the emitted `.bmod` must be
> covered by a `test_build/` library+consumer fixture pair exercising it with a
> **USER-DEFINED instance declared in the fixture library** (user protocol,
> struct, enum, or annotation), and must **NOT** be covered solely by a compiler
> builtin or pre-registered name (`Printable`, `Option`, `Result`, `Array`,
> `string`, `chan`); each such fixture must include all three legs (library
> builds and emits its `.bmod`; the emitted `.bmod` re-parses standalone via
> `qcc --parse-only <lib>.bmod` exiting 0; a consumer builds and runs with an
> exact-output check); and where a construct can be omitted as well as emitted
> (a visibility filter, a skip predicate), a **negative leg must assert the
> omission by absence**.

**Rationale**: three P9-class breaks in this epic — `table pub struct` emitted in
the inverse of source order, a conformance record naming a user-defined protocol
emitted as a forward reference, and a record naming a non-exported protocol —
each shipped a library whose interface no consumer could read, and each was
invisible to a green suite because the corpus exercised only builtin
(`Printable`) instances.

U3's emission change is a **visibility filter**, so F-1's final clause applies
directly: there must be a negative leg asserting a non-`pub` member's **absence**
from the `.bmod`.

## 6. Risks

- **Generic emission filtering is the trap.** Filtering generic structs' bodies
  would break every cross-module generic (their pub methods call private
  helpers). The filter must apply to non-generic structs only; test 8 guards it.
- **Golden churn hides real change.** Every golden moves in this unit. Each diff
  must be read, not rubber-stamped — that is why the goldens exist (KI-4).
- **`mDefiningFile` misuse** (§4.6) would reject legal intra-library access.

## 7. Definition of done

Overview done-conditions 3 and 4 pass; the `fail/xmodule/` leg exists with the
canonical-diagnostic arm; every new fixture has a `.expected` pattern matching in
both build modes; cross-module generics stay green; goldens regenerated and each
diff explained in the PR; format version at 3; full gate list green; docs updated
(Principle I).

## 8. Open questions

None yet. Anything the docs mark open will be raised rather than guessed —
notably, if P9 enforcement over §4.4 item 3 (`table`/`@json` field types) turns
out to need D15 metadata that only U5 defines, that is a question, not a
judgement call.
