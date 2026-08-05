# U1 spike write-up — cross-module construction ABI via a library-emitted factory

**Epic**: `docs/epics/modules-v2-exports` · **Unit**: U1 · **REQ**: REQ-001
**Design audit**: `docs/epics/modules-v2-exports/audit-u1.md` (rev 2)
**Outcome**: **the ABI works.** No wall hit; no fallback needed.

## The problem

Construction was caller-allocating. At a `Counter(5)` site the *consumer's*
codegen computed `DataLayout::getTypeAllocSize` (`CGStruct.cpp:641-642`),
generated the struct's destructor from its field list (`:660`), called
`__blang_rc_alloc_dtor(size, dtor)` (`:663`) and then `Counter_init` (`:667`).
A consumer reading a `.bmod` has neither the size nor the field list, so the
export model's "the interface carries no layout" goal was unreachable without an
ABI change (review finding F1).

Release was never the problem: `__blang_rc_release`
(`runtime/blang_runtime.c:115-131`) invokes the destructor pointer stored in the
allocation header by `__blang_rc_alloc_dtor` (`:77-84`). It is already
layout-free. **The asymmetry is what makes the factory sufficient with zero
runtime change** — and that was confirmed, not assumed: no file under `runtime/`
was touched by this unit.

## What was built

The defining module emits, for every non-generic struct whose `init` has a body:

```llvm
define ptr @__Counter_new(i32 %0, ptr %1) {
entry:
  %new.ptr = call ptr @__blang_rc_alloc_dtor(i64 16, ptr @__Counter_dtor)
  call void @Counter_init(ptr %new.ptr, i32 %0, ptr %1)
  ret ptr %new.ptr
}
```

