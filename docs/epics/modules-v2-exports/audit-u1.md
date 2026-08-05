# Design audit — modules-v2-exports, pre-U1

**Epic**: [overview.md](overview.md) · **Unit**: U1 (construction ABI — the crux)
**Author**: `lead` (implementer/epic lead)
**Rev 1**: 2026-08-05 (`1d06cc1`) · **Rev 2**: 2026-08-05 — folds in the `critic`
APPROVE-WITH-CHANGES list (B1–B5, M1–M7, N4/N6/N7) and the manager's rulings on
Q1–Q8.
**Constitution**: `.specify/memory/constitution.md` v1.2.0 — Principle VI requires
this artifact *before* implementation.

Every "today" claim was verified first-hand against the working tree at `ce485b2`;
cites are `file:line` into that tree.

---

## 0. Baseline

### 0.1 Build environment

The machine had **no C++ toolchain**: no `cmake`, `gcc`/`g++`, `llc-18`, or LLVM
dev headers — only `/usr/lib/llvm-18/lib/libLLVM.so.18.1`. `sudo` needs an
unavailable password. Resolved without sudo by fetching the Ubuntu package closure
(`apt-get download --print-uris` works unprivileged) and extracting it with
`dpkg-deb -x` into `/home/ben/toolchain`, plus four fixups: a truncated `cmake`
`.deb` (re-fetched); `libLLVM.so.1` symlinked from the installed runtime package;
a dangling `libzstd.so` repointed at the system `libzstd.so.1`; and a `cc`
symlink (a dpkg alternative, present in no `.deb`). `~/toolchain/env.sh` exports
`PATH`/`CC`/`CXX`/`LLVM_CONFIG`/`LLVM_DIR` and the include/library paths.

> **Manager ruling (Q6), binding on this epic:** the prefix is acceptable as a
> local dev aid, but **CI is the authority — no claim in any PR may be gated on
> the local prefix alone.** Every unit's PR quotes CI results; local gate output
> is corroborating evidence only. Recorded in Known Issues.

### 0.2 Regression baseline — fully green

| Gate | Command | Result |
|---|---|---|
| parse/sema (LLVM) | `./run_tests.sh` | **217 passed, 0 failed** |
| parse/sema (parse-only) | `BUILD_DIR=build-nollvm ./run_tests.sh` | **210 passed, 0 failed** |
| codegen E2E + goldens | `./test_codegen.sh` | **156 passed, 0 failed** (149 golden-checked, 7 quarantined) |
| leak check | `./test_codegen.sh --leak-check` | **156 passed, Leaks: 0** |
| LSP goldens | `./test_lsp.sh` | **49 passed, 0 failed** |
| build system | `test_build/run_build_tests.sh` | **all checks passed** |
| runtime units | `ctest --test-dir build` | **78/78 passed** |

No pre-existing red. **Doc drift (Principle I, pre-existing):** `CLAUDE.md` says
134 codegen tests / 195 pass-fail tests; the suites report **156** and **217**. Not
caused by this epic; the first unit to touch `CLAUDE.md` corrects it.

### 0.3 Done-condition 8 baseline

`tools/check_no_field_reachins.sh examples/ test_build/` **exits 1 today**, as its
own header predicts. Reach-in sites:

| Site | Line | Reach-in |
|---|---|---|
| `examples/todo_app/main.b` | 146 | `string p = req.path;` |
| `examples/todo_app/main.b` | 147 | `string m = req.method;` |
| `examples/todo_app/main.b` | 160 | `Todo_from_json(req.body)` |
| `examples/todo_app/main.b` | 178 | `Todo_from_json(req.body)` |
| `examples/wordfreq/main.b` | 39 | `for k in counts.keys` |

`wordfreq:39` is a **false positive** (`counts` is a `Map`; matched by the
`HttpParsedHeaders:keys` entry). The `[^(]` trailer also produces false negatives
(`x.body(`). U4 owns tightening it — a green exit must mean something.
`Todo_from_json(req.body)` is simultaneously a reach-in and the
generated-function-spelling problem of open question #1.

### 0.4 Recon findings that change unit scoping

