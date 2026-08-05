# Known issues — epic modules-v2-exports

Deliberate simplifications, deferrals, and adjacent weaknesses **surfaced** by
this epic's work, recorded per constitution Principle VI ("a deliberate
simplification MUST be recorded in Known Issues with its rationale") and the
fix-or-file discipline. Each entry says what it is, why it was not fixed here,
and who owns it.

Filed at U1; extended by each subsequent unit.

---

## KI-1 — `BuildCache::computeKey` hashes content with no filenames or separators

**Filed**: U1 (audit Q8 / F-D). **Owner**: not this epic — file only.

`BuildCache::computeKey` (`BuildCache.cpp:121-144`) feeds SHA-256 the *contents*
of each source file back to back, then the `blang.toml` text, then the dependency
hashes. It never hashes filenames, never inserts a separator between files, and
carries no compiler or `.bmod`-format version.

Two consequences beyond the missing format version:

- Renaming a source file does not change the cache key.
- Moving a line from one source file to another within the same project does not
  change it either, because the concatenation is identical.

**Why not fixed here.** U2 adds the `.bmod` format-version salt that REQ-009
requires, and that is the part this epic's correctness depends on. Fixing the
filename/separator weakness is a behavioural change to the cache key for every
project (it invalidates every warm cache) and traces to no requirement in this
epic. Bundling it into U2 would make a reviewable unit unreviewable.

**Manager ruling (2026-08-05)**: file, do not fix in this epic. DC7's test must
bump the real format-version constant rather than a test-only stub, so the test
proves the shipped mechanism.

---

## KI-2 — the epic's local gate runs used a hand-assembled toolchain prefix

**Filed**: U1 (audit Q6). **Owner**: infrastructure, not the compiler.

The development host had no C++ toolchain at all — no `cmake`, `gcc`/`g++`,
`llc-18`, or LLVM dev headers, and no usable `sudo`. A working toolchain was
assembled without privileges by downloading the Ubuntu package closure
(`apt-get download --print-uris`) and extracting it with `dpkg-deb -x` into
`/home/ben/toolchain`, with four fixups: a truncated `cmake` `.deb` re-fetched;
`libLLVM.so.1` symlinked from the installed runtime package; a dangling
`libzstd.so` repointed at the system copy; and a `cc` symlink added (it is a dpkg
alternative, present in no `.deb`).

**Consequence, and the rule it imposes.** That prefix is not what CI uses, is not
version-controlled, and is not reproducible from the repo. **CI is the authority
for every gate claim in this epic.** Local gate output is corroborating evidence
only, and no PR may rest a claim on the local prefix alone.

**Manager ruling (2026-08-05)**: acceptable as a local dev aid under that rule.
If the devbot host is expected to be provisioned, that is a host-configuration
bug to fix at the source rather than per-run.

---

## KI-3 — `buffer`, `collections` and `cli` are exempt from module-private enforcement

**Filed**: U1 (audit N7; design decision A7). **Owner**: Epic B
(`modules-v2-graph`) closes it; U3/U4 work around it.

The namespaced stdlib modules (`net`, `fs`, `timer`, …) each get their own
`Scope` (`qcc.cpp:315-322`), which is a real module boundary that visibility
rules can be enforced against. The promoted modules `buffer`, `collections` and
`cli` are parsed **into the user's own scope** (`qcc.cpp:308-313`) — so no
boundary exists to enforce against at all.

They are therefore **exempt from module-private enforcement for this entire
epic**. This is not an oversight and not a bug in the enforcement: there is
nothing to enforce until per-module scopes exist.

**Why they are promoted in the first place**: a namespaced module's internal
string-returning calls hit a string-ARC double-free under the module-prefix
codegen (the rationale is recorded in the comment at `qcc.cpp:303-307`).
Demoting them before that root cause is fixed would ship a known double-free —
which is exactly the sequencing error review finding F3 caught in the original
plan.

**Consequence for U4**: `Map` and `Set` have no `init`; their only construction
form is the struct literal that U5 outlaws. U4 gives them a `pub init` and
migrates the call sites, so the corpus does not depend on a form that is being
removed.

---

## KI-4 — imported method signatures are exported wholesale until `pub` exists on impl members

**Filed**: U1 (design decision A3; reviewer MIN-6). **Owner**: U3, by design.

The `.bmod` currently ships **every** method of a non-generic exported struct,
not only the `pub` ones, because `pub` does not yet parse inside `impl` blocks
(`QImplBlock.cpp` accepts only `fn` / `init` / `static fn`). U3 adds the syntax
and flips the default to private.

This interim over-export is **deliberate and sequenced**, not a visibility hole
that slipped through: making methods reachable at all (P8) and making them
selectively reachable (D9) are two reviewable changes, and doing them in one
commit would produce a change no reviewer could check properly. Recorded here so
that anyone reading the `.bmod` between U1 and U3 knows the state is intentional.

**Consequence for U2**: any golden `.bmod` U2 commits will contain private
methods, and **U3 will have to update those goldens** when the `pub` filter lands.
That is the intended signal, not churn to avoid — a format change that does not
move a golden is a format change no reviewer can see.

