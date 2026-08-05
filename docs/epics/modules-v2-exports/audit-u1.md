# Design audit — modules-v2-exports, pre-U1

**Epic**: [overview.md](overview.md) · **Unit**: U1 (construction ABI — the crux)
**Author**: `lead` (implementer/epic lead) · **Date**: 2026-08-05
**Constitution**: `.specify/memory/constitution.md` v1.2.0 — Principle VI requires
this artifact *before* implementation. **Status: awaiting `critic` review. No
implementation has begun.**

Every "today" claim below was re-verified first-hand against the working tree at
`ce485b2` (cites are `file:line` into that tree), not inherited from the planning
documents.

---

## 0. Baseline (established before any change)

### 0.1 Build environment — resolved, with a caveat to record

The machine had **no C++ toolchain at all**: no `cmake`, no `gcc`/`g++`, no
`llc-18`, no LLVM dev headers — only `/usr/lib/llvm-18/lib/libLLVM.so.18.1` (the
runtime shared object). `sudo` requires a password that is not available.

Resolved without sudo by fetching the Ubuntu package closure and extracting it
into a private prefix:

```
apt-get download --print-uris <pkgs>      # works without sudo
curl -O <uris> ; dpkg-deb -x <deb> /home/ben/toolchain
```

Packages: `build-essential` closure (gcc-13/g++-13/cpp-13/binutils/libc6-dev/
linux-libc-dev/libstdc++-13-dev/libgcc-13-dev), `cmake`, `make`, `pkg-config`,
`llvm-18-dev`, `llvm-18-tools`, `clang-18`, `libclang-rt-18-dev` (ASan runtime),
`libsqlite3-dev`, `valgrind`, `libz3-dev`, `libedit-dev`, `libxml2-dev`,
`zlib1g-dev`, `libzstd-dev`.

Three fixups were required and are recorded so the environment is reproducible:

1. `cmake_3.28.3-1build7_amd64.deb` downloaded truncated (16 KB); re-fetched.
2. `LLVMExports.cmake` references `libLLVM.so.1`, which ships in the *runtime*
   package already installed system-wide → symlinked
   `~/toolchain/usr/lib/llvm-18/lib/libLLVM.so.1` → `/usr/lib/llvm-18/lib/libLLVM.so.1`.
3. `zstd::libzstd_shared` is required by `LLVMSupport`'s link interface; the
   extracted `libzstd.so` symlink was dangling → repointed at the system
   `/usr/lib/x86_64-linux-gnu/libzstd.so.1`.
4. `bcc`/`test_codegen.sh` invoke `cc`; gcc-defaults' `/usr/bin/cc` symlink is a
   dpkg alternative, not a file in the .deb → added `~/toolchain/usr/bin/cc`.

`~/toolchain/env.sh` exports `PATH`, `LD_LIBRARY_PATH`, `C_INCLUDE_PATH`,
`CPLUS_INCLUDE_PATH`, `LIBRARY_PATH`, `CC`, `CXX`, `LLVM_CONFIG`, `LLVM_DIR`,
`PKG_CONFIG_PATH`, `CMAKE_PREFIX_PATH`. Every gate below was run under it.

> **Known Issue to record (Principle VI):** the toolchain is a hand-assembled,
> non-standard prefix living outside the repo and outside version control. It is
> *not* what CI uses. Gate results obtained locally are therefore indicative, and
> **CI remains the authority** for this epic's merge decisions. If the devbot host
> is supposed to have a provisioned toolchain, this is a host-configuration bug
> worth fixing at the source rather than per-run.

### 0.2 Regression baseline — fully green (evaluation.md requires this before U1)

| Gate | Command | Result |
|---|---|---|
| parse/sema (LLVM) | `./run_tests.sh` | **217 passed, 0 failed** |
| parse/sema (parse-only) | `BUILD_DIR=build-nollvm ./run_tests.sh` | **210 passed, 0 failed** |
| codegen E2E + goldens | `./test_codegen.sh` | **156 passed, 0 failed** (149 golden-checked, 7 quarantined) |
| leak check | `./test_codegen.sh --leak-check` | **156 passed, Leaks: 0** |
| LSP goldens | `./test_lsp.sh` | **49 passed, 0 failed** |
| build system | `test_build/run_build_tests.sh` | **all checks passed** |
| runtime units | `ctest --test-dir build` | **78/78 passed** |