**F-A — CI does not run the build-system suite. [Manager: APPROVED as in-scope.]**
`.github/workflows/ci.yml` runs `run_tests.sh`, `test_codegen.sh`, `bcc test`,
sanitizers, `ctest`, fuzz, demos and LSP — but never
`test_build/run_build_tests.sh`. Done-conditions **1, 5 and 6** all rest on that
suite, and the epic requires each condition to be backed by a test **CI runs**.
**U2 adds a `build-system` CI job** invoking `test_build/run_build_tests.sh`, with
the prerequisites the reviewer enumerated: `llvm-18-dev`, `llvm-18`,
`libzstd-dev`, `libuv1-dev`, `pkg-config`, `libsqlite3-dev`, `git`, and `nm`
(binutils — the suite's `linkonce_odr` assertions at
`run_build_tests.sh:43-46` depend on it).

**F-B — the generic-slice `pub` hazard (affects U3 ordering; justifies A2).**
Generic structs ship methods as **verbatim source slices** inside an
`impl Name { … }` block (`BmodEmitter.cpp:238-265`). The impl parser rejects
anything but `fn`/`init`/`static fn` at **`QImplBlock.cpp:125`**. So the moment U3
marks a generic method `pub`, the emitted slice contains `pub fn` and any consumer
that has not learned to parse `pub` in impl blocks — including one reading a
warm-cached `.bmod` written by a newer compiler — fails to parse the interface.
Within U3 the parser change lands before or with the emission change. This is the
concrete failure the format-version salt exists to prevent.

**F-C / M1 — `table pub struct` is a real round-trip defect, owned by U2.**
`emitStruct` (`BmodEmitter.cpp:216-266`) writes the annotation *before* `pub`
(`table pub struct Name`), the inverse of source order. Reclassified from
"drive-by nit" to **an owned U2 defect with a committed regression fixture**: a
`table struct` in a library, its `.bmod` re-parsed by a consumer, asserted by a
golden `.bmod` plus a build fixture. It survives today only through parser
leniency, and U5's D15 metadata work makes `table` structs load-bearing across the
boundary — so it must be correct before then, not after.

**F-D / Q8 — `BuildCache::computeKey` weakness.** `BuildCache.cpp:121-144` hashes
**file contents only**: no filenames, no separators between files, no compiler
version, no format version. Renaming a source, or moving a line between two
sources in the same project, does not change the key. **[Manager ruling: file in
Known Issues; do not fix here.]** U2 adds only the format-version salt REQ-009
asks for, and **DC7's test must bump the real version constant** — not a
test-only stub — so the test proves the shipped mechanism.

**F-E — the `fail/xmodule/` harness is cheap.** `run_tests.sh` discovers `fail/`
recursively (`:215`) and `run_test` is single-file by construction (`:107`,
`:110`). So U3 needs: (i) `-not -path '*/fail/xmodule/*'` added to the `fail/`
find — the precedent already set for `fail/warn/`; (ii) its own inline leg
modelled on the `fail/warn/` loop (`:224-247`), reusing `resolve_expected_pattern`
(`:79-93`) and the ERE matcher (`:166`) verbatim; (iii) **an arm added to the
canonical `file:line:col` gate at `:146-153`**, which is keyed on a `fail/sema/`
path glob — without it the located-error requirement of DC2/DC3 is not actually
enforced.

**F-F / N6 — golden `.bmod` files are a U2 requirement.** The only `.bmod`
*content* coverage in the repo is three greps at
`test_build/run_build_tests.sh:24-26`; there are no golden `.bmod` files. Since
this epic's thesis is "the `.bmod` becomes a true interface", **U2 must introduce
committed golden `.bmod` files** with an update flag, rather than accreting greps.
U3 and U5 update those goldens as the format changes — which is also how each
format bump becomes visible in review.

**M4 — every format change bumps the version.** U2, U3 and U5 each change the
`.bmod` shape (factory attribute; `pub` members; dropped layout + D15 metadata).
**Each of the three bumps the format-version constant in its own PR**, and each
updates the golden `.bmod`s. One bump for the whole epic is not acceptable: a
warm cache written between two units would otherwise be silently misread.

