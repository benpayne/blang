# U3 design note — Three type tiers: core / prelude / library

**Epic**: modules-v2-graph · **Unit**: U3 · **Speckit**: `036-type-tiers`
**Covers**: REQ-003, REQ-004, and REQ-005 (retire-promotions half).
**Design artifact (Principle VI)** — presented to @auditor **before implementation**
because U3 carries a real design-decision surface (the mixed-module promotion
mechanism and its codegen-prefix implications). Binding: D12, D13, D14; F3.

**Revision 2 (2026-08-09), after auditor CHANGES-REQUIRED:** Options A/B settled
(**Option B** confirmed) and the hard-located-error stance confirmed. Two blocking
defects resolved: **B1** — `Task`/`Array` are compiler-known **core**, not D14
deletions; only `Buffer`'s bare-name registration is deleted (§6). **B2** — prelude
**load** policy specified: `collections.b`/`buffer.b` are **unconditionally
combined** so `Map`/`Set`/`Buffer` are in scope with **zero imports** (§5a), with a
positive zero-import fixture as the D13 teeth. Non-blocking N1–N4 folded.

**Depends on**: U2 (merged, PR #145) — the string-ARC path is regression-locked, so
demoting `cli` no longer ships the double-free.

---

## 1. Goal (done-conditions 2 + 3 retire-half)

Replace the accidental seven tiers with three declared ones:

- **core** — compiler-known (primitives, `string`, `Array`, `Option`/`Result`,
  `Printable`, `print`/`println`/`to_json`, FFI, `chan`). Unchanged this unit.
- **prelude** — a **fixed, compiler-shipped** list of ordinary-BLang types that are
  automatically in scope, unqualified, everywhere: exactly **`{Map, Set, Buffer}`**
  (types only, D12/D13). Users/libraries cannot extend it.
- **library** — the default: imported and qualified.

Concretely:
1. Replace the hardcoded `isGlobalTypeLib` string list (`qcc.cpp:351-352`) with a
   **declared prelude manifest** that is the single source of truth.
2. **Delete only** the `Buffer` bare-name `addType` registration
   (`QModule.cpp:587`) — the one with no definition behind it (D14) — so `Buffer`
   resolves from its parsed prelude definition and an undefined type name fails **at
   the type**, proven by a parse-only `fail/sema` fixture. **`Task` and `Array`
   (`QModule.cpp:585-586`) are compiler-known core and stay registered** (B1; §6).
3. **Demote `cli`** to an ordinary qualified library module (`cli.has_flag(...)`).
4. **Mixed module** `collections.b`: its **types `Map`/`Set` → prelude**, its **free
   function `sort` → qualified `collections.sort`**. Migrate the bare/`<T>` `sort`
   call sites.

## 2. Current seams (recon 2026-08-09, post-U1/U2 master)

| Seam | Location | Note |
|------|----------|------|
| Promotion list | `qcc.cpp:351-353` | `isGlobalTypeLib = buffer\|collections\|cli` → parsed into `combineScope` (no namespace, **no module prefix**). |
| Namespace path | `qcc.cpp:359-366` | other stdlib modules get a `kScope_Namespace` + a **module prefix** (`net__…`). |
| Bare-name addType | `QModule.cpp:585-587` | `Task`/`Array`/`Buffer` names with no definition (D14 target). |
| `collections.b` | `Map` `:18`, `Set` `:175`, `sort` `:287` | the mixed module. |
| `buffer.b` | `Buffer` `:14` only | pure prelude-type module (no free fns). |
| `sort` call sites | `test_files/codegen_sort.b`, `codegen_sort_desc.b` (`sort<int>(...)`, both `import collections;`), `examples/wordfreq/main.b` (bare `sort(...)`) | **3 files**; behavior identical, so **stdout goldens do not change** — only the call spelling. |

Prelude membership `{Map, Set, Buffer}` is exactly today's promoted set **minus
`cli`**, so migration for those names is zero — only `cli` and `collections.sort`
move.

## 3. The design decision: how to promote prelude types from a mixed module

`Map` must resolve **unqualified** while `sort` (same file) must be
**qualified**. A single `.b` file parses into one scope, so they cannot both sit
in `combineScope`. Two candidate mechanisms:

### Option A — namespace the module (prefix) + promote the prelude type symbols

Parse `collections.b`/`buffer.b` into a real `kScope_Namespace` (like `net`/`cli`),
**with a module prefix**, then copy each **prelude type** it defines (its
`StructDefinition` + a `Type` registration) up into `combineScope`. `sort` stays in
the `collections` namespace → `collections.sort`. Uniform (all stdlib modules become
real namespaced boundaries — progress toward KI-3).

- **Risk**: gives `collections`/`buffer` a **module prefix**, exercising the
  module-prefix codegen path. **Assessed:** for `Map`/`Set` (generic) it is a
  non-issue — generic struct methods mangle via `mangleGenericName` (base + U1
  digest + args, **no** module prefix; `CGTypes.cpp`), so their symbols are
  prefix-independent already. **`Buffer` is non-generic**, so its methods *would*
  pick up a `buffer__` prefix (`CodeGen.cpp:185`) — a genuinely new configuration.
  U2 characterized the prefixed string-ARC path as clean, but U2's fixture is
  generic-free free-functions; a prefixed **non-generic struct with methods** used
  unqualified across the promotion is not something U2 proved. **This is the risk
  the auditor should weigh.**

### Option B — keep prelude modules prefix-free; route only free functions to a namespace (RECOMMENDED)

Parse `collections.b`/`buffer.b` into `combineScope` **as today (no prefix)** —
driven by the **manifest** (promote a module into `combineScope` iff it defines a
prelude type), so the codegen path for `Map`/`Set`/`Buffer` is **byte-identical to
today** (zero new prefix risk). For a **mixed** module, additionally register a
`collections` **namespace** and route its **non-prelude free functions** (`sort`)
there for qualified access, and **withhold them from unqualified `combineScope`
visibility** so bare `sort(...)` no longer resolves (a located error) and
`collections.sort(...)` does.

- **Pro**: no change to the `Map`/`Set`/`Buffer` codegen path (the thing most likely
  to regress); the only behavioral change is `sort`'s spelling and `cli`'s demotion.
- **Con**: the promotion becomes "types + selected symbols to `combineScope`, other
  free functions to a sibling namespace" — a slightly more nuanced injection than a
  whole-file scope choice. Mechanically: after `Module::Parse`, iterate the module's
  symbols; a prelude-type struct (and its `Type`) → `combineScope`; every other
  top-level symbol → the `collections` namespace (and `addImportedModule` so
  qualified access resolves). `buffer.b` has no free functions, so it needs no
  namespace — pure prelude promotion.

**Decision: Option B (CONFIRMED by auditor, rev 2).** It satisfies the
done-condition (prelude types unqualified; `collections.sort` qualified; `cli`
demoted; manifest is the single source of truth; `Buffer`'s bare-name `addType`
deleted) while keeping the load-bearing `Map`/`Set`/`Buffer` codegen path unchanged
— the smallest blast radius consistent with the design. Option A's
uniform-namespacing is desirable long-term but drags in the non-generic-`Buffer`-
prefix question that belongs with the broader uniform-namespacing work (U6/KI-3),
not the tier declaration. `cli` **is** demoted to a real prefixed namespace (it
defines no prelude type and no type at all — pure free functions), which is exactly
the U2-unblocked step.

**N2 (fold):** confirm during implementation that routing the generic free function
`sort` into a `collections` namespace does **not** perturb U1's identity-folded
generic mangling/dedup. `sort` is a generic **function** (not a type), and U1
deliberately does **not** stamp generic-function symbols with a module digest
(KG-1) — `sort<int>` still mangles `sort_int`. Namespacing only changes the
*resolution path* (`collections.sort` → the same `FunctionDefinition`), not the
emitted instance symbol, so `linkonce_odr` dedup is unaffected. A codegen fixture
(`collections.sort<int>`) plus `--leak-check` verifies this at implementation.

## 4. `cli` demotion specifics (F3-gated, now unblocked)

`cli.b` becomes an ordinary namespaced module (prefix `cli`): `cli.has_flag(...)`,
`cli.flag_value(...)`, etc. Its internal use of `Map` resolves via the prelude
(unqualified). This is precisely the prefixed-internal-string-return path U2's
`nsarc` fixture regression-locks — so the double-free cannot silently return.
Migrate `test_files/codegen_cli.b` + `examples/kv/main.b` to the qualified spelling
and confirm `--leak-check` clean (the live proof U2's lock guards).

## 5. Manifest representation

A single compiler-shipped constant (proposed `PreludeManifest.h` or a `static const`
in `QModule.cpp`): `{"Map", "Set", "Buffer"}`, types only, with a comment stating it
is **closed** (D13: users/libraries cannot extend it; adding a name is a language
change with the one-way-door bar). Consumed by (a) the driver's combine-scope
routing and (b) any diagnostic that explains "this name is prelude." The `Buffer`
bare-name `addType` deletion (D14, §6) is independent.

## 5a. Prelude LOAD policy (D13 — B2 resolved)

**The gap the auditor caught (B2):** the note governed scope *routing* but was silent
on *loading*, and today's loading contradicts D13. `bcc` combines `buffer.b`
**unconditionally** (`bcc.cpp:1015-1021`, the always-shipped `stdlibFiles`) but
`collections.b` **only on `import collections;`** (`bcc.cpp:1024` `kKnownOrder`,
import-gated); `qcc` force-loads no prelude at all; `test_codegen.sh` gates
`collections.b` on `import collections|cli`. So `Map`/`Set` are **unavailable
without an import** — library behavior wearing a prelude label, which **violates
D13** ("prelude is automatically in scope, unqualified, everywhere — never
imported").

**Policy (auditor's recommended option (a)): unconditional prelude load.** The
modules that define prelude types are combined **unconditionally**, so their prelude
types are always in scope with **zero import lines**:

- **`bcc`**: move `collections` out of the import-gated `kKnownOrder`
  (`bcc.cpp:1024`) into the **always-combined** `stdlibFiles` set (where `buffer`
  already lives), preserving order (`buffer.b` before `fs`/`net`; `collections.b`
  before `cli.b`, which uses `Map`). Confirm `buffer.b` stays unconditional.
- **`test_codegen.sh`**: always include `buffer.b` **and** `collections.b` (drop the
  `import collections|cli` gate for *loading*; still combine `cli.b` only when
  imported since `cli` is library-tier now).
- **the qcc combine harness / any other combine driver**: same — prelude modules are
  always present.

**D13 teeth (positive fixture).** Add a `codegen_*.b` fixture that uses `Map`,
`Set`, **and** `Buffer` with **ZERO `import` lines**, compiles, and runs (committed
stdout golden). This is U3's real D13 proof: prelude types need no import. Pairs with
the §6 negative (Buffer-without-prelude parse-only error) — together they show
`Buffer` resolves **iff** its prelude definition is loaded, and that the loader
always loads it.

**Documented asymmetry (N3).** `collections.sort` **still requires
`import collections;`** — it is a **library-tier free function** (name-capability,
D7), not a prelude type. So within the *same* mixed module, `Map`/`Set` need no
import while `sort` does. This is intentional and is the concrete manifestation of
"tiers are assigned per name, not per module" (done-condition 2). It is documented
in `language_design.md` (N3, §7).

## 6. Bare-name `addType` — reclassify Task/Array to core, delete only Buffer (D14; B1 resolved)

`QModule.cpp:585-587` registers three bare names with no definition behind them:

```cpp
s->addType( new Type( "Task" ) );    // line 585
s->addType( new Type( "Array" ) );   // line 586
s->addType( new Type( "Buffer" ) );  // line 587
```

The original note proposed deleting all three (D14). **The auditor rejected that
(B1), correctly:** two of them are compiler-known **core** types, not "names with
nothing behind them":

- **`Task` → core.** It drives the spawn/wait ABI: a `getLLVMType` special-case
  (`CGTypes.cpp:77`) and pointer-return handling (`CGTypes.cpp:620-621`), and it is
  used in `test_files/pass/wait_basic.b`, `pass/spawn_expr.b`, `codegen_wait.b`.
  Deleting its registration would break `spawn`/`wait`. **Keep registered**,
  reclassified as **core** (compiler-known, justified under D14: it *does* have the
  compiler behind it, just not a BLang source definition — exactly the core tier's
  admission criterion).
- **`Array` → core.** It has a `getLLVMType` special-case (`CGTypes.cpp:90`) and
  dozens of `== "Array"` ARC/type predicates; `addType("Array")` is its **sole**
  scope-resolution path (no `QType` special-case creates it). **Keep registered**,
  reclassified as **core**.
- **`Buffer` → the genuine D14 deletion.** `Buffer` is an ordinary BLang struct
  (`stdlib/buffer.b:14`) with no compiler special-case; its bare-name registration
  is the one that "buys nothing but a later, worse error." **Delete line 587.** Its
  resolution moves to the **parsed prelude definition** in `stdlib/buffer.b`, which
  §5a makes unconditionally in scope — so `Buffer` still resolves everywhere, now
  *with a real definition behind it*.

**done-condition-2 fixture (D14 teeth).** A **parse-only** `fail/sema` fixture that
uses `Buffer` as a type **without the prelude present** (i.e. `Buffer b;` compiled
by `qcc` alone, no `--combine` of `buffer.b`) must now fail **at the type** with a
located `file:line:col: error:` (unknown type `Buffer`), asserting the error column
points at the **`Buffer` type token** — proving resolution comes from the definition,
not a hollow registration. (`Task`/`Array` stay resolvable standalone, being core —
so they are *not* valid D14 probes, which is exactly B1's point.)

## 7. Migration scope & goldens

- `test_files/codegen_sort.b`, `codegen_sort_desc.b`: `sort<int>(...)` →
  `collections.sort<int>(...)`. **Stdout goldens unchanged** (identical behavior).
- `examples/wordfreq/main.b` (×3 call sites): `sort(...)` → `collections.sort(...)`;
  its integration golden unchanged.
- `test_files/codegen_cli.b`, `examples/kv/main.b`: unqualified `has_flag(...)` etc.
  → `cli.<fn>(...)`. Stdout goldens unchanged.
- Mind the KEEP-IN-SYNC contract with `BmodEmitter` (`QModule.cpp:607-613`) — the
  core builtins list is untouched; only the `Buffer` bare-name `addType` (line 587)
  is deleted (`Task`/`Array` stay).

**N1 — migration ownership:** the 3 `sort` + 2 `cli` call-site migrations land **in
the U3 PR**, not deferred to U6's corpus sweep. They are tiny (5 files, spelling
only, no stdout-golden change) and are the *direct* observable consequence of this
unit's tier reassignment — deferring them would leave U3's own change unexercised
(bare `sort`/`has_flag` would still resolve via the very promotion U3 removes, so
the PR could pass without proving the demotion). U3 must be self-consistent: the PR
that removes the promotion also fixes every call site the promotion supported.

**N3 — language_design.md** documents the mixed-module rule: prelude **types**
(`Map`/`Set`/`Buffer`) need no import; a **same-module free function** (`sort`) is
library-tier and needs `import collections;` — tiers are per name, not per module.

## 8. Gates

`run_tests.sh` both modes; `test_codegen.sh` + `--leak-check` (the `cli`/`nsarc`
prefixed path stays clean); `test_lsp.sh`; `test_build/run_build_tests.sh`; goldens
updated only where a spelling change is visible (expected: none in stdout). Docs:
`docs/language_design.md` (type tiers) + `CLAUDE.md`.

## 9. Auditor items — dispositions (rev 2)

All settled; note re-submitted for re-audit.

- **Q1 Option A vs B — RESOLVED: Option B** (auditor-confirmed), prefix-free prelude
  modules, route free functions to a `collections` namespace (§3).
- **Q2 `sort`/`has_flag` unqualified → hard located error — RESOLVED: confirmed.**
  After demotion, bare `sort(...)`/`has_flag(...)` is a hard `file:line:col: error:`
  (unknown symbol); no deprecation shim (full enforcement is U6, but these specific
  demotions error now).
- **B1 (was Q3) — RESOLVED:** `Task`/`Array` reclassified **core** (kept
  registered, compiler-known); only **`Buffer`** bare-name registration deleted;
  the D14 probe is **Buffer-without-prelude**, parse-only, asserting the error
  column at the type token (§6).
- **B2 — RESOLVED:** unconditional prelude load for `collections.b`/`buffer.b`
  across `bcc` + `test_codegen.sh` + the qcc combine harness; positive **zero-import
  Map/Set/Buffer** fixture as D13 teeth; `collections.sort` asymmetry documented
  (§5a, N3).
- **N1** migration lands in the U3 PR (§7). **N2** generic-`sort` namespacing does
  not perturb U1 mangling/dedup — verified by a `collections.sort<int>` codegen +
  `--leak-check` fixture (§3). **N3** language_design.md documents the per-name
  mixed-module rule (§5a, §7). **N4** verified line numbers: `addType` at
  `QModule.cpp:585-587` (Task 585 / Array 586 / Buffer 587).

**Status:** B1 and B2 resolved; A/B and hard-error settled. Holding U3
implementation for @auditor's re-audit clearance.