No pre-existing red to absorb. Both configurations build clean.

**Doc drift noted (Principle I, pre-existing):** `CLAUDE.md` states 134
`codegen_*.b` tests and "195 pass/fail tests"; the suites actually report **156**
codegen tests and **217** parse/sema tests. Not caused by this epic; flagged so a
unit that touches `CLAUDE.md` corrects it rather than propagating it.

### 0.3 Done-condition 8 baseline

`tools/check_no_field_reachins.sh examples/ test_build/` **exits 1 today**, as the
script's own header predicts (it is the U4/U5 target gate, not a current CI gate).
Reach-in sites reported:

| Site | Line | Reach-in |
|---|---|---|
| `examples/todo_app/main.b` | 146 | `string p = req.path;` |
| `examples/todo_app/main.b` | 147 | `string m = req.method;` |
| `examples/todo_app/main.b` | 160 | `Todo_from_json(req.body)` |
| `examples/todo_app/main.b` | 178 | `Todo_from_json(req.body)` |
| `examples/wordfreq/main.b` | 39 | `for k in counts.keys` |

Two observations for U4/U5, not for U1:

- The `wordfreq:39` hit is a **false positive**: `counts` is a `Map`, matched by
  the `HttpParsedHeaders:keys` entry. The script keys on the bare field token with
  no type awareness. It will also miss reach-ins written as `x.body(` (the
  `[^(]` trailer) — i.e. it can produce both false positives and false negatives.
  U4 owns tightening it; a green exit must mean something.
- `Todo_from_json(req.body)` is simultaneously a field reach-in *and* the
  generated-function-spelling problem behind **open question #1**. The two are
  entangled at the same call site.

### 0.4 Recon findings that change unit scoping (raise now, not at review time)

**F-A (BLOCKER for the epic done condition as worded).**
`test_build/run_build_tests.sh` is **not invoked by CI**. `.github/workflows/ci.yml`
runs `run_tests.sh`, `test_codegen.sh`, `bcc test`, sanitizers, `ctest`, fuzz, demos
and the LSP legs — there is no leg that runs the build-system suite. But
done-conditions **1, 5 and 6** are *all* backed by `test_build/run_build_tests.sh`
fixtures, and the epic's preamble requires each condition to be "backed by a
committed test **that CI runs**". As things stand, three of the nine conditions
would be satisfied by a suite nothing runs automatically. **Proposal: U2 adds a
`build-system` job to `ci.yml` invoking `test_build/run_build_tests.sh`**, since
U2 is the first unit to add fixtures there. Flagging rather than silently
absorbing it, because it is a scope addition to a unit.

**F-B (affects U3 ordering, and justifies the U2 cache salt).**
Generic structs ship their methods as **verbatim source slices** wrapped in an
`impl Name { … }` block (`BmodEmitter.cpp:238-265`). The impl-block parser rejects
anything that is not `fn` / `init` / `static fn` with a single diagnostic at
**`QImplBlock.cpp:125`** ("Expected 'fn', 'init', or 'static fn' in impl block").
Therefore, the moment U3 marks any generic struct's method `pub` in source, the
emitted slice contains `pub fn` and **every `.bmod` consumer that has not yet
learned to parse `pub` in impl blocks fails to parse the interface** — including a
warm-cached `.bmod` produced by a newer compiler and read by an older one. Two
consequences: (i) within U3 the parser change must land before or with the
emission change, never after; (ii) this is a concrete instance of the failure mode
the U2 format-version salt exists to prevent, which strengthens A2's placement.