**N7 — the promoted-module exemption, restated where implementers will read it.**
`buffer`, `collections` and `cli` are parsed into the **user's own scope**
(`qcc.cpp:308-313`), so no module boundary exists to enforce against until Epic B.
They are **exempt from module-private enforcement for this entire epic** (design
A7). Enforcement applies to the *namespaced* stdlib modules (`net`, `fs`, `timer`,
…), which do get their own `Scope` (`qcc.cpp:315-322`). This exemption is recorded
in Known Issues, not silently applied.

**M6 — tracked, not fixed here.** Recorded on the epic's issue list and revisited
at U5; no U1 action.

---

## (a) The export-model inversion, and how U1–U5 discharge the 9 done conditions

### Restatement

BLang's module boundary exports exactly the wrong half of a type. A `pub struct`
ships its **full field layout** into the `.bmod` and **none of its methods**, so a
consumer can read and write an imported type's internals but can neither construct
it nor call anything on it — the interface publishes the implementation and hides
the API. Worse, an exported declaration may reference a non-`pub` type; the
emitter writes the reference without the declaration, the library build exits 0,
and the failure surfaces as a *syntax error inside a generated file* at the
*consumer's* build (P9). This epic inverts the model: methods and `init` become
the API behind an explicit `pub` (private by default, D9); member variables become
always-private, which makes struct literals module-private automatically and
leaves exactly one external construction form, `Counter(5)` (D9, D7); and the
`.bmod` becomes a true interface — signatures, protocol-conformance records
(D16), enum variants and payloads (D17), and a compiler-facing metadata section
that only `table`/`@json` shapes need and that user source can never name (D15).
The keystone constraint is that construction is caller-allocating today, so
removing layout from the interface is impossible without first moving allocation
into the library: hence U1, the factory ABI, leads.

### Unit → done-condition mapping

| DC | Done condition (abbrev.) | Owning unit | Supporting |
|----|--------------------------|-------------|------------|
| 1 | `test_build/` lib+bin: `Counter c = Counter(5);` + `c.bump()` builds and runs | **U2** | U1 (the ABI it rides on) |
| 2 | Struct literal + field access on an imported type are located errors (`fail/xmodule/`) | **U5** | U3 (harness + origin marker) |
| 3 | `pub` parses on methods/`init`; unmarked method unreachable cross-module, callable in-module | **U3** | U1 (bodyless-member rule) |
| 4 | Non-`pub` type in an exported signature = located error at the **library** build (incl. enum payloads) | **U3** | — |
| 5 | `print("{}", x)` on an imported `impl Printable` type works E2E | **U2** | U3 (`pub` filtering) |
| 6 | Imported `@json table struct`: `query`/`to_json` work; naming the field is an error | **U5** | U2 (metadata groundwork, F-C fix) |
| 7 | `BuildCache` key incorporates a `.bmod` format version; bump invalidates warm cache | **U2** | U3, U5 (each bumps it — M4) |
| 8 | `check_no_field_reachins.sh examples/ test_build/` exits 0; examples pass | **U5** | U4 (accessor surface + `examples/`) |
| 9 | All gates green in both build modes; `--leak-check`; LSP; build tests; docs updated | **all units** | verified at U5 |

**U1 owns no done condition on its own.** It is a de-risking spike producing an
ABI and a fixture; DC1 is credited to U2, where the emitted `.bmod` first makes
`Counter(5)` parse in a consumer. Flagged because a manager scoring U1 against the
DC list would otherwise conclude it produced nothing.

---

## (b) U1 — the factory ABI, concretely

### b.0 What the code does today (verified first-hand)

- **Construction is caller-allocating.** `genConstructExpression`
  (`CGStruct.cpp:603-717`) computes `dl.getTypeAllocSize(structType)`
  (`:641-642`), generates the destructor from the field list via
  `getOrGenStructDestructor` (`:660`), calls `__blang_rc_alloc_dtor(size, dtor)`
  (`:663`) — or `__blang_rc_alloc(size)` when the struct has no refcounted field
  and the dtor is `nullptr` (`:249-253`, `:664-665`) — then
  `StructName_init(heapPtr, args...)` (`:667-711`).
- **The destructor is `InternalLinkage`**, named `__StructName_dtor`
  (`:194-204`, `:264-265`), walking `sd->getFields()` (`:230-247`, `:283-306`).
- **Release is already layout-free.** `__blang_rc_release`
  (`runtime/blang_runtime.c:115-131`) reads `hdr->destructor`, stored at
  allocation by `__blang_rc_alloc_dtor` (`:77-84`). **No runtime change needed.**
