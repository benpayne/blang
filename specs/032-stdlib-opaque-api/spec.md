# Spec: stdlib public-API redesign for opaque types

**Epic**: modules-v2-exports · **Unit**: U4 · **Branch**: `epic/modules-v2-exports-u4`
**Covers**: REQ-010 (+ KI-8 / KI-10, folded in by the 2026-08-05 manager ruling)
**Depends on**: U2 (merged, `a00bb20`); U3 merged (`54fc372`) · **Speckit**: `032-stdlib-opaque-api`
**Status**: Draft — §6 carries one BLOCKING open question (Q-U4-1)

Binding inputs: design record D1–D17 (`docs/epics/modules-v2/overview.md`),
epic decisions A1–A7 (`../../docs/epics/modules-v2-exports/design.md`, esp. **A4**:
the accessor surface is hand-designed per type and design-audited *before*
implementation), `known-issues.md` KI-3, KI-8, KI-10, KI-19, and standing checks
SC-1 / SC-2 (constraint F-1).

---

## 1. Problem

U5 makes member variables always-private and struct literals module-private. Every
consumer that reads a stdlib struct's field today breaks at that flip. U4 ships the
**target surface** those consumers migrate onto, and moves `examples/` onto it ahead
of the flip, so U5 is a rule change rather than a rule change *plus* an API design.

This is API **design** work, not churn (review F8, decision A4). Mechanical
field→getter generation is a record-level **non-goal**.

Two renderer defects (KI-8, KI-10) were folded into this unit because they break
exactly the spellings this surface pushes authors towards: once fields are private,
a method call and `Printable` are the only ways to read data out of an opaque type.
They are **fixed and merged on this branch already** (§7).

## 2. Goals

- **G1** — every stdlib struct a consumer reaches into gains a `pub` accessor/method
  surface sufficient to replace the reach-in, hand-designed per type (§4).
- **G2** — `tools/check_no_field_reachins.sh examples/ test_build/` exits **0**, with
  a real enumerated maintained field list (DC8).
- **G3** — all `examples/` compile and their integration scripts pass on the new
  surface.
- **G4** — KI-8 and KI-10 fixed with committed regression tests, **before** any U5
  corpus migration (hard constraint from the workplan).
- **G5** — docs updated per Principle I (`docs/language_design.md`, `CLAUDE.md`).

## 3. Non-goals

- **The U5 corpus migration.** `test_files/` and `demos/` reach-ins are U5's. DC8
  scopes the gate to `examples/` and `test_build/` deliberately. This unit does not
  migrate `test_files/codegen_http*.b`, `demos/13_http_server.b`,
  `demos/14_file_server.b`, or `test_files/codegen_map_hashed.b`.
- **Making fields actually private.** Field access keeps working this epic-unit; U4
  ships the target surface only. Enforcement is U5.
- **`buffer` / `collections` / `cli` module-private enforcement** — exempt this epic
  (A7, KI-3). Accessors are still *added* to `Map`/`Set` (they are needed by
  `examples/wordfreq`), but no visibility rule is applied to them here.
- **The flat-merge resolution path** (`qcc.cpp`), any **generic factory**, and
  anything under `runtime/` — untouched, per the epic scope guards.
- **KI-15, KI-18** — U5's.

## 4. Design — the accessor surface

Derived from an exhaustive sweep of `examples/`, `test_build/`, `test_files/`,
`demos/`, `stdlib/` (recorded in `design-audit.md` §2). Types with **zero** consumer
reach-ins (`Buffer`, `File`, `ServerSocket`, `Selector`) get no new surface — their
existing method surface already suffices; the sweep is the evidence.

**Naming rule (enabled by spike S5, §5): an accessor takes the field's own name.**
A method may share a name with a field and dispatch is correct, so migration is
purely *adding `()`* — the smallest possible diff, and no invented vocabulary
(`get_`/`_list` suffixes) leaks into the public API. `self.method` inside the
accessor still reads the field.

| Type | New `pub` surface | Replaces |
|---|---|---|
| `net.HttpRequest` | `method()`, `path()`, `body()` | `req.method` / `.path` / `.body` |
| `net.HttpResponse` | `status()`, `content_type()`, `body()` | `resp.status` / `.body` / `.content_type` |
| `net.HttpRequestLine` | `method()`, `path()`, `version()` — **new impl block** | `reqline.method` etc. |
| `net.HttpParsedHeaders` | `count()`, `key(i)`, `value(i)`, `has(name)`, `get(name)` — **new impl block** | `hdrs.keys[i]` / `hdrs.values[i]` |
| `fs.FileInfo` | mark existing `exists`/`is_file`/`is_dir`/`get_size` **`pub`** | (already method-only) |
| `net.Socket` | mark existing `get_fd()` **`pub`** | `conn.fd` |
| `collections.Map` | `keys()`, `values()` | `counts.keys` |
| `collections.Set` | `items()` | (none in `examples/`; symmetry) |

`HttpParsedHeaders` is the one type that gets a **designed** surface rather than
getters: the parallel-array representation is precisely what should not be public.
`has`/`get` are the operations consumers actually want; `count`/`key`/`value` retain
ordered iteration without exposing the arrays.

### 4.1 Construction

`HttpResponse` needs no builder — every consumer already goes through
`http_ok`/`http_json`/`http_not_found`/`http_response`. `HttpRequest`, `Route`,
`Socket` are literal-constructed by consumers today; free constructors
(`net.http_request(...)`, `net.route(...)`, `net.socket_from_fd(int)`) are the U5
migration target. **Only `socket_from_fd` is required by U4**, because
`examples/chat/main.b:50` is the sole `examples/` literal site; the others serve
`test_files/`, which is U5's. They are specified here so U5 inherits a design, not a
decision.