**F-C (U2 emission detail).** `emitStruct` (`BmodEmitter.cpp:216-266`) writes
`table pub struct Name` — annotation *before* `pub`, the opposite of source order
(`pub` then `table`). It round-trips today only because the `.bmod` parser is
lenient. U2/U5 touch this function; the ordering should be normalised as a
drive-by, with a `.bmod` content assertion pinning it.

**F-D (U2 cache-salt detail).** `BuildCache::computeKey`
(`BuildCache.cpp:121-144`) hashes **file contents only** — no filenames, no
separators between files, no compiler version, no format version. Two
consequences beyond the known missing format version: renaming a source file does
not change the key, and moving a line between two sources in the same project does
not either. The format-version salt is U2's REQ-009 job; the
filename/separator weakness is adjacent and I will **raise it rather than
opportunistically fix it**, since it is not in any REQ.

**F-E (U3 harness — cheaper than feared).** `run_tests.sh` discovers `fail/`
recursively (`:215`), and `run_test` is single-file by construction (`:107`,
`:110`). So `fail/xmodule/` needs (i) `-not -path '*/fail/xmodule/*'` added to the
`fail/` find — exactly the precedent already set for `fail/warn/` — and (ii) its
own inline leg modelled on the `fail/warn/` loop (`:224-247`), reusing
`resolve_expected_pattern` (`:79-93`) and the ERE matcher (`:166`) verbatim. The
`fail/sema/` canonical `file:line:col` gate at `:146-153` is keyed on a path glob
and must gain an arm for `fail/xmodule/` or the located-error requirement of
done-conditions 2/3 is not actually enforced.

**F-F (baseline for U2/U5 `.bmod` assertions).** The only existing `.bmod`
*content* coverage in the entire repo is three greps at
`test_build/run_build_tests.sh:24-26`. There are no golden `.bmod` files. Since
this epic's whole thesis is "the `.bmod` becomes a true interface", U2 should
introduce a golden-`.bmod` assertion rather than accreting more greps.

---

## (a) The export-model inversion, and how U1–U5 discharge the 9 done conditions

### Restatement

BLang's module boundary currently exports exactly the wrong half of a type. A
`pub struct` ships its **full field layout** into the `.bmod` and **none of its
methods**, so a consumer can read and write an imported type's internals but
cannot construct it or call anything on it — the interface publishes the
implementation and hides the API. Worse, an exported declaration may reference a
non-`pub` type; the emitter writes the reference without the declaration, the
library build exits 0, and the failure surfaces as a *syntax error inside a
generated file* at the *consumer's* build (P9). This epic inverts the model:
methods and `init` become the API behind an explicit `pub` (private by default,
D9), member variables become always-private so struct literals are automatically
module-private and external construction has exactly one form `Counter(5)` (D9,
D7), and the `.bmod` becomes a true interface — signatures plus
protocol-conformance records (D16) plus enum variants/payloads (D17) plus a
compiler-facing metadata section that only `table`/`@json` shapes need and that
user source can never name (D15). The keystone constraint is that construction is
caller-allocating today, so removing layout from the interface is impossible
without first moving allocation to the library: hence U1, the factory ABI, leads.

### Unit → done-condition mapping

| DC | Done condition (abbrev.) | Owning unit | Supporting |
|----|--------------------------|-------------|------------|
| 1 | `test_build/` lib+bin: `Counter c = Counter(5);` + `c.bump()` builds and runs | **U2** | U1 (ABI it rides on) |
| 2 | Struct literal + field access on an imported type are located errors (`fail/xmodule/`) | **U5** | U3 (harness + origin marker) |
| 3 | `pub` parses on methods/`init`; unmarked method unreachable cross-module, callable in-module | **U3** | — |
| 4 | Non-`pub` type in an exported signature = located error at the **library** build (incl. enum payloads) | **U3** | — |
| 5 | `print("{}", x)` on an imported `impl Printable` type works E2E | **U2** | U3 (`pub` filtering) |
| 6 | Imported `@json table struct`: `query`/`to_json` work; naming the field is an error | **U5** | U2 (metadata section groundwork) |
| 7 | `BuildCache` key incorporates a `.bmod` format version; bump invalidates warm cache | **U2** | U5 (second format change) |
| 8 | `check_no_field_reachins.sh examples/ test_build/` exits 0; examples pass | **U5** | U4 (accessor surface + `examples/`) |
| 9 | All gates green in both build modes; `--leak-check`; LSP; build tests; docs updated | **all units** | verified at U5 |

