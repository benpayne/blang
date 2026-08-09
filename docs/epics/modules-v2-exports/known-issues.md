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

## KI-4 — imported method signatures are exported wholesale until `pub` exists on impl members — CLOSED in U3

**Filed**: U1 (design decision A3; reviewer MIN-6). **Owner**: U3, by design.
**CLOSED (U3)**: `pub` now parses on impl members, visibility is private by
default, and the `.bmod` emits only `pub` members for non-generic structs. Every
committed golden was regenerated — the churn this entry predicted came due and
was paid. `.bmod` format moved 2 → 3, and a format-2 file is now REJECTED on
read rather than silently reinterpreted.

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

**Filed**: U2 (surfaced while writing the D16 fixture). **Owner: U4**
(manager ruling 2026-08-05 — see "Ownership" below). **Severity: silent wrong
output.**

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

**Filed**: U2 (surfaced while fixing KI-9). **Owner: U4** (manager ruling
2026-08-05 — see KI-8's "Ownership"). **Severity: silent wrong output; a type
confusion at a runtime boundary.**

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

---

## Ownership ruling — KI-8 and KI-10 belong to U4, and must land before U5

**Manager ruling, 2026-08-05.** Both are assigned to **U4** (stdlib opaque API),
not to a standalone fix and not left unowned.

**Why U4 owns them.** They are one defect asked at two sites — *which expressions
can this renderer actually render?* — and deserve one coherent answer rather than
two point fixes. U4 owns the question they answer: once fields are private and
the stdlib exposes accessors, **a method call and `Printable` are the only ways
to read data out of an opaque type**, and these bugs break exactly those two
spellings at the print surface.

**Two indirect done-condition exposures**, which is what makes this more than
tidiness:

- **DC8 pushes authors into the broken spellings.** The reach-in gate moves code
  from `p.x` toward `p.get_x()` and `"{...}"` interpolations. A migrated example
  writing `"{obj.field()}"` prints *its own source text*, and an integration
  script that only checks the exit code still passes. U5's corpus migration is
  when this would land in the corpus at scale.
- **`CLAUDE.md` advertises coverage no test has.** It states that Printable
  structs work in `{}` placeholders. That is false inside interpolated literals
  and false for `self`. This is the exact shape of KI-9, which survived precisely
  because the documented claim was broader than the tested one.

**Hard constraint**: these must be fixed **before U5's corpus migration**, not
after. Migrating the corpus onto spellings that silently misbehave would bake the
defect into every example and every golden, and the goldens would then *lock in*
the wrong output.

---

## KI-11 — a single-line `impl` block makes a generic struct's `.bmod` unparseable

**Filed**: U2 (surfaced while adding generic-conformance coverage for M-1).
**Owner**: unowned — **pre-existing**, and confirmed untouched by U2
(`sliceDefinitionSource` is `BmodEmitter.cpp:33-103`; U2's earliest change to
that file is at line 222).

A generic struct ships its method bodies as verbatim source slices, sliced from
the start of the method's line. When an `impl` block is written on **one line**,
the method's location *is* the `impl` line, so the slice takes the whole block —
and the emitter then wraps it in another `impl` block:

```blang
pub struct Holder<T> { T item; }
impl Holder { fn get(self) -> T { return self.item; } }
```

emits

```
impl Holder {
impl Holder { fn get(self) -> T { return self.item; } }
}
```

which fails to re-parse: `Expected 'fn', 'init', or 'static fn' in impl block`.

Multi-line `impl` blocks — the form every fixture and every stdlib module uses —
are unaffected, which is why this has never been hit.

**Scope check performed**: generic **conformance records** themselves are fine.
A generic struct with a multi-line conformance impl emits
`impl Summable for Pair { }` after its interface block and round-trips cleanly;
that is now covered by `test_build/mathlib` + `myapp`. The slicing defect is
orthogonal to M-1, so suppressing generic record emission would have removed
working behaviour to work around an unrelated bug.

**Current corpus exposure: nil.** Measured, not assumed:

- zero single-line `impl` blocks in `stdlib/`, `examples/` or `test_build/`;
- the six in `test_files/` (`codegen_ix_method_chain_field.b:10`,
  `codegen_ix_match_bind.b:6`, and four `fail/sema` fixtures) are all on
  **non-generic** structs, which never ship method bodies in a `.bmod`;
- the only generic structs anywhere are `Pair<T>` (multi-line) and
  `collections.b`'s `Map`/`Set` (multi-line, and exempt this epic per KI-3).

Residual risk is therefore a **future** library shipping a single-line `impl` on
a **generic** struct — the one combination that triggers it.

**Recommendation**: slice from the method's `fn` token rather than the start of
its line, or record a precise body span at parse time. Needs its own change and
its own fixtures.

---

## KI-12 — a `sync` receiver is not locked around a method call

**Filed**: U2 (noticed while fixing B1). **Owner**: not this epic — file only.

Taking a method receiver from the variable's *address* (which is what
`genMethodCall` has always done, and what print dispatch now does too) means the
`sync` lock/unlock that `genVariableExpression` emits around a *read* is not
emitted around a *method call*. So:

```blang
sync Counter c = Counter(0);
c.bump();          // no lock is taken for the duration of bump()
```

`sync` therefore provides **no mutual exclusion for method bodies at all** — only
for whole-value reads through a variable expression. Print dispatch is now
consistent with method calls, which is the right consistency; but the shared
behaviour is itself weaker than `sync` implies.

Not U2's to fix: it is a concurrency-semantics question (should a `sync` receiver
hold the lock for the whole call? what about re-entrancy and nested calls?) that
needs a design decision, not a codegen tweak.

---

## KI-13 — `bcc build` swallows qcc's located diagnostic

**Filed**: U2 (noticed while adding the B2 negative leg). **Owner**: not this
epic — file only, but it undercuts this epic's value proposition.

`qcc` produces a precise located diagnostic:

```
sizeapp/main.b:8:2: error: imported type 'Point' is not printable — its interface
declares no 'impl Printable for Point'
```

but through the normal user path `bcc build` reports only:

```
error: compilation failed for sizeapp
```

The located line never reaches the user. This is **pre-existing `bcc` behaviour**,
not caused by U2 — but this epic exists to replace confusing cross-module
failures with precise located ones (P9), and every diagnostic it adds is
invisible through the command users actually run. The build tests see the good
message only because they invoke `qcc` directly.

**Recommendation**: forward the child's stderr. Small change, large effect on the
epic's user-visible value; worth scheduling within this epic if a unit has room.

---

## KI-14 — two rules decide whether a struct is Printable

**Filed**: U2 (B2). **Owner**: U5 convergence item.

Print dispatch answers "is this type Printable?" two different ways:

- **local** struct — does it have a method named `to_string`?
- **imported** struct — does its interface carry `impl Printable for X`?

