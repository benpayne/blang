# Design audit — U4 stdlib opaque API

**Principle VI artifact**: written and reviewed **before** implementation of the
accessor surface (epic decision **A4** — the DTO→opaque conversion is public-API
design, and mechanical field→getter generation is a record-level non-goal).

Status: **awaiting reviewer approval** (`critic`). Implementation of §3 does not
start until this is approved in the PR.

---

## 1. KI-8 / KI-10 — diagnosis

One fact produced three separate wrong answers: **the implicit `self` parameter's
declared type name is the literal string `"self"`**; it does not name the enclosing
struct. Every predicate that resolved a receiver by looking its declared *type name*
up in `mStructDefMap` therefore missed on a `self` receiver and silently answered
"not a struct".

**KI-10** was the first symptom: `println("{}", self)` handed a raw struct pointer to
the string runtime because the print path could not tell `self` was a `Printable`
struct.

**KI-8(b)** was the same miss one level down, and was **not** a rendering bug — it was
a use-after-free. `methodReturnTypeName` missed on `self.describe()`, so `isStringType`
said the expression was not a string, so `genReturnStatement` skipped the retain it
owes a returned string, and `releaseTempStrings()` then freed the very value being
returned:

```llvm
%s = call ptr @P_describe(ptr %self)
call void @__blang_string_release(ptr %s)   ; refcount 1 -> 0, FREED
ret ptr %s                                  ; caller gets a dangling pointer
```

The caller read a freed `BlangString` whose length field had been reused, so the
program aborted in `concat_many`. `string s = p.delegate();` read `s.length` as 4 for
a 3-character string before dying on the next read. This is why the earlier naive
"track it too" patch produced a **double-free**: it added a release without fixing
the missing retain, treating the symptom at one site instead of the resolution rule.

**Fix at the root**: a single `receiverStructDef(Expression*)` answers "which struct
is this receiver?" for every caller — resolving `self` through `mSelfStructMap`, a
field access through `getFieldTypeName` (Sema leaves self-based field types
unannotated, so this closes the same hole one level down), a generic parameter
through the active substitution, and gating on `isUserStructType` so builtin names
cannot slip through. `methodReturnTypeName`, the print-dispatch site and the
interpolation site all route through it, replacing three ad-hoc lookups.

**Second defect, same root, fourth site**: `genMethodCall` had **no branch for a FIELD
receiver**. `h.inner.num()` resolved to no struct, so the call was dropped on the
floor with no diagnostic — printing nothing for a string result and 0 for an int.
Pre-existing on master. It matters here because composition is exactly the idiom the
opaque-accessor API pushes code towards.

Evidence: `test_files/codegen_method_return_delegation.b` (committed **red** in
`8a4d044`, green in `2cf851a`) covers one and two levels of delegation, a bound
result read twice (a freed string survives one read by luck), `Printable` whose
`to_string` delegates via all three spellings, a field receiver from inside and
outside the owning struct, a parameter receiver, and a borrowed-field return for
contrast (which must keep its retain and must not get a second one).
`--leak-check`: 159/0, **Leaks: 0** — no leak and no double-free.

## 2. Sweep — where consumers reach into stdlib fields

Scope: `examples/` (6 `.b`), `test_build/` (9), `test_files/` (438), `demos/` (15),
`stdlib/` (12), root `test.b`. `.length`/`.is_empty` on `string`/`Array` are builtin
pseudo-fields and are excluded.

**Inventory**: all stdlib structs live in 4 files (`collections.b`, `buffer.b`,
`fs.b`, `net.b`). `cli/timer/io/math/time/random/env/sys` define **no structs** — zero
reach-in surface. No `table struct` and no `@json` anywhere in stdlib.

| Type | Reach-ins in `examples/` (**U4 scope**) | Elsewhere (**U5 scope**) |
|---|---|---|
| `HttpRequest` | `todo_app:146,147,160,178` (`path`,`method`,`body`) | `demos/13,14`; `test_files/codegen_http*.b` (+8 literal sites) |
| `Socket` | `chat:93,101,107` (`fd`); `chat:50` **literal** | — |
| `Map` | `wordfreq:39` (`counts.keys`) | `codegen_map_hashed.b:28` (`m.buckets` — white-box) |
| `HttpResponse` | none | `codegen_bcc_net_utils.b`, `codegen_http_*.b` (17 reads) |
| `HttpRequestLine` | none | `codegen_http_blang.b`, `codegen_bcc_net_utils.b` (13 reads) |
| `HttpParsedHeaders` | none | `codegen_http_blang.b` (7 reads) |
| `Route`, `HttpServer` | none | `test_files/` literals only |
| `Buffer`, `File`, `FileInfo`, `ServerSocket`, `Selector` | **none** | **none** — already method-only |

`test_build/` has **zero** reach-ins (its only stdlib importer is `timerapp`, and
`timer.b` has no structs). So DC8's gate is carried entirely by `examples/`.

**Highest-risk sites, all U5's** (recorded so U5 inherits the analysis):
1. `codegen_http_routing.b:48-50` fabricates an `HttpServer` from its three
   underscore-prefixed internals to unit-test routing offline. No public alternative
   exists; needs an offline constructor + `routes()` accessor, or a rewrite.
2. `codegen_map_hashed.b:28` reads `m.buckets.length` to assert the open-addressing
   table grew. No accessor can express this without exposing the representation;
   either a `bucket_capacity()` test hook or relax the test.
3. `chat:50` `net.Socket { fd: f }` rebuilds a Socket from a raw int — needs
   `socket_from_fd`. **This one is U4's**, being in `examples/`.

## 3. Per-type API rationale

**Naming**: accessors take the field's own name (spike S5 proves a method may share a
field's name and dispatch correctly). Rejected: `get_`-prefixed names and
`_list`-suffixed names — they invent vocabulary for no benefit and make the U5
migration a rename instead of adding `()`. The two pre-existing `get_`-named methods
(`Socket.get_fd`, `FileInfo.get_size`) are left alone: renaming them is churn that
buys nothing this unit, and `get_fd` is already the spelling `demos/14` uses.

**`HttpParsedHeaders` is the one designed surface, not a getter pair.** Exposing
`keys()`/`values()` would export the parallel-array *representation* — precisely the
thing the epic exists to stop, and the representation most likely to change (a hashed
lookup is the obvious future). The surface is what consumers actually want:
`has(name)` / `get(name)` for lookup, `count()`/`key(i)`/`value(i)` for ordered
iteration. Rejected: returning a `Map<string,string>` — it drags a two-argument
generic into an exported signature, which is exactly what KI-19 says the parser
rejects before P9 sees it.

**`Map`/`Set` do get plain `keys()`/`values()`/`items()`** — unlike
`HttpParsedHeaders`, the arrays *are* the concept for a map, and `wordfreq` iterates
them. They return `Array<K>` / `Array<V>`: **single**-argument generics, so no
KI-19 exposure.

**Types with zero reach-ins get nothing.** Adding speculative accessors to `Buffer`,
`File`, `ServerSocket`, `Selector` would be the mechanical generation A4 bans. The
sweep is the evidence that nothing is missing.

## 4. Open question

**Q-U4-1** (spec §6) — `Map`/`Set` `pub init` (audit AF-1) has **no spelling**: a
constructor call on a generic type does not parse, and static methods on generic
structs are not monomorphized. Delivering it needs a parser or codegen change outside
U4's file set and outside D1–D17. Raised, not improvised. The rest of U4 is
unaffected.