**U1 owns no done condition on its own.** It is a de-risking spike whose output is
an ABI and a `test_build/` fixture; DC1 is credited to U2, which is where the
emitted `.bmod` first makes `Counter(5)` parse in a consumer. That asymmetry is
intentional and worth the reviewer's attention: if U1 is judged only by the epic
done-condition list it looks like it produces nothing, when in fact it produces
the precondition for DC1, DC2 and DC5.

**Coverage check:** every DC has exactly one owning unit; no DC is orphaned; no
unit is without a DC except U1 (by design, above).

---

## (b) U1 — the factory ABI, concretely

### b.0 What the code actually does today (verified, not quoted from the plan)

- **Construction is caller-allocating.** `CodeGen::genConstructExpression`
  (`CGStruct.cpp:603-717`): computes `dl.getTypeAllocSize(structType)`
  (`:641-642`), generates the destructor from the field list via
  `getOrGenStructDestructor` (`:660`), calls `__blang_rc_alloc_dtor(size, dtor)`
  (`:663`) — or `__blang_rc_alloc(size)` when the struct has **no** refcounted
  field and `getOrGenStructDestructor` returns `nullptr` (`:664-665`) — then calls
  `StructName_init(heapPtr, args...)` (`:667-711`).
- **The destructor is `InternalLinkage`**, named `__StructName_dtor`
  (`CGStruct.cpp:194-204`, `:264-265`), and walks `sd->getFields()` releasing
  `string`/`Array`/`Buffer`/fn-typed/user-struct fields (`:230-247`, `:283-306`).
  It returns `nullptr` outright when no field needs releasing (`:249-253`).
- **Release is already layout-free.** `__blang_rc_release`
  (`runtime/blang_runtime.c`) reads `hdr->destructor`, stored at allocation by
  `__blang_rc_alloc_dtor`, and calls it at refcount zero. **No runtime change is
  needed** — confirmed by reading both functions.
- **Structs are always heap pointers.** Method ABI is
  `StructName_method(ptr self, args...)`; `self` is the raw heap pointer, loaded
  from the variable's alloca and passed directly (`CGStruct.cpp:2017-2031`).
  Methods are declared `ExternalLinkage` with mangled name
  `[modulePrefix__]StructName_methodName` (`CodeGen.cpp:184-219`).
- **`Counter(5)` doesn't even parse for an imported type.** `QExpression.cpp:418-421`
  only builds a `ConstructExpression` when `sd->hasInit()` is true. A `.bmod`
  struct today carries no methods at all, so `hasInit()` is false and the parse
  falls through — this is the exact mechanism of P8, and it means U2's emission of
  an `init` signature is what makes the parse succeed. No parser change is needed
  for construction.

### b.1 Consequences that shape the design (all first-hand)

1. **No sret / no by-value struct ABI anywhere.** Every BLang struct value is a
   refcounted heap pointer; a struct is never passed or returned by value, and
   `getLLVMType` lowers a struct-typed field to `ptr`. The factory therefore
   returns a plain `ptr` and needs **no** sret attribute, no ABI classification,
   no alignment negotiation. The prompt asks for sret/by-value details: the honest
   answer is that they do not arise, and *that* is why the factory is cheap. If
   the spike discovers any by-value struct path, it is a wall (see b.6).
2. **`.bmod` structs are injected into `gScope` but NOT into the consumer's
   `mod->mStructList`** (`qcc.cpp:355-363`). The method-declaration loop at
   `CodeGen.cpp:171-221` iterates `mod->mStructList`, so it never sees imported
   structs. Consequence: a cross-module method call finds no LLVM function, and
   `genMethodCall` **returns `nullptr` silently** at `CGStruct.cpp:2014-2015`.
   That silent nullptr is a Principle III violation waiting to happen and must
   become a located diagnostic as part of this work.