- **Method ABI**: `StructName_method(ptr self, args...)`, `self` the raw heap
  pointer (`CGStruct.cpp:2017-2031`); declared `ExternalLinkage` with mangled name
  `[modulePrefix__]StructName_methodName` (`CodeGen.cpp:184-219`).
- **`Counter(5)` doesn't parse for an imported type.** `QExpression.cpp:418-421`
  builds a `ConstructExpression` only when `sd->hasInit()`; a `.bmod` struct
  carries no methods, so it is false and the parse falls through. This is P8's
  exact mechanism — U2's `init` signature emission is what makes the parse
  succeed. **No parser change is needed for construction itself.**

### b.1 Consequences that shape the design

**1. By-value applies to *structs only* — payload-carrying enums are by value.
[B3, corrected.]** Rev 1 claimed "no by-value struct ABI anywhere". That is right
for **structs**: `getLLVMType` lowers a struct-typed value to `ptr`
(`CGTypes.cpp:140-142`), so a struct is never passed or returned by value and the
factory returns a plain `ptr` needing no sret attribute or ABI classification.
It is **wrong as a general statement**: `getLLVMType` returns
`getOrCreateEnumType(...)` — a real aggregate — for an enum **with** a payload,
and `i32` for one without (`CGTypes.cpp:147-152`). Payload-carrying enums
therefore cross function boundaries **by value**, and their layout
(`{i32 tag, [N x i8] payload}`) is computed from the variant payload list.

> **ABI invariant this forces (U2/U5 must not break it):** an exported enum's
> **variant and payload-type list must keep shipping in the `.bmod`** — it is not
> merely API surface (D17), it is **layout information a consumer needs to pass
> the value at all**, and to generate the recursive-enum helpers
> (`__enum_<Name>_box_dtor`, `__enum_<Name>_payload_retain`) at use sites. D17
> already requires this; b.1 records *why* it is non-negotiable, so a later unit
> optimising "the `.bmod` should carry less" cannot remove it by accident.

**2. `.bmod` structs are injected into `gScope` but NOT into the consumer's
`mod->mStructList`** (`qcc.cpp:355-363`). The method-declaration loop at
`CodeGen.cpp:171-221` iterates `mod->mStructList`, so it never sees imported
structs; a cross-module method call finds no LLVM function and `genMethodCall`
**returns `nullptr` silently** (`CGStruct.cpp:2014-2015`) — a Principle III
violation to fix, not inherit.

**3. The declaration seam already exists, one line away.** For free functions,
`CodeGen.cpp:546-547` is exactly the right shape:

```cpp
// Extern functions are declarations only — no body
if ( func->isExtern() )
    return llvmFunc;
```

The method loop has **no equivalent**: `CodeGen.cpp:257-283` unconditionally
creates an entry block and an implicit return, so a bodyless method routed through
it becomes a **defined empty function** (`define ... ret 0`) that collides at link
with the library's real one. **U1 mirrors `:546-547` in the method loop**, keyed
on "bodyless", so imported signatures become `declare`.

**4. Non-generic pub functions from a `.bmod` are already marked extern**
(`qcc.cpp:350-351`) so codegen declares without defining. The factory reuses this
*lowering* behaviour — but **not** by being a source-nameable free function
(see b.3).

### b.2 The ABI and the factory symbol

**[Manager ruling Q1: do NOT use `StructName_new`.]** `StructName_new` collides
with the mangling of a user method named `new` (`CodeGen.cpp:188-190` produces the
identical symbol). The factory therefore uses the **reserved, unspellable**
`__`-prefixed family that already houses `__StructName_dtor` and
`__enum_<Name>_box_dtor`:

```
symbol:  __<modulePrefix__>StructName_new
type:    ptr (*)(<init parameter types...>)
linkage: ExternalLinkage
body:    %p = call ptr @__blang_rc_alloc_dtor(i64 <size>, ptr @__StructName_dtor)
         ; or @__blang_rc_alloc(i64 <size>) when the dtor is null
         call void @StructName_init(ptr %p, <args...>)
         ret ptr %p
```

