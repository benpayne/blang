# Known issues — epic 001-toolchain-and-stdlib

Codegen limitations **surfaced** by the epic's new code (not introduced by it),
recorded honestly per the constitution's fix-or-file discipline. Each has a
minimal repro and a note on how the affected feature works around it. U5's new
stdlib exercises string/array/aggregate ARC paths that were previously untested;
these are the edges it revealed.

## Fixed in-unit (U4/U5)

- **Unary float negation** emitted an invalid integer `sub` constexpr → now
  `CreateFNeg` (U4, `CGExpressions.cpp`).
- **`math.abs_int` ABI mismatch** (`long` C param vs BLang `int`) → C aligned to
  `int` (U4).
- **`match` on an enum subject whose subject-call had string-literal args** leaked
  the arg temp on non-first arms → surgical pre-branch temp flush (U4,
  `CGEnum.cpp`).
- **String relational operators (`<`, `>`, `<=`, `>=`) compared POINTERS**, not
  content → now lexicographic via `__blang_string_compare` (U5,
  `CGExpressions.cpp`). Locked in by `test_files/codegen_string_compare.b`.

## Deferred (filed, worked around) — array/aggregate refcount ARC

These share a root theme: **refcount accounting for a heap value read out of an
array element or returned inside an aggregate is incomplete.** A general fix
belongs in a dedicated codegen/ARC unit (high blast radius across the string and
array runtime); U5 works around each and does not regress the green suite.

1. **Array-element string local binding double-frees — FIXED (kv example).**
   `string a = args[i];` and even a plain copy `string b = a;` created a second
   tracked owner without retaining; scope exit double-freed the shared string.
   Fixed by retaining at the BINDING site when the initializer is a borrowed
   source (VariableExpression / IndexExpression) — not at the IndexExpression
   node like the earlier reverted attempt, which double-counted flows that are
   already balanced (field access retains+tracks as a temp; call results and
   literals transfer via untrack; own-from-own moves transfer and are excluded).
   Locked in by `test_files/codegen_string_bind.b` (+ golden, leak-clean).
   REMAINING generic-context gap: a `T`-declared local inside a monomorphized
   generic (see #4) is still neither tracked nor retained — the raw-name keying
   across tracking/retain/release sites must move together in the dedicated ARC
   unit.

2. **Aggregate (multi-field struct) return with array fields double-frees.**
   Returning a struct whose `Array<...>` fields were populated inside the
   function corrupts at cleanup (locals alias the returned struct's fields).
   Repro: `struct T{Array<string> a; Array<string> b;} fn build()->T{ ...push...; return T{a:..,b:..}; }`.
   Single-field returns and value-type fields are safe. Workaround: `cli` returns
   scalars / a single `Array<string>` instead of a multi-field `Flags` struct.

3. **Struct-valued hashed Map under heavy churn intermittently crashes.**
   `Map<string, StructWithStringField>` with many interpolated-key inserts
   intermittently heap-corrupts (the value/key string-ARC through set/get under
   volume). `Map<string, int>` is fully stable (500-key demonstration test) and
   the light struct-value case (`codegen_bcc_collections_map.b`, `Map<string,
   Point>`, one set/get) is stable. Workaround: the hashed-Map demonstration uses
   `Map<string, int>`; struct values are documented as unstable under churn.

4. **Generic `sort<T>` of refcounted elements** (`sort<string>`) is unsafe — the
   monomorphized element swap `T tmp = items[j]` hits issue #1 for `T = string`.
   `sort<T>` is delivered for value types (int/double, tested asc+desc);
   refcounted-element sort awaits the array-element ARC fix.

5. **Namespaced-module internal string-returning calls double-free.**
   A stdlib module parsed into its own namespace (module-prefix codegen) whose
   functions call each other returning strings double-frees; the same code is
   clean inline or as a global-scope module. Workaround: `cli` (and `collections`)
   are global-scope stdlib modules (`isGlobalTypeLib`), called unqualified.

6. **Array-valued hashed Map crashes at scope exit** (same family as #1/#3).
   Nested generic types now parse (`>>` split), so `Map<string, Array<int>>` is
   expressible: set/get work and return correct values, but the function's
   scope-exit cleanup double-frees the Array values flowing through the Map
   (the monomorphized `get` returns a borrowed `self.values[i]` that the
   caller's local then owns-and-releases). Repro:
   `Map<string, Array<int>> m = Map<string, Array<int>> {keys:[],values:[],buckets:[]};
    m.set("k", [1,2]); Array<int> v = m.get("k");` → correct output, SEGV at exit.
   Direct `Array<Array<int>>` (push/index/iterate) is fully working and
   leak-clean (`codegen_nested_array.b`) — the gap is specifically refcounted
   values returned out of generic methods. Awaits the same dedicated
   array-element ARC unit as #1/#3/#4.