### 4.2 P9 / KI-19 accounting

`Map`/`Set` accessors return `Array<K>` / `Array<V>` — **single**-argument generic
types. The multi-argument form (`Map<string,Secret>`) that KI-19 flags is **not**
introduced into any exported signature by this unit, so U4 does not rely on P9
having checked a construct the parser rejects first. This is stated explicitly
because "no P9 error on a `Map` signature" must not be read as "P9 checked it".

## 5. Spike findings (run before design, per Principle VI / A4)

| # | Question | Result |
|---|---|---|
| S5 | May a method share a name with a field? | **Yes** — compiles, and runs correctly (`r.method()` → method, `self.method` → field; printed `GET /x`). This is what makes §4's naming rule viable. |
| S1 | Does `Box<int>()` (ctor call on a generic) parse? | **No** — `error: Failed parse value`. `QExpression.cpp:369-416` accepts `Name<Args>` only before `{`, never before `(`. |
| S3 | Does a zero-arg `static fn new()` on a generic work? | Parses; **fails codegen**: `undefined function 'new' (mangled: Box_new)`. |
| S4 | Does an *inferable* `static fn make(T)` on a generic work? | Parses; **fails codegen** the same way. Static methods on generic structs are not monomorphized. |

S1/S3/S4 together are the basis of Q-U4-1.

## 6. Open questions

### Q-U4-1 (BLOCKING for one clause) — `Map`/`Set` `pub init` has no spelling

The workplan (audit **AF-1**) requires U4 to *"add `pub init` to `Map` and `Set` —
their only construction form today is the struct literal U5 outlaws — and migrate
the literal call sites with their goldens"* (14 real sites).

**There is no way to construct a generic struct other than a struct literal.** All
three candidate spellings fail (§5): the constructor call does not parse, and static
factory methods on generic structs are not monomorphized by codegen.

Delivering this clause therefore requires **either**:

- **(a)** a parser change — accept `Name<Args>(...)` and build a `ConstructExpression`
  carrying the type args, plus codegen to monomorphize and call the instantiated
  `init`; or
- **(b)** a codegen change — monomorphize `static fn` on generic structs, then
  `Map.new()` / `Map.make(...)`; still needs LHS-type inference for the zero-arg case.

Both are **language/compiler surface changes**, outside U4's stated file set
(`stdlib/*.b`, `examples/`), and neither is covered by D1–D17.

**Why it does not block the rest of U4**: struct literals are not field reach-ins, so
DC8's gate does not flag them; `collections` is exempt from module-private
enforcement this epic (A7/KI-3), so `Map`/`Set` literals keep working; and the one
`examples/` `Map` reach-in (`wordfreq:39 counts.keys`) needs an **accessor**, not
`init`. G1–G5 are all deliverable without an answer.

**Recommendation**: defer the clause to U5 (which owns the literal flip and would
otherwise have to design the construction spelling twice), or split it into its own
unit. Raised rather than improvised, per the epic execution rules.

## 7. KI-8 / KI-10 — already landed on this branch

| Commit | Content |
|---|---|
| `5e3cfdc` | Interpolation renders dotted paths, structs and `self` — or rejects them (KI-8(a), KI-10). Golden `codegen_print_printable.expected.out` had **frozen the KI-10 bug** (it expected the literal text `({self.x}, {self.y})`); it now expects `(10, 20)`. |
| `8a4d044` | KI-8(b) regression test, committed **deliberately red**. |
| `2cf851a` | KI-8(b) fix — one `receiverStructDef()` resolves a receiver's struct for every caller. Also fixed a second defect at the same root: `genMethodCall` had no FIELD-receiver branch, so `h.inner.num()` was silently dropped. |

Diagnosis and evidence: `design-audit.md` §1.

## 8. Test plan

- **DC8 gate**: `tools/check_no_field_reachins.sh examples/ test_build/` exits 0.
  The script's field list stays a **real enumeration** — a wildcard that trivially
  passes is explicitly rejected. `FileInfo:name` (a field that does not exist) is
  removed; `Map`/`Set` fields are added.
- **Negative leg**: the gate must be shown to have teeth — a deliberately
  reintroduced reach-in makes it exit non-zero.
- **`examples/`**: every integration script passes. `todo_app`'s libm link failure is
  **environmental** (dangling `libsqlite3.so` symlink) and settled — it must not be
  read as a regression.
- **Gates**: `run_tests.sh` + `test_codegen.sh` green in **both** build modes;
  `test_codegen.sh --leak-check` clean; `test_lsp.sh`; `test_build/run_build_tests.sh`;
  `ctest`.
- **SC-1 / F-1**: this unit adds **no new `.bmod` construct** (it adds `pub` methods to
  stdlib source, which the existing emitter already handles), so F-1's fixture
  obligation is not triggered. SC-1 continues to run via `test_build/`.

## 9. Traceability

| Goal | Requirement | Verified by |
|---|---|---|
| G1 | REQ-010 | §4 surface implemented; sweep in `design-audit.md` §2 is the completeness argument |
| G2 | REQ-010 / DC8 | gate exits 0 + negative leg |
| G3 | REQ-010 / DC8 | `examples/*/test_*.sh` |
| G4 | KI-8, KI-10 | `codegen_method_return_delegation.b`, `codegen_print_printable.b` |
| G5 | Principle I | `docs/language_design.md`, `CLAUDE.md` |