**Plus a Sema reserved-name check [B2]:** a user-declared identifier (function,
method, or struct) whose mangled symbol would begin with `__` is a located
`file:line:col: error:`, in all build modes. Without it the reservation is a
convention, not a guarantee, and the collision returns the first time someone
writes an unusual name. This ships in U1 with a `test_files/fail/sema/` fixture
and an `.expected` pattern.

**Ownership contract:** the factory returns a **+1 reference** — exactly what
`genConstructExpression` produces today — so every downstream ARC decision
(`trackTempStruct` at `CGStruct.cpp:715`, store-untracks-temp, scope-exit release)
is unchanged. Destruction stays `__blang_rc_release`, reading the dtor pointer the
*library* installed. **The consumer never computes a size and never generates a
dtor for an imported type.**

### b.3 How the `.bmod` declares it — a struct attribute, not a free function

**[Manager ruling Q2: settle in U1, not U2. The factory is NOT a source-nameable
free `pub fn` injected into `gScope`.]** Rev 1 proposed emitting
`pub fn Counter_new(int start) -> Counter;` because it rides `qcc.cpp:350-351`
for free. That is rejected, and correctly: injecting the factory as an ordinary
public symbol makes it **callable from source**, which hands every consumer a
second construction form and contradicts D9's "exactly one external form". It
would also collide in the flat namespace and appear in completion.

The factory is instead recorded as an **attribute of the struct** in the `.bmod` —
part of the struct's interface record, not a free symbol — carrying the `init`
parameter signature and the factory's reserved symbol name. Consumers read it to
emit the call; **no name enters `gScope`**.

**[Manager ruling Q3.]** The record also states **whether the `init` is private**,
so a consumer's `Counter(5)` against a private `init` produces
"constructor of 'Counter' is private" rather than "type 'Counter' has no
constructor". Distinguishing the two is worth the byte: an LLM consumer
self-corrects from the first and cannot from the second (Principle VI).

U1 hand-writes the `.bmod` record to prove the ABI; U2 makes `BmodEmitter` emit
it. The exact surface syntax is settled in U1's speckit spec, with a golden
`.bmod` (F-F) pinning it from the first commit.

### b.4 How the consumer lowers the two statements

**`Counter c = Counter(5);`**

1. **Parse** — unchanged, once the `.bmod` supplies an `init` signature.
2. **Sema** — resolves against the injected `StructDefinition`; arity/arg-type
   checks already run over the `init` signature. New: if the struct is
   foreign-marked and its `init` is recorded private → located error (b.3).
3. **CodeGen** — `genConstructExpression` branches: if the struct is
   foreign-marked **and non-generic**, emit `call ptr @__Counter_new(i32 5)` and
   skip `CGStruct.cpp:640-712` entirely. Then `trackTempStruct` as today.
4. **Scope exit** — unchanged `__blang_rc_release`.

**Fail-open paths must become loud [B4].** Two exist today and both silently
produce wrong code:

- `genConstructExpression` continues past a missing `init` (`:688-712` simply
  skips the call), and `getTypeAllocSize` of an **opaque/empty** struct type
  yields **1 byte** — so a foreign construction that slips through the inline path
  today allocates a 1-byte block, never initialises it, and hands back a pointer
  every subsequent field access reads out of bounds. `genConstructExpression`
  must **report a located error** instead of returning that allocation.
- **The inline construction path must explicitly reject a foreign-marked struct**
  rather than fall through to layout computation. Belt and braces with the branch
  in step 3: if the factory branch is ever not taken for a foreign struct, that is
  a compiler bug and must surface as a diagnostic, not as a 1-byte allocation.

The same applies to `genMethodCall`'s silent `nullptr` (`CGStruct.cpp:2014-2015`).

**`c.bump()`**

1. Parse/Sema — resolves against the method list the `.bmod` supplies.
2. CodeGen — `genMethodCall` looks up `Counter_bump` by mangled name
   (`:1985-1986`). For an imported struct, U1 adds a declaration step so the
   symbol exists as a `declare`, mirroring `CodeGen.cpp:546-547` (b.1 item 3).
   The spike asserts on the emitted IR that `Counter_bump` is a `declare`, not a
   `define`.
3. Call — `self` = the loaded heap pointer, unchanged.

### b.4a Bodyless members: the parser prerequisite and the hole it opens [B1]