---

## KI-5 — the factory symbol is prefix-free on the consuming side

**Filed**: U1 (reviewer MAJOR-2). **Owner**: U2 must plan around it; Epic B
closes it properly.

The factory symbol is mangled **with** the defining module's codegen prefix when
emitted (`__net__Socket_new`), mirroring method mangling, so two namespaced
modules that both define a `Socket` get distinct factories. But a **consumer**
derives the name from the struct name alone (`__Socket_new`), because the
defining module's prefix is not carried in the `.bmod`.

**Why this is sound today**: the only producers of `.bmod` files are `bcc build`
library projects, which run with no module prefix. The namespaced stdlib modules
that *do* get a prefix are combined into the consumer's own compilation, where
construction takes the inline path and no factory is involved.

**What breaks if that changes**: if a namespaced module ever ships a `.bmod`, the
consumer references a symbol the library never emitted — a **link error, not a
miscompile**, which is the failure mode to prefer. Closing it properly means
carrying the defining module's identity in the interface, which is Epic B's
canonical module identity (design record D5/D10), not this epic's.

**U2 actions** (both blocking):

1. If U2 makes any namespaced stdlib module produce a `.bmod`, this must be
   resolved first.
2. **The prefix-aware emission branch is currently dead code** (reviewer
   MINOR-B): no namespaced stdlib struct has an `init` today, so
   `mangleStructFactoryName` is never called with a non-empty prefix in any
   test. U2 must add coverage for it when it wires the `build-system` CI job —
   an untested branch is not a working branch, and the whole point of the
   prefix-aware form is to be correct the first time a prefixed module needs it.
3. **CI must provision the sanitizer archives** (reviewer MAJOR-6). U1's
   cross-module ASan leg lives in `test_build/run_build_tests.sh` and skips when
   `build-asan/` is absent. A `build-system` job with only the package list from
   audit F-A would print `SKIP` on every run while reporting green. The job needs

   ```yaml
   - run: cmake -S . -B build-asan -DBLANG_SANITIZE=address,undefined
   - run: cmake --build build-asan --parallel
   ```

   The leg itself now **fails rather than skips when `CI=true`**, so a
   provisioning regression is loud rather than invisible.

---

## KI-6 — the reserved `__` rule does not cover `extern fn`

**Filed**: U1 (reviewer MIN-2). **Owner**: accepted as correct behaviour.

Sema rejects source declarations whose name begins with `__`, which is what makes
the compiler's generated-symbol family (`__<Struct>_dtor`, `__<Struct>_new`,
`__enum_<Name>_box_dtor`) genuinely reserved rather than merely conventional. The
check covers functions, methods, structs, fields, enums, protocols and local
variables.

**`extern fn` is exempt**, and must be: it names a symbol defined outside BLang,
and the runtime's own entry points are `__blang_*` — 62 such declarations exist
in `stdlib/`. The consequence is that a determined author can still collide with
a generated symbol by declaring `extern fn __Counter_new(...)`, and would get a
link-time error rather than a diagnostic.

**Not fixed**, because the alternatives are worse: reserving `__blang_*` only
would leave the rest of the family open, and blocking `__` in `extern fn`
entirely would make the C runtime undeclarable. The residual hole requires
deliberately writing an `extern fn` that shadows a generated symbol, which is not
an accident anyone has.

---

## KI-7 — an imported struct is not yet fully opaque: query and `@json` codegen still read layout

**Filed**: U1 (audit b.7 / M7). **Owner**: U5 — a hard dependency, recorded so
U5 plans for it rather than discovering it mid-implementation.

The U1 factory removes the *construction* site's dependence on a foreign struct's
layout, but it is not the only consumer-side site that needs it:

- Query-row materialisation (`CGRuntime.cpp:1134-1145`) independently calls
  `getOrCreateStructType`, `dl.getTypeAllocSize(structType)` and
  `getOrGenStructDestructor(structDef, …)` to build one heap struct per result
  row.
- `@json` generation (`CGRuntime.cpp:1519-1651`) walks the field list in
  whichever module compiles the call.
- `Sema::checkTableField` validates `.field` references against
  `StructDefinition::getFields()` in the consumer's compile.
- `CodeGen::registerExternalTypes` itself still calls `getOrCreateStructType` for
  every non-generic imported struct, which builds the LLVM type from the shipped
  field list. U1 removed the *construction site's* dependence on layout, not this
  one.

**Consequence**: **U5 cannot drop field layout from the `.bmod` until these paths
are handled**, either through the D15 compiler-facing metadata section or by
routing them through the same library-emitted-entry-point idea the factory uses.
The factory alone does not make an imported struct opaque, and U1 does not claim
it does — field layout still ships in the `.bmod` today.

---

## KI-8 — struct-valued expressions inside a string interpolation are dropped

**Filed**: U2 (surfaced while writing the D16 fixture). **Owner**: unowned —
pre-existing, unrelated to this epic. **Severity: silent wrong output.**