3. **The clean seam already exists**: `CodeGen::registerExternalTypes`
   (`CodeGen.cpp:380-401`), called from `qcc.cpp:533` and `:659`, already receives
   the bmod structs/enums and is **outside** the flat-merge injection block. This
   is where the consumer-side declarations for imported types belong. It keeps U1
   entirely clear of the resolution path.
4. **Non-generic pub functions from a `.bmod` are already marked extern**
   (`qcc.cpp:350-351`, `f->setFunctionExtern(true)`) so codegen declares without
   defining and the symbol links from the `.a`. **The factory should ride this
   exact path**: if the factory is emitted into the `.bmod` as an ordinary
   `pub fn`, it needs *zero* new consumer-side codegen. This is the single most
   important design consequence in this audit.

### b.2 The ABI

For every **non-generic** struct with an `init`, the **defining module** emits:

```
symbol:  <modulePrefix__>StructName_new
type:    ptr (*)(<init parameter types...>)      ; C-level: void *StructName_new(...)
linkage: ExternalLinkage
body:    %p = call ptr @__blang_rc_alloc_dtor(i64 <size>, ptr @__StructName_dtor)
         ; or @__blang_rc_alloc(i64 <size>) when the dtor is null
         call void @StructName_init(ptr %p, <args...>)
         ret ptr %p
```

Naming: `StructName_new`, chosen to sit in the *existing* mangling family
(`StructName_init`, `StructName_method`, `__StructName_dtor`) and to pick up the
module prefix through the same code path. `_new` is not a BLang keyword and
cannot collide with a user method, because `new` is not a legal method name today
— **the spike must confirm this**, and if a user *can* write `fn new(...)`, the
factory moves to the reserved `__`-prefixed family (`__StructName_new`) which
already houses `__StructName_dtor` and `__enum_<Name>_box_dtor`.

Ownership contract: the factory returns a **+1 reference** — exactly what
`genConstructExpression` produces today, so every downstream ARC decision
(`trackTempStruct` at `CGStruct.cpp:715`, store-untracks-temp, scope-exit
release) is unchanged by construction. Destruction stays `__blang_rc_release`,
which reads the dtor pointer the *library* installed. **The consumer never
computes a size and never generates a dtor for an imported type.**

### b.3 How the `.bmod` declares it (U1 spike hand-writes it; U2 emits it)

The factory is declared as an ordinary exported function so it rides
`qcc.cpp:350-351` (non-generic pub fns are marked extern → declared, not defined,
and linked from the `.a`). The emitted form matches `emitFunction`'s existing
signature-only shape (`BmodEmitter.cpp:185-213`), so U2 needs no new syntax:

```
// today, mathlib.bmod:            pub fn add(int a, int b) -> int;
// the factory is the same shape:  pub fn Counter_new(int start) -> Counter;
```

The spike hand-writes this line into a `.bmod` to prove the ABI **before** any
emitter change. That is the whole point of spiking first: if the declaration form
is wrong, we learn it with zero `BmodEmitter.cpp` churn.