**Prerequisite.** `FunctionDefinition::Parse` **already** accepts a bodyless
member: if the next token is `;` it sets `mFuncBody = nullptr`
(`QFunctionDefinition.cpp:220-226`, the path protocol methods use). So
`fn bump(self) -> int;` inside an `impl` block parses today. **`init` does not** —
`ParseInit` unconditionally calls `Block::Parse` (`QFunctionDefinition.cpp:332`).
U1 must relax `ParseInit` to accept `;` → `mFuncBody = nullptr`, or the `.bmod`
cannot carry an `init` signature at all and DC1 is unreachable.

**The hole this exposes (pre-existing, widened by the above).** Because bodyless
`fn` already parses in ordinary `.b` source, a user can write `fn bump(self);`
today and get a silently-defined empty function returning 0 — Principle III
exactly backwards. Relaxing `init` would extend that to constructors.

**Rule U1 ships:** a bodyless method or `init` is legal **only** in an interface
module (`Module::isExtern()`, i.e. a parsed `.bmod`) and in `protocol`
declarations. In ordinary `.b` source it is a located
`file:line:col: error:` in all build modes, with a `test_files/fail/sema/`
fixture and `.expected` pattern for both the `fn` and the `init` form. This closes
a pre-existing hole rather than opening a new one, and it is the check that makes
the "declare, not define" seam safe.

### b.5 Provenance: two predicates, not one [B5, M5]

**These are different questions and must not share a flag:**

| Predicate | Question | Type | Set where | Consumed by |
|---|---|---|---|---|
| `mFromInterface` | "did this definition arrive through a `.bmod`?" — an **ABI** question | `bool` on `StructDefinition` | `qcc.cpp:398-400` | U1: factory vs inline construction; declare-not-define |
| module origin | "which module defines this?" — a **visibility** question | origin string on the definition | U3 | U3/U5: is this member reachable from here? |

Conflating them breaks `--combine`: namespaced stdlib modules (`net`, `fs`, …)
have a real module boundary that U3 must enforce, but they arrive as **parsed
`.b` source**, not `.bmod`s, so `mFromInterface` is false for them. A single flag
would silently exempt the entire stdlib from visibility enforcement — which is
also why the `buffer`/`collections`/`cli` exemption (N7) must be an explicit,
recorded decision rather than an emergent property of the flag.

**[B5: the `registerExternalTypes`-derived option is struck.]** Rev 1 offered
deriving provenance from arrival through `registerExternalTypes`
(`CodeGen.cpp:380-401`). That is rejected: it is LLVM-side only, so it cannot
inform Sema in parse-only builds (Principle III), and it infers a fact that should
be stated. **`mFromInterface` is stamped at `qcc.cpp:398-400`**, inside the
existing `if (isBmod)` block where `mod->setExtern(true)` already lives — the
natural home, and **outside** the flat-merge injection block.

What exists today and is **not** sufficient, so the reviewer can confirm I am not
adding redundant state: `Module::isExtern()` (`Type.h:282-283`, `:299`) is
per-module and never reaches individual definitions;
`FunctionDefinition::mIsExtern` (`Type.h:341-342`) is the `extern fn` **FFI**
flag that `qcc.cpp:350-351` already overloads to mean "declared-only", so a source
`extern fn` and an imported function are indistinguishable — I will not overload
it further; `Scope::mImportedModules` (`Type.h:208-221`) tracks import
*statements*. `StructDefinition`/`EnumDefinition`/`ProtocolDefinition` have **no**
provenance field of any kind.

### b.5a Visibility follows the defining module of the enclosing declaration [M3]

Stated now because U1's provenance work is where it would first be got wrong, and
because getting it wrong breaks cross-module generics **silently**:

> A member reference is checked against the module that defines the **enclosing
> declaration**, not the module that is currently compiling.

A generic struct's method body ships verbatim into the `.bmod`
(`BmodEmitter.cpp:238-265`) and is compiled **inside the consumer**, but it is
still `Pair`'s own code: `self.first` and `Pair<T> { first: …, second: … }`
(both present in `test_build/mathlib/mathlib.bmod` today) are private-field and
struct-literal uses that **must keep working** once U5 makes fields private and
literals module-private. Under the rule above they do, because the enclosing
declaration is defined by `mathlib`. Under a naive "is the current module the
defining module?" check they would all become errors and every cross-module
generic would break. U3/U5 own the enforcement; U1 owns not designing a predicate
that makes the correct rule unexpressible.