String interpolation resolves bare identifiers of scalar/string type. Anything
else is **silently mishandled**, in two different ways, with no diagnostic:

**(a) a dotted expression is passed through as literal text**:

```blang
struct P { int x; string n; }
// ...
P p = P(7, "hi");
println("{}", "obj={p.x}");     // prints:  obj={p.x}
println("{}", "str={p.n}");     // prints:  str={p.n}
int q = p.x;
println("{}", "local={q}");     // prints:  local=7     <- only this works
```

`{self.x}` inside a method body behaves the same way. The placeholder is copied
to the output verbatim, so a program that looks right prints its own source text.

**(b) a bare STRUCT identifier yields an empty string**, rather than either
dispatching through `Printable` or being rejected:

```blang
S s = S(9);
println("bare={}", s);        // prints:  bare=S(9)   <- the placeholder path works
println("interp={}", "x={s}"); // prints:  interp=x=  <- interpolation drops it
```

So the same value renders correctly as a `{}` argument and vanishes inside an
interpolated literal. "Dotted expression" under-described this: the common thread
is that the interpolation path resolves a narrower set of expressions than the
format-placeholder path, and silently emits nothing (or source text) for the
rest.

**Why not fixed here**: it is in the string-interpolation parser/codegen, an
independent subsystem from anything U2 touches, and U2 already carries the format
version, conformance records, the `table` fix, goldens and a CI job. Fixing it
blind inside this unit would make the PR unreviewable.

**Workaround in the corpus**: `test_build/printlib/shape.b` copies fields into
locals before interpolating, with a comment pointing here.

**Recommendation**: fix as a standalone change with `fail/sema` coverage — the
right behaviour is either to evaluate the expression (dispatching through
`Printable` for structs, as the placeholder path does) or to reject the
placeholder, never to emit source text or nothing at all (Principle III).

---

## KI-9 — Printable dispatch through `print`/`println` passed the wrong `self` — FIXED in U2

**Filed and fixed**: U2. Recorded because it was a **silent wrong-output bug in a
documented feature** that survived every existing gate.

`println("{}", p)` on a struct implementing `Printable` printed garbage:

```
direct   = P(3,4)      // p.to_string() called explicitly — always correct
dispatch = P(-933207160,24747)   // println("{}", p) — garbage
```

**Cause** (`CGExpressions.cpp`): a struct value is a refcounted **heap pointer**,
and `genExpression` on a struct variable yields that pointer. The print path
nevertheless stored it into a fresh alloca and passed *the alloca* as `self` —
a pointer **to** the self pointer — so `to_string` read its fields out of a stack
slot. The stale comment ("val already holds the loaded struct value") dated from
an earlier model in which structs were values.

**Fix**: pass `val` directly. One line, plus the comment explaining why.

**Why it survived**: no test exercised it. `p.to_string()` called directly took a
different code path and was always correct, and the codegen matrix had no
Printable-through-a-placeholder case at all — despite `CLAUDE.md` advertising
"structs implementing it can be used in `{}` print placeholders". Locked in by
`test_files/codegen_printable_dispatch.b`, which asserts direct and dispatched
output agree, covers a refcounted (`string`) field, and mixes Printable and
non-Printable arguments in one call.

**Note for U2's own scope**: done-condition 5 (`print("{}", x)` on an imported
`impl Printable` type) was **unreachable** until this was fixed — the conformance
record makes dispatch resolve, but dispatch itself was broken.

---

## KI-10 — `println("{}", self)` inside a method hands a raw struct pointer to the string runtime

**Filed**: U2 (surfaced while fixing KI-9). **Owner**: unowned — pre-existing.
**Severity: silent wrong output; a type confusion at a runtime boundary.**

The struct-argument detection at the print dispatch site keys on the argument
being a `VariableExpression` whose `mVariable` has a struct type. The implicit
`self` parameter does not satisfy that test, so a `self` argument falls through
to the generic path and the raw struct pointer is passed to
`__blang_string_concat_many` **as if it were a `BlangString`**:

```blang
impl S {
	fn show(self) { println("self={}", self); }   // prints:  self=
}
```

It prints empty rather than dispatching through `Printable`, even when the struct
conforms — `println("{}", s)` on the same value from outside the method is
correct. Nothing is diagnosed.

This is a **type confusion across the C boundary**: the runtime reads a
`BlangString` header out of a pointer that is a struct. It happens to render as
empty here rather than crashing, which is luck, not safety.

**Why not fixed in U2**: it is the same dispatch site KI-9 touched, but a
different defect — `self`-argument *recognition*, not receiver-pointer
derivation — and the fix needs its own decision about whether `self` should be
Printable-dispatched, rejected, or given a distinct diagnostic. U2 already
carries the format version, conformance records, the `table` fix, goldens, a CI
job and the KI-9 regression; adding a second semantic change at the same site
would obscure both.

**Recommendation**: fix alongside KI-8 — they are the same underlying gap (the
set of expressions the print/interpolation paths can actually render) and should
get one coherent answer, with `fail/sema` coverage for whatever is rejected.
