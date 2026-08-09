# U4 design note — Module graph & per-module export scopes (extractable resolver)

**Epic**: modules-v2-graph · **Unit**: U4 · **Speckit**: `037-module-graph-and-resolver`
**Covers**: REQ-006, REQ-007 (done-conditions 4 + 5) · **Depends on**: U1 (merged).
**Design artifact (Principle VI)** — ONE bounded audit pass requested (SIGN-OFF /
CHANGES-REQUIRED). Binding: P4/P5; the design record's "extract a resolution
service both `qcc` and `blangd` call."

**U4 is BEHAVIOR-NEUTRAL.** It is a *refactor + seam*, not a semantics change: the
same scopes are built and the same symbols resolve as today. The flat-merge global
injection **stays** (its removal is done-condition 6 / U6). All prior gates stay
green with no behavior change.

---

## 1. Goal

1. Extract the resolution environment into a **standalone named class** (`Resolver`),
   NOT inline in `qcc.cpp main()`.
2. Make it the **single shared entry point** constructed by **both** `qcc.cpp` and
   `lsp/Compile.cpp` (both call sites grep-verifiable) — the Epic C seam. blangd
   stays single-file; it does NOT resolve cross-module here.
3. Two `ctest`s: the resolver's own unit test (driver-independent) + a
   `ResolverReuseTest` that constructs the resolver as `lsp/Compile.cpp` does and
   asserts a fixture resolves identically to the `qcc` path.

## 2. Current seams (recon, post-U3 master)

| Path | What it builds today |
|------|----------------------|
| `qcc.cpp main()` | `createGlobalScope()` → `gScope`; (combine) a `preludeScope`→`combineScope` chain; per-file scope routing (user→combine, cli→combine, prelude→holding namespace, other stdlib→namespace) into `moduleNamespaces`; up-front `.bmod` flat-merge injection into `gScope`; prelude PROMOTE. All **inline**. |
| `lsp/Compile.cpp compileDocument()` | `createGlobalScope()` → `gScope` (static, process-lifetime); one file `Scope` parented to `gScope`; parse + `stampDefiningOrigin` + `Sema`. Single-file; no combine/prelude/bmod. |

The **shared kernel both already do**: build the global builtin scope, then create a
module scope parented into that environment. That kernel — plus the module-node /
namespace registry `qcc` layers on — is what U4 names as `Resolver`.

## 3. Design — the `Resolver` component

A named class (`Resolver.h`/`Resolver.cpp`, in the frontend sources so blangd and qcc
both link it) that **owns the resolution environment** and exposes the operations
both drivers need:

```cpp
class Resolver {
public:
    Resolver();                                   // builds + OWNS the global builtin
                                                  // scope (createGlobalScope())
    QLang::Scope *globalScope() const;            // the shared gScope

    // A module node's scope. `blangd` calls this once (single file). `qcc` calls it
    // per input; in combine mode the module scopes chain through the prelude/combine
    // hierarchy the driver sets up via the methods below.
    QLang::Scope *newModuleScope( QLang::Scope *parent = nullptr );

    // Module graph (used by qcc combine; present-but-unused by blangd this epic —
    // Epic C wires blangd through them). Behavior-neutral: same registrations as the
    // inline code did.
    void registerNamespace( const std::string &name, QLang::Scope *ns );
    QLang::Scope *resolveNamespace( const std::string &name ) const;
    // ... (import-edge bookkeeping the driver already keeps in moduleNamespaces)
};
```

**Ownership & lifetime.** `Resolver` owns the global scope via a `SmartPtr<Scope>`
(the `Frontend.h` contract: a refcount-0 raw `gScope` would be freed when the first
child's parent ref drops). It sets the `gScope` global alias on construction (both
drivers rely on it), restoring the prior value on destruction so nested/2nd
constructions in one process (the LSP server, tests) don't clobber each other.

**What moves vs. stays (bounded).**
- **Moves into `Resolver`:** global-scope construction + ownership; the
  `gScope`-alias management; module-scope creation; the namespace/import registry
  (`moduleNamespaces` becomes resolver state). This is the shared kernel + the graph
  bookkeeping.
- **Stays in `qcc.cpp` (this unit):** the combine-specific routing *policy* (which
  file → which scope), the `.bmod` flat-merge injection (removal is U6), and the U3
  prelude LOAD/PROMOTE — these call the resolver's primitives but the *policy* is not
  yet generalized. Keeping them in the driver keeps U4 a small, reviewable,
  behavior-neutral extraction; U6 moves resolution fully through the graph.
- **`lsp/Compile.cpp`:** replaces its inline `createGlobalScope()`/`fileScope`
  construction with a `Resolver` (a process-static `Resolver` preserving today's
  process-lifetime global scope) + `newModuleScope()`. No functional change —
  `test_lsp.sh` stays byte-identical.

## 4. The two call sites (DC5, grep-verifiable)

- `qcc.cpp`: constructs a `Resolver`, uses `globalScope()` + `newModuleScope()` (and
  the namespace registry) where it previously built scopes inline.
- `lsp/Compile.cpp`: constructs a `Resolver`, uses `globalScope()` +
  `newModuleScope()`.

A `grep -n "Resolver" qcc.cpp lsp/Compile.cpp` shows both constructing the one
component — the seam Epic C consumes without re-plumbing.

## 5. The two ctests (DC4 + DC5)

- **`ResolverTest`** (driver-independent): construct a `Resolver`, create a module
  scope, register a namespace, resolve a builtin type and a registered namespace —
  asserts the resolver works with no `qcc`/`bcc` involvement.
- **`ResolverReuseTest`**: construct the resolver **exactly as `lsp/Compile.cpp`
  does** (global + one module scope), parse a small fixture through it, and assert a
  chosen name resolves to the **same** definition the `qcc` single-file path yields
  for the same source — proving the editor and compiler share one resolution truth.

Both registered with `add_test` (CMake), linking `BLANG_FRONTEND_SOURCES` +
`blang_sha256` (as blangd does), no LLVM.

## 6. Behavior-neutrality (the guarantee)

- Same scopes, same parenting, same injection order → identical resolution. Proven by
  **all prior gates staying green with no golden changes**: `run_tests.sh` (both
  modes), `test_codegen.sh` (+`--leak-check`), `test_lsp.sh`, `test_build`.
- The global flat-merge injection is **retained** (DC4 explicitly behavior-neutral;
  removal is DC6/U6).
- blangd is **not** wired cross-module (Epic C); it constructs the resolver but
  resolves one file, exactly as today.

## 7. Out of scope (hold the line)

- Removing the global injection block (U6 / DC6).
- Enforcing imports, use/name capability, collision diagnostics (U6/U7).
- Wiring blangd to resolve across modules (Epic C).
- Generalizing the combine routing *policy* into the resolver (U6).

## 8. Questions for the auditor (bounded)

1. **Extraction boundary** (§3 "moves vs. stays"): is owning the global scope +
   module-scope factory + namespace registry the right minimal seam for a
   behavior-neutral U4, with the combine *policy* + bmod injection left in the driver
   until U6? (Recommendation: yes — smallest change that satisfies DC4/DC5 and gives
   Epic C a real entry point.)
2. **`gScope` alias management**: Resolver sets/restores the `gScope` global on
   construct/destruct. Acceptable for the single-process LSP + tests, or prefer the
   drivers keep setting `gScope` explicitly? (Recommendation: Resolver owns it, save/
   restore, so there is one owner.)