### b.6 Walls, and the fallback

The spike stops and raises a question rather than widening scope if:

- **W1 (structs).** A struct is ever passed or returned by value.
- **W1′ (enums) [B3].** A payload-carrying enum crosses a module boundary in a
  way that needs layout the `.bmod` does not carry. Mitigation is stated in b.1:
  variant/payload lists keep shipping (D17). If some path needs *more* than that,
  it is a wall — payload-carrying enums are genuinely by value
  (`CGTypes.cpp:147-152`) and the factory does not help them.
- **W2.** The `+1` cannot be reconciled with an existing ARC site without changing
  ARC semantics (an explicit non-goal).
- **W3.** The `nullptr`-dtor case cannot be encapsulated (it can — the factory
  calls `__blang_rc_alloc`).
- **W4.** Generic construction cannot be left alone. The generic path
  (`CGStruct.cpp:625-634`, `:647-658`) is consumer-side by design; the spike's
  generic case is a **regression guard only** (A6). A generic factory is a wall.

**Fallback, in preference order:** (1) layout as compiler-facing ABI metadata in
the D15 section — costs the rebuild-avoidance win, preserves every visibility
done condition, and is the design record's own named alternative (F1 / design A1);
(2) a library-emitted `size`+`dtor` accessor pair — strictly worse, only if the
wall is specifically in the *call* form; (3) stop and escalate.

### b.7 The layout residue U1 does not remove [M7]

`genConstructExpression` is **not** the only consumer-side site that needs a
foreign struct's layout. Query-row materialisation
(`CGRuntime.cpp:1134-1145`) independently calls `getOrCreateStructType`,
`dl.getTypeAllocSize(structType)` and `getOrGenStructDestructor(structDef, …)` to
build one heap struct per result row. `@json` generation
(`CGRuntime.cpp:1519-1651`) walks fields the same way.

So the factory alone does **not** make an imported struct fully opaque:
**U5 cannot drop field layout from the `.bmod` until these paths are handled**
(via the D15 metadata section, or by routing them through the same
library-emitted-factory idea). Recorded here as an explicit **U1 → U5
dependency** so U5 discovers it in its plan rather than in its implementation.

### b.8 Blast-radius sweep owed before U4/U5 planning [M2]

Before U4/U5 are planned, I will run and commit a **struct-literal-aware** sweep —
not just field reach-ins — across `examples/`, `test_build/`, `test_files/` and
`stdlib/`, counting: struct literals for types that become foreign; field
accesses on imported types; and generated-function calls by name
(`Todo_from_json`). `tools/check_no_field_reachins.sh` finds none of the literal
forms today (it greps `.field` only), so the current exit-1 output understates the
migration. The sweep's output sizes U5 and feeds U4's accessor design.

### b.9 U1 exit criteria

- A `test_build/` lib+bin pair constructs, calls and releases an imported
  non-generic struct across a `.bmod` with zero consumer layout knowledge,
  asserted on the consumer's `.ll`: no `__blang_rc_alloc_dtor` for the foreign
  type, no `__Counter_dtor` definition, `Counter_bump` present as `declare`.
- Bodyless-member rule enforced with `fail/sema` fixtures (both `fn` and `init`).
- Reserved-`__`-name Sema check with a `fail/sema` fixture.
- Fail-open paths produce located errors, each with a fixture.
- ASan/`--leak-check` clean; `ctest` green; a generic cross-module case unchanged.
- Full gate list green in **both** build modes, **quoted from CI** (Q6).
- Spike write-up committed under `specs/029-construction-abi-factory/`.

---

## (c) Files and subsystems expected to change

**U1:**