Both are commented at the dispatch site, and the split is deliberate: it keeps
same-module behaviour byte-identical while making the conformance record
load-bearing (without which the record is decorative, and dispatch breaks once
U3's `pub` filter hides non-public methods).

But it is two rules for one core protocol, and the local rule accepts a type that
never declared conformance. **U5 should converge them** — most likely by
requiring `impl Printable for X` everywhere — as part of the pass where
visibility rules are finalised. Recorded now so the divergence is a decision
rather than a drift.

---

## KI-15 — `impl ForeignProtocol for MyStruct` records are silently dropped

**Filed**: U2 (reviewer MAJOR-1). **Owner: U5.**
**Not a regression** — see below.

`BmodEmitter::emit` builds its set of resolvable protocol names from the modules
being emitted, plus the hardcoded builtin `"Printable"`. A protocol that arrives
through a **dependency's** `.bmod` is never in that set, so `emitConformances`
skips the record.

Reproduced with a two-library chain: `protolib` exports `pub protocol Drawable`;
`uselib` depends on it and declares `impl Drawable for Shape`. `uselib.bmod`
contains `Shape`, its `init` and `draw` signatures — and **no conformance
record**.

**Why this is not a regression, and why the skip is still right.** Before the N1
fix the same source emitted the record anyway, producing a *forward/dangling
reference* that made `uselib.bmod` unparseable — the N1 class, a library whose
interface no consumer can read. Skipping strictly improves that: a missing record
degrades one capability, an unparseable interface destroys all of them. And
naively re-widening the predicate to "emit every recorded conformance" is
**exactly how N1 happened**. Do not do that; the fix is to widen the set of names
the emitter can *resolve*, not to stop checking.

**No observable impact today**: `Printable` is the only conformance anything in
the compiler consumes (print dispatch), and it is in the set. Generic constraint
checking against a foreign protocol has no consumer yet.

**What this caps.** REQ-007 / D16 is delivered for **`Printable`** and for
**same-module user-defined protocols**, and **not** for foreign protocols.
**U5 must not read a green suite as "D16 complete"** — the suite contains no
foreign-protocol conformance, by construction, because one cannot currently be
expressed in a `.bmod`.

**Why U5.** U5 does the cross-module data-contract work where generic constraint
checking against an imported type first becomes reachable. Closing it needs
either the dependency's protocol names threaded into the emitter's resolvable set
(the emitter would have to read the dep `.bmod`s it was given), or a decision
about how a foreign protocol is *named* in an interface at all — which runs into
re-export and qualification (D8, Epic B). That is a design choice, not a patch.

---

## KI-16 — `mDefiningFile` has no file → module mapping

**Filed**: U3 (M-3 obligation 1). **Owner**: Epic B (`modules-v2-graph`).

`mDefiningFile` holds a source file's base name. A library split across several
`.b` files therefore yields several distinct values, so a module-private rule
that compared those strings directly would **reject legal intra-library access**.

**Obligation 2 is done**: origin stamping lives in one shared
`stampDefiningOrigin()` called by both `qcc` and `blangd`, so the compiler and
the editor cannot disagree about a definition's origin.

**Obligation 1 — the mapping itself — is deferred**, and the deferral is safe
because **nothing reads the field today**. U3's rules are keyed on predicates
that already carry the right meaning: the private-`init` check uses
`isFromInterface()` (the ABI predicate) and P9 uses `isPublic()`. No rule in this
epic needs the mapping.

**Owner rationale**: mapping a file to a module is a fragment of canonical module
identity, which is Epic B's (D5). Building a half-identity here would be a second
mechanism Epic B then has to reconcile or delete.

**Constraint for whoever picks it up**: do not key a module-private rule on this
field until the mapping exists, and populate it on the `lsp/Compile.cpp` path so
`qcc` and `blangd` cannot diverge.

---

## KI-17 — stdlib `FileOps` conformance methods were not `pub`

**Filed and FIXED in U3.** Recorded because it is the first real-code consequence
of the sixth P9 surface, and because a reviewer seeing `stdlib/*.b` in a U3 diff
should know why.

`File.read`/`write`/`close` (`stdlib/fs.b`) and `Socket`/`ServerSocket`'s same
three (`stdlib/net.b`) implement the exported `FileOps` conformance but carried
no `pub`. Under the new rule that is a library-build error — correctly: the
conformance is exported, so the methods that satisfy it are API.

Marked `pub`. This is not a workaround: those methods *are* the public surface of
`File` and `Socket`, and every caller in `examples/` already uses them. The rule
found real under-marking, not a false positive.

---

## KI-18 — an interface-version mismatch reports correctly, then cascades

**Filed**: U3 (reviewer MINOR-8). **Owner: U5.**

U3 made `qcc` validate a `.bmod`'s format version on read and reject a mismatch
with a correct located primary diagnostic:

```
lib.bmod:1:1: error: interface file was produced by a different compiler version
(.bmod format 2, this compiler expects 3); rebuild the dependency
```

But the read path then `continue`s to the next input, so the consumer's own
**correct** source is analysed against a dependency that was never loaded, and
the user gets a spurious cascade underneath the real error:

```
consumer.b:4:12: error: Failed parse varible
consumer.b:5:9:  error: Failed to find symbol 'c'
```

**The irony is worth stating**, because it is the argument for fixing it: this is
a small instance of exactly the failure mode the P9 narrative exists to end — a
correct file blamed for a defect that lives somewhere else. The primary
diagnostic is right; everything after it is noise pointing at innocent code.

**Suggested fix** (reviewer's, and I agree): treat an interface-version mismatch
as **fatal** — stop before analysing dependents rather than continuing. Nothing
downstream can be trusted once an interface failed to load, so there is no value
in the extra diagnostics and real cost in the misdirection.

**Why U5 owns it**: U5 bumps the format version again (layout → D15 metadata), so
it is the unit that will actually exercise a format transition against a warm
cache and real dependents. Fixing it there means the fix is tested by the
transition rather than by a synthetic fixture. U4 touches no `.bmod` version
path.

---

## KI-19 — multi-argument generics in exported signatures are untested

**Filed**: U3 (reviewer NIT-9). **Owner: U4** (must not assume coverage);
underlying parser limitation is pre-existing and unowned.

U3 widened `checkExportedTypeRef` to recurse into generic type ARGUMENTS before
its early returns, so `pub fn take(Box<Secret> b)` is now correctly rejected
(fixture `p9_generic_arg_private_type.b`). That fixture uses a **one**-argument
generic.

The **two**-argument form cannot be tested, because the PARSER rejects it first:

```blang
pub fn take(Map<string, Secret> b) -> int { ... }
//                    ^ expected ',' or ')'
```

A multi-argument generic type in a parameter position does not parse. This is a
**pre-existing parser limitation**, not something U3 introduced — but the
consequence is that the widened recursion is **unverified for multi-argument
containers**, so nobody should assume `Map<K,V>` in an exported signature behaves
like `Box<T>` does.

**Flagged explicitly at U4** because U4 redesigns `Map`/`Set`'s public API. If
U4's accessor surface puts a `Map<K,V>` or `Set<T>` into any exported signature,
it will meet the parser limitation first — and must not read "no P9 error" as
"P9 checked it".

---

## KI-20 — a `shared`/`sync` struct as a string-interpolation part passes the wrong self pointer

**Status: RESOLVED** (U4-remainder, PR #1). The interpolation-part self pointer now
routes through a shared `structSelfPointer()` helper (taken from the receiver's
ADDRESS, exactly as `genPrintCall` already did), so a `shared`/`sync` struct part
renders correctly instead of passing its first 8 bytes as a pointer. Regression
fixture `test_files/codegen_interp_struct_qualified.b` **segfaults without the fix**.

**Filed**: U4 salvage-merge (independent review, MINOR-1). **Owner: U4 remainder**
(same renderer the unit owns).

The KI-8/KI-10 fix renders a struct-valued interpolation part
(`"{obj}"`) by calling `genPrintableToString( val )` where
`val = genExpression(part)` (`CGExpressions.cpp:1079`). That is correct for a
heap struct pointer, but `genVariableExpression` **double-loads** a `shared`/`sync`
variable, so for a `shared`/`sync` struct local `val` is the first 8 bytes of the
struct interpreted as a pointer, not the struct address. The sibling direct-print
path (`genPrintCall`, `CGExpressions.cpp:1506`) already takes the self pointer from
the variable's *address* to avoid exactly this. So `"{s}"` where `s` is a
`shared`/`sync` struct would pass a garbage self pointer (wrong output or segfault).

**Not a regression** — struct interpolation parts were fully broken before U4, and
this shape is untested. But it is inconsistent with the fix applied one function
over. The U4-remainder run should route the interpolation-part self pointer through
the same address-based path as `genPrintCall`, and add a `shared`/`sync`-struct
interpolation fixture.

---

## KI-21 — `println("{}", h.inner)` on a field-access struct arg still hits the KI-10 raw-pointer path

**Status: RESOLVED** (U4-remainder, PR #1). `genPrintCall`'s struct-arg detection now
resolves a FIELD-access receiver via `receiverStructDef` (not just a
`VariableExpression`), so `println("{}", h.inner)` renders through Printable. The
duplicated `hasPrintable` detection is unified into `structIsPrintable()` (shared with
`genPrintableToString`), the self-pointer logic into `structSelfPointer()`, and the
dead `fnName` local is gone. Regression fixture:
`test_files/codegen_print_field_struct.b` (prints empty for the field-access
direct-print without the fix).

**Filed**: U4 salvage-merge (independent review, MINOR-2). **Owner: U4 remainder**.

The KI-10 fix covers the *interpolation* path (a field-access struct part renders
correctly) and the *direct-print variable* path (`genPrintCall` treats a
`VariableExpression` arg as a struct at `CGExpressions.cpp:1405`). It does **not**
cover a **field-access** struct arg in the direct-print form: `println("{}", h.inner)`
where `h.inner` is a struct still falls through to the raw-pointer-as-`BlangString`
path — the same KI-10 defect, one argument shape not covered.

Pre-existing and out of the salvage diff's scope, but it is the KI-10 class and
belongs to the renderer question U4 owns. Once opaque types force reads through
methods/`Printable`, this shape becomes reachable in real code. Fix in the
U4-remainder run with a field-access direct-print fixture; unify the `hasPrintable`
struct-arg detection between `genPrintCall` and `genPrintableToString` (the review
also flagged a dead `fnName` local at `CGExpressions.cpp:1484` and the duplicated
detection logic — clean up in the same pass).

---

## KI-22 — `for x in <generic-struct-method-call>()` mis-resolves the loop-var element type

**Status: RESOLVED (U5b, run `aeba092e`, commit `22ee5f7`).** Fixed as its own commit
within the U5 phase per the product-owner ruling (OQ#2). Two coupled fixes in
`CGStatements::genForInStatement` (+ a `CGTypes::methodReturnElementType` helper): (1)
the for-in element-type inference now handles the `MethodCallExpression` case, resolving
the method's `Array<K>` return element type through the receiver's type-argument
substitution (`K -> string` for a `Map<string,int>`); (2) the iterable, when it is a
fresh owning array temp (method/call result), is untracked before the loop body so the
body's block-scope cleanup cannot release it mid-loop (the actual use-after-free), and is
released once after the loop instead. `for k in m.keys()` now needs no
intermediate-typed-var workaround. Fixture `codegen_forin_generic_method.b` (+golden,
`--leak-check` clean). The scoped fix stayed within the described `methodReturnTypeName`
family — the escape hatch was not needed. Original report retained below.

**Filed**: U4-remainder (run `aeba092e`), discovered while migrating `examples/wordfreq`
onto the `Map.keys()` accessor. **Owner: U5** (raise for a ruling — a codegen change
outside U4's file set). **BLOCKING for the "just add `()`" migration promise on
generic collections.**

A for-in loop whose iterable is a **method call on a generic struct** returning
`Array<K>` binds the loop variable with the WRONG element type — the receiver's
type arguments are not substituted into the method's return element type, so the
loop variable defaults to `int` and a `string` element is read as an integer
(garbage), then dereferenced (segfault). Minimal reproduction:

```blang
Map<string, int> m = Map<string, int> { keys: [], values: [], buckets: [] };
m.set("a", 1);
for k in m.keys() { println("{}", k); }   // k bound as int → garbage, then segfault
```

Confirmed **independent of the method↔field name collision** (a distinctly-named
`keys_probe()` fails identically) and **specific to the direct for-in-over-method-call
source**: two forms work today —

- an **intermediate typed var**: `Array<string> ks = m.keys(); for k in ks { ... }`
  (Sema/codegen resolve the element type from the declared `Array<string>`); and
- the **field-access** form `for k in m.keys` (a plain field, element type resolved).

So the element-type resolution for a for-in source is missing the
`callReturn/methodReturn`-through-substitution path that the assignment path already
has. Likely `CGStatements.cpp` for-in element-type inference + `methodReturnTypeName`
substitution (the same family the KI-8(b) `receiverStructDef` work touched, one rung
over).

**Why it matters now**: the whole opaque-exports thesis pushes iteration from
`for k in counts.keys` (field, works) toward `for k in counts.keys()` (method, broken)
— exactly the KI-8/KI-10/KI-20/KI-21 pattern ("once types are opaque, method reads are
the only spelling, and this shape breaks it"). It is **load-bearing for U5's corpus
migration**, where `Map`/`Set` iteration is pervasive, and it partially contradicts
spec `032`'s naming-rule promise that migration is "purely adding `()`" for the
generic-collection iteration case (spike S5 validated the promise only for a
non-generic `HttpRequest.method()`).

**Disposition in U4-remainder (this PR)**: NOT fixed here — a codegen change is outside
U4's stated file set (`stdlib/*.b`, `examples/`) and the "stop and raise, don't widen
scope" execution rule applies. `examples/wordfreq` is migrated with the **intermediate
typed-var form** (correct, idiomatic, DC8 gate green) so U4 ships unblocked. Raised to
the manager for a ruling: fold a scoped codegen fix into U5, or handle as its own unit.
Until fixed, the `check_no_field_reachins.sh` migration guidance for a generic
collection is "bind to a typed `Array<...>` local, then iterate," not "add `()` in
place."

---

## KI-23 — combine-mode namespaced-stdlib field/literal privacy is grep-gated, not Sema-enforced (Epic B)

**Filed**: U5 design audit (run `aeba092e`, 2026-08-09; `rev` blocking finding point 1). **Owner: Epic B**
(`modules-v2-graph`, per-module scopes). **Deliberate simplification** (Principle VI).

U5 makes field access and struct literals on a cross-module struct a located Sema error. The rule keys on
`StructDefinition::isFromInterface()` — true only for a struct that **arrived through a parsed `.bmod`**
(`qcc.cpp:463`, the `.bmod`-input branch). That is **not** the same as "is cross-module":

- **The Type.h contract.** `Type.h` (443-446, 467-475) documents `mFromInterface` as **an ABI predicate
  only** — "must construction go through the library-emitted factory?", explicitly **NOT** "is this member
  visible here?", with a standing instruction **not to overload the flag for visibility**. U5 reuses it for
  the field/literal rules, but ONLY in rules that are themselves **scoped to `.bmod`-arrival** (which is
  exactly what the flag holds) — so it is not a general-visibility overload. The initial U5 design draft
  claimed a false "from-interface == imported" equivalence and named a synonym `isImported()`; the design
  audit rejected that and it was removed (spec `033` §4.2).

- **The gap.** A **combine-mode namespaced stdlib** struct (`net`/`fs`/`timer`, each with its own scope) is
  parsed from `.b` **source** under `--combine`, so `setFromInterface(true)` is never called for it —
  `isFromInterface() == false` — yet it **is** cross-module. U5's compile-time field/literal enforcement is
  therefore **`.bmod`-path-only this epic**; a consumer reaching into a namespaced-stdlib struct's fields in
  combine mode is **not** caught by Sema. That privacy stays **grep-gated** by
  `tools/check_no_field_reachins.sh` (the reach-in gate covers the stdlib DTO fields).

**Closed by Epic B**, which introduces real per-module scopes and a canonical module identity: the
field/literal rules then key on "defined in a different module than the use site" — covering `.bmod` AND
combine-mode namespaced stdlib uniformly — and the ABI flag is no longer read for visibility at all. Tracked
alongside KI-16 (the file→module mapping Epic B owns).