(verbatim from the fixture's library IR). The consumer lowers
`Counter c = Counter(5, "hits");` to `call ptr @__Counter_new(i32 5, ptr %str)`
and never computes a size or a destructor.

### Why the symbol is `__Counter_new` and not `Counter_new`

`Counter_new` is exactly what `CodeGen.cpp:188-190` produces for a user method
named `new`, so the two would collide silently. The factory therefore lives in
the reserved `__` family alongside `__<Struct>_dtor` and
`__enum_<Name>_box_dtor`, and Sema now **rejects any source declaration whose
name begins with `__`** (`extern fn` exempt — it names a foreign C symbol, and
62 such declarations exist in `stdlib/`). The reservation is enforced, not
merely documented.

### Why the factory is not a `pub fn` in the `.bmod`

The cheapest implementation would have emitted `pub fn Counter_new(...) -> Counter;`
into the interface, because non-generic public functions are already marked
extern on import (`qcc.cpp:350-351`) and would need no new codegen. That was
**rejected**: it makes the factory callable from source, which gives external
construction a second spelling and breaks D9's "exactly one external form".

Instead the `.bmod` carries the struct's **interface record** — an `impl` block
of bodyless signatures:

```
pub struct Counter {
	int count;
	string label;
}
impl Counter {
	init(int start, string name);
	fn bump(self) -> int;
	fn value(self) -> int;
	fn name(self) -> string;
}
```

The `init` signature *is* the factory record: its presence tells the consumer to
construct through the factory, whose symbol both sides derive from the struct
name (`CodeGen::mangleStructFactoryName`). The factory name never crosses the
`.bmod` as a symbol and never enters any scope, so it cannot be named.

## The three fail-open paths this closed

The spike was as much about what happens when the ABI *doesn't* apply:

1. **`genConstructExpression` with no `init`** fell through to a heap allocation
   that no constructor ever wrote. For a struct with no resolvable layout
   `getTypeAllocSize` yields **1 byte**, so every later field access read out of
   bounds. Now a located error.
2. **`genMethodCall` returned `nullptr`** when no symbol was found
   (`CGStruct.cpp:2014-2015`), silently dropping the call. Now a located error.
3. **Bodyless members compiled to empty functions.** `FunctionDefinition::Parse`
   already accepted `fn bump(self) -> int;` in ordinary source
   (`QFunctionDefinition.cpp:220-226`), and the method loop unconditionally built
   an entry block and an implicit `ret 0` (`CodeGen.cpp:257-283`) — so the
   consumer would have *defined* an empty `Counter_bump` colliding at link with
   the library's real one. Fixed on both sides: codegen mirrors the extern-function
   seam (`CodeGen.cpp:546-547`) and emits a `declare`; Sema rejects the bodyless
   form outside a `.bmod`.

`ParseInit` was relaxed to accept `init(...);` (it unconditionally called
`Block::Parse`), which was a hard prerequisite — without it the `.bmod` cannot
carry an `init` signature at all.

## Evidence

Fixture: `test_build/counterlib` (lib) + `test_build/counterapp` (bin), asserted
in `test_build/run_build_tests.sh`.

Consumer IR invariants (all asserted by the suite, not just observed):

```
declare ptr @__Counter_new(i32, ptr)     ; declared, never defined
declare i32 @Counter_bump(ptr)           ; declared, never "define ... ret 0"
%ctor.ptr = call ptr @__Counter_new(i32 5, ptr %str)
```
plus: no `@__Counter_dtor` anywhere in the consumer's IR, and no
`__blang_rc_alloc_dtor(i64 16, ...)` — the consumer never allocates the imported
struct itself.

Program output is exact-matched. The fixture uses a `string` field specifically
so the destructor the **library** installed has real work to do on release.

**Sanitizers**: `test_build/run_build_tests.sh` carries a **committed ASan/LSan
leg** for this fixture — it relinks `counterapp` against the ASan/UBSan runtime
archives in `build-asan/` and runs it with `detect_leaks=1`, asserting both a
clean sanitizer report and unchanged output. It **skips loudly (yellow)** when
those archives or `llc` are absent rather than passing silently, and honours
`ASAN_BUILD_DIR` like `test_codegen.sh` does.

This leg matters more than the aggregate leak gate: `counterapp` is the first
program in the tree whose destructor is **installed by one module and invoked by
another**, and `Counter.label` is a refcounted string, so a broken hand-off shows
up as a leak or a double-free rather than as wrong output. Asserting it by hand
once would not have protected the property.

**Generic regression guard** (audit A6/W4): `test_build/mathlib` + `myapp` ship a
generic struct with `impl` bodies and generic functions instantiated on both
sides. The factory deliberately does not apply to them — `genStructFactory` and
the foreign-construction branch both return early for generics — and all their
existing assertions (including the `linkonce_odr` dedup checks) stay green. No
generic factory was attempted.

## Walls: none hit

- **W1 (structs by value)** — did not arise. Structs lower to `ptr`
  (`CGTypes.cpp:140-142`); the factory returns `ptr`; no sret, no ABI
  classification, no alignment negotiation.
- **W1′ (payload-carrying enums by value)** — did not arise **in this unit**. The
  fixture crosses only struct values. The audit's invariant stands and is
  untouched: an exported enum's variant/payload list keeps shipping in the
  `.bmod` because it is layout, not just API (`CGTypes.cpp:147-152` returns a
  real aggregate for a payload-carrying enum). U2/U5 must not remove it.
- **W2 (ARC)** — the factory returns +1, exactly what the inline path produced,
  so `trackTempStruct` and every downstream ARC decision are unchanged. No ARC
  semantics were modified.
- **W3 (null dtor)** — encapsulated: the factory calls `__blang_rc_alloc` when
  `getOrGenStructDestructor` returns nullptr for a field-free struct.
- **W4 (generics)** — not attempted, per A6.

## Known limitations, carried forward deliberately

1. **Field layout still ships in the `.bmod`.** U1 changed *how construction
   works*, not *what the interface carries*. Removing layout is U5's job and is
   blocked on more than the factory — see below.
2. **The factory symbol is prefix-aware when emitted and prefix-free when
   consumed** — a deliberate asymmetry, and a **deviation from the audit's
   `__<modulePrefix__>StructName_new` design that U2 must plan around** (filed as
   KI-5).

   The emitting side (`CodeGen::mangleStructFactoryName`) includes the defining
   module's codegen prefix, mirroring method mangling
   (`net__Socket_read`), so two namespaced modules that both define a `Socket`
   get distinct factories instead of silently sharing one symbol.

   The consuming side (`CodeGen::mangleImportedStructFactoryName`) cannot: a
   consumer knows the struct's name but not the defining module's prefix, which
   the `.bmod` does not carry. It therefore derives the prefix-free form.

   This is sound **only** because the sole producers of `.bmod` files today are
   `bcc build` library projects, which run with no module prefix; the namespaced
   stdlib modules that do get a prefix are combined into the consumer's own
   compilation, where construction takes the inline path and no factory is
   involved. **U2 introduces namespaced stdlib modules to this path and must
   resolve the asymmetry before any prefixed module ships a `.bmod`.** If it is
   not resolved, the failure is a link error against a symbol the library never
   emitted — loud, not a miscompile — but it is still a failure. Closing it
   properly means carrying the defining module's identity in the interface, which
   is Epic B's canonical module identity (D5/D10).
3. **`table`/`@json` and query codegen still need consumer-side layout.**
   Query-row materialisation (`CGRuntime.cpp:1134-1145`) independently calls
   `getOrCreateStructType`, `getTypeAllocSize` and `getOrGenStructDestructor`,
   and `@json` generation walks fields the same way. **The factory alone does not
   make an imported struct opaque** — recorded in the audit as a U1 → U5
   dependency so U5 plans for it rather than discovering it.
4. **Interim visibility**: every method ships in the `.bmod` until `pub` exists on
   impl members (U3), per design decision A3. This is deliberate and documented,
   not an oversight.

## Gate results

Run locally under the hand-assembled `~/toolchain` prefix; **CI is the
authority** (manager ruling Q6).

| Gate | Before | After |
|---|---|---|
| `./run_tests.sh` | 217 / 0 | **221 / 0** (+4 fixtures) |
| `BUILD_DIR=build-nollvm ./run_tests.sh` | 210 / 0 | **214 / 0** (+4) |
| `./test_codegen.sh` | 156 / 0 | **156 / 0** |
| `./test_codegen.sh --leak-check` | Leaks: 0 | **Leaks: 0** |
| `./test_lsp.sh` | 49 / 0 | **53 / 0** |
| `test_build/run_build_tests.sh` | all pass | **all pass** (+11 assertions) |
| `ctest --test-dir build` | 78/78 | **78/78** |