| File | Change |
|---|---|
| `QFunctionDefinition.cpp` | `ParseInit` accepts a bodyless `;` form (b.4a) |
| `Sema.cpp` | bodyless-member-outside-interface error; reserved `__` name check; private-`init` construction error |
| `CGStruct.cpp` | `genConstructExpression`: foreign branch → factory; reject foreign struct on the inline path; loud error instead of the 1-byte allocation; loud error for the `genMethodCall` nullptr |
| `CodeGen.cpp` | factory emission for non-generic structs with `init`; bodyless-method declaration seam mirroring `:546-547`; declaration step for imported structs' methods |
| `CodeGen.h` | declarations for the above |
| `Type.h` | `mFromInterface` bool + accessors on `StructDefinition` |
| `qcc.cpp` | **stamp `mFromInterface` at `:398-400` only** — see the confirmation below |
| `BmodEmitter.cpp` | hand-written record in the spike; emitter change is U2's |
| `test_build/`, `test_files/fail/sema/` | fixtures |
| `specs/029-construction-abi-factory/` | spec/plan + spike write-up |
| `manifest.yaml` | speckit renumbering (below) |

**Later units** (declared for scope checking, untouched in U1):
`BmodEmitter.{h,cpp}` (U2, U5), `BuildCache.cpp` + `.github/workflows/ci.yml`
(U2), `QImplBlock.cpp` (U3), `Sema.cpp` visibility (U3/U5), `SQLGen.cpp` +
`CGRuntime.cpp` (U5, per b.7), `stdlib/*.b` (U4), `examples/` (U4/U5),
`test_files/fail/xmodule/` + `run_tests.sh` (U3),
`tools/check_no_field_reachins.sh` (U4/U5), `docs/language_design.md`
§"Modules and Imports" (581-707) and `CLAUDE.md` (Principle I, same PR as each
behavior change).

**Speckit renumbering [Q5, manager APPROVED]:** `specs/` already runs to
`028-integration`, so the workplan's `002`–`006` names collide. U1–U5 use
**`029-construction-abi-factory`, `030-bmod-true-interface`,
`031-pub-members-and-export-enforcement`, `032-stdlib-opaque-api`,
`033-private-fields-opaque-bmod`**, and `manifest.yaml` is updated in the same
commit as this revision.

### Explicit confirmation — the flat-merge resolution path

**I will not modify the flat-merge resolution path in `qcc.cpp`.** The injection
block at **`qcc.cpp:330-381`** — the loop walking `bmodMap` and calling
`gScope->addSymbol()` / `gScope->addType()`, with the
`// This implements the flat merge.` comment at `:333` and `bmodMap.clear()` at
`:380` — is **Epic B's seam and stays exactly as it is**. U1's only `qcc.cpp`
change is stamping `mFromInterface` inside the pre-existing `if (isBmod)` block at
**`:398-400`**, alongside `mod->setExtern(true)` — a different block, after the
merge, per B5.

---

## (d) Open questions

**Resolved by manager ruling — recorded, not re-escalated:** Q1 (reserved `__`
symbol + Sema check), Q2 (struct attribute, settled in U1), Q3 (record private
`init`), Q5 (029–033 + manifest), Q6 (CI is the authority), Q7 (`build-system` CI
job in U2, with the listed prerequisites), Q8 (Known Issues; DC7 bumps the real
constant).

**Q4 — open question #1 in `overview.md`** (cross-module spelling of generated
data-contract functions, e.g. `Todo_from_json`): no action, already owned by U5.
Noted only that `examples/todo_app/main.b:160` couples it to a field reach-in, so
U4's migration and U5's answer touch the same line.

**Still open, U1 will answer with evidence rather than argument:** whether any
path needs a payload-carrying enum's layout beyond the variant list the `.bmod`
already ships (W1′). If one does, that is a wall and I stop.

---

## Constitution self-check

| Requirement | Status |
|---|---|
| VI — design artifact before implementation | this document, rev 2; no code written before it |
| VI — security dimension mandatory for U1 | allocation ABI and dtor function pointers analysed (b.2, b.4); the factory installs the dtor in the *library*, narrowing consumer trust in foreign layout; reserved-name check prevents symbol capture of the `__` family |
| III — reject, don't coerce | three fail-open paths converted to located errors (b.4, b.4a); bodyless-member hole closed |
| II — tests | exit criteria name every fixture (b.9) |
| IV — ARC/runtime verified | no runtime change; `--leak-check` in exit criteria |
| I — docs | `docs/language_design.md:581-707` + `CLAUDE.md` listed; pre-existing count drift flagged (§0.2) |
| Audit pattern | rev 2 addresses all `critic` B/M/N findings; PR carries per-dimension verdicts |
