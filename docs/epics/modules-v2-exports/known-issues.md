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

**U2 action**: if U2 makes any namespaced stdlib module produce a `.bmod`, this
must be resolved first.

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

**Consequence**: **U5 cannot drop field layout from the `.bmod` until these paths
are handled**, either through the D15 compiler-facing metadata section or by
routing them through the same library-emitted-entry-point idea the factory uses.
The factory alone does not make an imported struct opaque, and U1 does not claim
it does — field layout still ships in the `.bmod` today.