Open design point deferred to U2, not guessed here: whether the factory is
*additionally* marked in the `.bmod` as belonging to `Counter` (so U3 can filter
it by the `pub init`'s visibility rather than treating it as a free function).
A free-function spelling is the minimum that works; an attributed spelling is
cleaner. U1 does not need to decide.

### b.4 How the consumer lowers the two statements

**`Counter c = Counter(5);`**

1. Parse: `QExpression.cpp:418-421` requires `sd->hasInit()`. With the `.bmod`
   carrying an `init` signature (U2; hand-written in the U1 spike), this is true
   and a `ConstructExpression` is built. **No parser change.**
2. Sema: unchanged — the `ConstructExpression` resolves against the injected
   `StructDefinition`; arity/arg-type checks already run over the `init`
   signature.
3. CodeGen: `genConstructExpression` gains an early branch — *if* the struct's
   defining origin is not this module (b.5) *and* it is non-generic, emit
   `call ptr @Counter_new(i32 5)` and skip lines `CGStruct.cpp:640-712` entirely
   (no `getTypeAllocSize`, no `getOrGenStructDestructor`, no `_init` lookup).
   Then `trackTempStruct(heapPtr)` exactly as today, so ARC is untouched.
4. Scope exit: unchanged `__blang_rc_release`.

**`c.bump()`**

1. Parse/Sema: resolves against the method list the `.bmod` supplies (U2).
2. CodeGen: `genMethodCall` looks up `Counter_bump` by mangled name
   (`CGStruct.cpp:1985-1986`). For an imported struct nothing has declared it, so
   U1/U2 adds a declaration step hanging off `registerExternalTypes`: for each
   non-generic imported struct, for each method the `.bmod` provided **without a
   body**, create an `ExternalLinkage` declaration with the
   `(ptr self, params...)` type and **no entry block**.
   The "no entry block" is load-bearing: `CodeGen.cpp:257-283` unconditionally
   creates an entry block and an implicit return for anything it processes, so a
   bodyless method routed through that loop would become a **defined empty
   function** in the consumer — a silent miscompile (empty `Counter_bump`
   returning 0, colliding at link with the library's real one). The declaration
   step must therefore be separate from that loop, and the spike must assert on
   the emitted IR that `Counter_bump` is a `declare`, not a `define`.
3. Call: `self` = the loaded heap pointer, unchanged (`CGStruct.cpp:2021-2031`).

### b.5 The "is this type foreign?" predicate

U1 needs a minimal answer; U3 ships the real one (the workplan's lightweight
defining-origin marker, explicitly *not* Epic B's canonical identity). For U1 the
predicate is: **the `StructDefinition` came from a parsed `.bmod`**. There is no
such flag today — verified: `Type.h` carries `mIsPublic` on struct/enum/protocol/
function and `mIsExtern` on functions only; nothing records bmod provenance for a
struct. U1 adds one boolean (`mFromInterface`) set where `.bmod` modules are
parsed, and consumed in `genConstructExpression`. It is deliberately a flag, not a
graph node.

What exists today and is **not** sufficient, so the reviewer can check I am not
adding redundant state: `Module::isExtern()` (`Type.h:282-283`) is set once per
`.bmod` input (`qcc.cpp:400`) and is used only to skip Sema/location-dump/re-emit
— it never reaches individual definitions. `FunctionDefinition::mIsExtern`
(`Type.h:341-342`) is the `extern fn` **FFI keyword** flag that `qcc.cpp:350-351`
*overloads* to mean "declared-only", so a source `extern fn` and an imported
non-generic function are already indistinguishable — I will not overload it
further. `Scope::mImportedModules` (`Type.h:208-221`) tracks import *statements*,
not symbol provenance. `StructDefinition`/`EnumDefinition`/`ProtocolDefinition`
have **no** external/imported field of any kind.

### b.6 What "hitting a wall" looks like, and the fallback

The spike stops and raises a question — rather than widening scope — if any of:

- **W1.** A struct is ever passed or returned **by value** anywhere in codegen
  (would reintroduce a real ABI-classification problem the factory does not
  solve).
- **W2.** The `+1` returned by the factory cannot be reconciled with an existing
  ARC decision site without changing ARC semantics (an explicit epic non-goal).
- **W3.** `getOrGenStructDestructor` returning `nullptr` for field-free structs
  cannot be encapsulated (it can — the factory just calls `__blang_rc_alloc`).
- **W4.** Generic construction cannot be left alone. The generic path
  (`CGStruct.cpp:625-634`, `:647-658`) is consumer-side by design and the factory
  must not touch it; the spike's generic case is a **regression guard only**, per
  workplan/A6. If a generic factory looks necessary, that is a wall.

**Fallback if the ABI genuinely fails** (in preference order, all recorded rather
than silently chosen):

1. **Layout as compiler-facing ABI metadata** — the `.bmod` carries field
   count/kinds in the D15 metadata section (never source-nameable), and the
   consumer keeps allocating. Costs the rebuild-avoidance win but preserves every
   *visibility* done condition (DC2–DC6, DC8). This is the design-record's own
   named alternative (F1, design.md A1 "rejected") and is the fallback precisely
   because it is already understood.
2. **Library-emitted `size`+`dtor` accessor pair** (`StructName_size()`,
   `StructName_dtor_ptr()`) — keeps allocation consumer-side but layout private.
   Strictly worse than the factory (two calls, no init encapsulation) and only
   worth it if the wall is specifically in the *call* form.
3. **Stop and escalate.** If neither holds, the export model needs redesign and
   the epic pauses — the workplan's own instruction.

### b.7 U1 exit criteria

- A hand-wired `test_build/` lib+bin pair constructs, calls, and releases an
  imported non-generic struct across a `.bmod` with **zero** consumer layout
  knowledge, asserted by inspecting the consumer's `.ll`: no
  `__blang_rc_alloc_dtor` for the foreign type, no `__Counter_dtor` definition,
  `Counter_bump` present as `declare`.
- ASan/`--leak-check` clean; `ctest` green.
- A generic cross-module case still works, unchanged (regression guard).
- Full gate list green in both build modes.
- Spike write-up committed under the unit's speckit dir (evaluation.md evidence).

---

## (c) Files and subsystems expected to change

**U1 (this unit) — narrow by construction:**

| File | Change |
|---|---|
| `CGStruct.cpp` | `genConstructExpression`: foreign-struct branch calling the factory |
| `CodeGen.cpp` | factory emission for non-generic structs with `init`; declaration step for imported methods, hung off `registerExternalTypes` (`:380-401`) — **separate from** the body-emitting loop at `:171-283` |
| `CodeGen.h` | declarations for the above |
| `Type.h` | one `mFromInterface` bool on `StructDefinition` + accessors |
| `qcc.cpp` | **set that flag only**, where `.bmod` modules are parsed — see the confirmation below |
| `test_build/` | new lib+bin spike fixture + `run_build_tests.sh` assertions |
| `specs/0NN-construction-abi-factory/` | spec/plan + spike write-up |

**Later units (declared for the reviewer's scope check, not touched in U1):**
`BmodEmitter.{h,cpp}` (U2, U5), `BuildCache.cpp` (U2), `QImplBlock.cpp` (U3),
`Sema.cpp` (U3 P9 enforcement, U5 field/literal visibility), `SQLGen.cpp` +
`CGRuntime.cpp` `@json` generation (U5, D15 metadata), `stdlib/net.b`,
`stdlib/fs.b`, `stdlib/collections.b` (U4), `examples/` (U4/U5),
`test_files/fail/xmodule/` + `run_tests.sh` (U3), `tools/check_no_field_reachins.sh`
(U4/U5), `docs/language_design.md` §"Modules and Imports" (lines 581-707, which
currently documents `pub` on exactly four kinds and says nothing about members)
and `CLAUDE.md` (Principle I, in the same PR as each behavior change).

### Explicit confirmation — the flat-merge resolution path

**I will not modify the flat-merge resolution path in `qcc.cpp`.** Concretely, the
injection block at **`qcc.cpp:330-381`** — the loop that walks `bmodMap` and calls
`gScope->addSymbol()` / `gScope->addType()` for every public function, struct,
enum and protocol, with the `// This implements the flat merge.` comment at
`:333` — is **Epic B's seam and stays exactly as it is**. Its symbol-injection
semantics, its ordering, and its `bmodMap.clear()` at `:380` will not change.

The one thing U1 needs from `qcc.cpp` is to *stamp the provenance flag* on
structs parsed from a `.bmod`. I will do that at the **`.bmod` parse** site
(Phase 1, around `qcc.cpp:221-270`), **not** inside the injection loop, so the
merge code is untouched. If review judges even that to be too close to the seam,
the flag can instead be set inside `Module::Parse` for `.bmod` inputs, or derived
in `registerExternalTypes` from the fact that the struct arrived through that
call — I will take the reviewer's preference rather than defend the cheapest
option. Everything else U1 needs lives in `CodeGen`.

---

## (d) Open questions — escalating rather than guessing

**Q1 (BLOCKING U1) — is `_new` a safe symbol suffix?**
`StructName_new` collides if a user can define `fn new(...)` in an `impl` block
(the method mangling is `StructName_new` too, `CodeGen.cpp:188-190`). I will
*verify* this in the spike rather than assume; if it collides I will use the
reserved `__StructName_new` family. Raising it because the answer changes the
`.bmod` spelling that U2 then has to emit, and I would rather not churn it.

**Q2 (BLOCKING U2, surfaced by U1) — how should the factory appear in the
`.bmod`: a free `pub fn`, or a member of the struct?**
The free-function form costs zero new consumer codegen (it rides
`qcc.cpp:350-351`). The member form is cleaner for U3's `pub init` filtering. I
propose free-function for U1's spike and defer the decision to U2's spec, but
flagging it now because it is a `.bmod` **format** commitment and format changes
are what the U2 cache-version salt exists to protect.

**Q3 (non-blocking, needs a ruling before U2) — what happens when an imported
struct's `init` is private?**
Per D9/the design record a `pub struct` with a private `init` is
"constructible only inside its module." So the library should emit **no factory**
for it, and a consumer's `Counter(5)` must be a *located error*, not a link
failure. I will implement that as the intent, but the diagnostic wording and
whether the `.bmod` records "has a private init" (to tell "no such constructor"
from "constructor is private" — materially better for an LLM consumer per
Principle VI) is a design choice I would like reviewed rather than invented.

**Q4 (non-blocking now, blocks U5) — open question #1 from `overview.md`, the
cross-module spelling of generated data-contract functions
(`Todo_from_json`).** Already recorded as U5-owned. Noting only that
`examples/todo_app/main.b:160` couples it to a field reach-in, so U4's migration
of `req.body` and U5's answer to Q4 touch the same line — they should be
sequenced deliberately, not merged by accident.

**Q5 (process, for the manager) — speckit directory numbering.**
The workplan names U1's speckit dir `002-construction-abi-factory`, but
`specs/002-diagnostics-engine` already exists and `specs/` runs through
`028-integration`. I propose `029-construction-abi-factory` … `033-private-fields-opaque-bmod`
and will not create directories until this is confirmed, to avoid a rename later.

**Q7 (BLOCKING the epic done condition, for the manager) — CI does not run
`test_build/run_build_tests.sh`; see F-A in §0.4.** Done-conditions 1, 5 and 6 are
all backed by that suite. I propose U2 adds a `build-system` CI job. This is a
scope addition to U2 and I want it approved rather than assumed.

**Q8 (for the reviewer) — `BuildCache::computeKey` hashes content with no
filenames or separators (F-D).** In scope for this epic I will add only the
format-version salt REQ-009 asks for. Confirm you want the
filename/separator weakness left alone (I believe it should be filed, not fixed
here, to keep U2 reviewable).

**Q6 (environment, for the human) — see §0.1.** The toolchain is hand-assembled
and unversioned; CI must remain the authority for gate results. If the host is
meant to be provisioned, that should be fixed at the source.

---

## Constitution self-check

| Requirement | Status |
|---|---|
| VI — design artifact before implementation | this document; **no code written** |
| VI — security dimension mandatory for U1 | allocation ABI + dtor function pointers analysed in b.2/b.4; factory installs the dtor in the library, narrowing (not widening) consumer trust in foreign layout |
| III — reject, don't coerce | the silent `nullptr` at `CGStruct.cpp:2014` is called out as a defect to fix, not to inherit |
| II — tests | U1 exit criteria name the fixture and the IR assertions (b.7) |
| IV — ARC/runtime verified | no runtime change; `--leak-check` in U1's exit criteria |
| I — docs | `docs/language_design.md:581-707` + `CLAUDE.md` listed in (c); pre-existing count drift flagged in §0.2 |
| Audit pattern | handing to `critic` now; BLOCKER findings addressed before any implementation |
