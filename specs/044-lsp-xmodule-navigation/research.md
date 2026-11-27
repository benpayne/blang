# Research: Cross-module go-to-definition and hover (U4)

Phase 0 of `/speckit-plan`. Every finding below comes from a **probe** run against
the U3 branch head (`7e6183c`, `build/blangd`). The probe was a throwaway
`tools/lsp_client.py` script that opened the real fixtures with
`initializationOptions.stdlibPath = ${ROOT}/stdlib` and sent `definition`/`hover`
at the case positions. The table below records the baseline U4 starts from.

## Probe baseline (pre-U4 behavior)

| Case | Position (0-based) | definition today | hover today |
|------|--------------------|------------------|-------------|
| qualified dep fn `mathlib.add` (myapp 5) | `add` | `test_build/mathlib/mathlib.bmod` 37:4 (**a `.bmod`**) | `fn add(int a, int b) -> int` |
| aliased `m.add` (aliasapp 9) | `add` | `mathlib.bmod` 37:4 (**a `.bmod`**) | `fn add(int a, int b) -> int` |
| name-cap construct `Counter(5, "hits")` (counterapp 6:14) | `Counter(` | `counterlib.bmod` 3:4 (**a `.bmod`**) | `struct Counter` |
| name-cap type position `Counter c` (counterapp 6:1) | `Counter` | `null` | `null` |
| `midx.get_box(7)` (usebox 13) | `get_box` | `midx.bmod` 4:4 (**a `.bmod`**) | `fn get_box(int n) -> Box<int>` |
| use-cap method `b.get()` (usebox 14:18) | `get` | `null` | `null` (not even a resolved type) |
| sibling `helpers.triple` (multifile 5) | `triple` | `projects/multifile/helpers.b` 1:4 ✓ | `fn triple(int x) -> int` |
| prelude construct `Map<string, int>()` (prelude_no_import 6:27) | `Map` | `stdlib/collections.b` 17:4 ✓ | `struct Map` |
| prelude type position `Map<string, int> m` (6:4) | `Map` | `null` | `null` |
| stdlib fn `net.http_not_found()` (todo_app 188) | `http_not_found` | `stdlib/net.b` 234:4 ✓ | `fn http_not_found() -> HttpResponse` |
| stdlib type position `net.HttpServer server` (todo_app 200:6) | `HttpServer` | `null` | `null` |
| struct literal `net.HttpServer { … }` (codegen_http_routing 47) | any | `null` | `null` |

## R1 — Cross-file definition largely works already; the defects are `.bmod` targets and missing arms

**Decision**: U4 does not rewrite navigation. It adds three things to the
existing `AstLocator`/`Server` path:

- **(a)** an **interface → source** mapping for any target whose file is an
  interface input;
- **(b)** a **use-capability member** fallback for method calls that Sema leaves
  unresolved;
- **(c)** a **type-reference** arm for type names written in type position.

Hover gets a **foreign renderer** (R6). Every other path stays byte-identical.

**Rationale**: `Server::definition` already emits `pathToUri(target.file)` for
whatever file the declaration's `SourceLocation` carries. Stdlib, prelude and
sibling declarations are parsed from their real `.b` files in the same
`ProjectCompile::run`, so they already land on source (rows marked ✓).
Only interface-derived declarations carry the nominal
`<depDir>/<name>.bmod` path.

**Alternatives considered**:

- *Stamp source locations onto the emitted `.bmod` text* (for example, a
  `// at file:line:col` comment per declaration, re-read by the parser). This
  would change the `.bmod` format, which is forbidden (overview "Never touch").
- *Resolve consumers against the dependency's source AST instead of the
  interface.* Rejected by design.md D2: it exposes private members, so the
  editor would disagree with `bcc`.

## R2 — Interface → source mapping (FR-002/FR-003)

**Decision**: Build a per-compile **`InterfaceSourceMap`** owned by
`CompileResult`, keyed by the interface input path (`<depDir>/<name>.bmod`, the
`CompileInput::path` U3 gives each closure interface). It points at that
dependency's kept source `Run` (`ProjectContextResult::depRuns`, one per walk
node: `ProjectContext.cpp:229`).

To map an interface declaration `D` whose `getLocation().file` is a key:

1. Find the dependency `Run` for that key. If there is none (the dependency's
   compile failed or it is unresolved), return **null**.
2. Search the `Run`'s recorded **source** modules (`ModuleRecord.role` is a
   source role, not `kInterface`; a library compile has no stdlib inputs, per
   U3) for the top-level declaration of the same **kind** and **name**:
   - for a free function, `getFunctionList()`;
   - for a struct, `getStructList()`;
   - for an enum or protocol, the corresponding lists;
   - for a method or `init`, the struct of the owning name, then its method of
     the same name (`init` → the struct's init method).
3. Exactly one match: use its `SourceLocation`. Zero or more than one match:
   return **null**. Never return the `.bmod` location.

Transitively shipped declarations (`Box<T>` reaching `usebox` through
`midx.bmod → boxq.bmod`) carry **boxq.bmod**'s path, because the injection
parses each closure interface as its own input. They therefore map to
**boxq**'s run, not midx's. The implement phase verifies this on the
usebox golden. If the generic body turns out to be attributed to `midx.bmod`,
step 2 runs over every closure run in topological order and still requires a
unique match.

**Rationale**: This uses only data the shared compile already produced: input
paths, module records, AST declaration lists, and the dependency's own
`ProjectCompile::run` (FR-003). An interface declaration exists only because
the one exporter (`BmodEmitter::emit`) chose to export it. Matching it by kind
and name inside the module it was exported from therefore re-applies no export
or visibility rule; it only re-finds the declaration the exporter started
from. BLang rejects duplicate functions in one scope and requires unique type
names per module, so kind+name is unique within one library module. The
"exactly one" requirement covers multi-file libraries.

**Alternatives considered**:

- *Carry a source back-pointer from the emitter into the interface AST.* This
  would mean either changing the parser for interface inputs or a
  blangd-specific side channel through `BmodEmitter`. The kind+name lookup
  needs neither.
- *Map by line number into the `.bmod` text.* Fragile, and it re-derives the
  emitter's layout.

## R3 — Use-capability method resolution (Story 2) comes from the shared use-capability type set

**Finding**: `b.get()` gets no `mResolvedMethod`. `Sema::structForType`
(Sema.cpp:1428) resolves the receiver's type by **name in the source scope**
(`mScope->findSymbol("Box")`). Under D7, `usebox` cannot name `Box`, so the
lookup fails and Sema returns silently. Codegen dispatches this call through
`mStructDefMap`, which `registerExternalTypes(allStructs, allEnums)` fills.
`qcc.cpp:390-417` builds `allStructs`/`allEnums` inline: every recorded
module's structs, minus a shadowed prelude provider's
`suppressedPrelude()` entries. That collection is the **use-capability rule**.

**Decision**:

- **Lift the collection loop** verbatim out of `qcc.cpp` into the shared
  frontend as `CompileContext::useCapabilityTypes(structs, enums)`. This is
  `ProjectCompile.h/.cpp`, with no LLVM. It is behavior-neutral, and `qcc`
  now calls it, as in U1.
- **Use it in blangd** when a `MethodCallExpression` has no `mResolvedMethod`:
  1. take the receiver's Sema-resolved type name (the `VariableExpression`'s
     declared/inferred type, or `getResolvedType()`), using the same receiver
     derivation Sema uses, factored as a shared static helper;
  2. find the **unique** struct of that name in `useCapabilityTypes`. More
     than one candidate (for example, boxa's and boxb's `Box`) means **null**,
     never a guess;
  3. take that struct's method of the same name. This is the loop Sema runs,
     factored as a shared `StructDefinition` lookup helper that Sema also calls.
- **Do not change Sema's stamping.** Making Sema resolve through the
  use-capability set would start reporting "has no method" in `qcc` for
  receivers it skips today. That is a user-visible diagnostic change, which
  is forbidden.

**Rationale**: The editor reuses the exact set and the exact member rule the
compiler uses for the same call. Nothing is copied, and `qcc` output stays
byte-identical: the extraction is proven by `test_codegen.sh` goldens and an IR
byte-identity spot check.

**Alternatives considered**:

- *Have Sema stamp through the fallback but suppress the errors.* This changes
  codegen inputs (`setResolvedType`), so IR could move.
- *A blangd-only "search every module for a struct named X".* Forbidden: it is
  a reimplementation that can drift, for example by ignoring the prelude
  shadow filter.

## R4 — Type names in type position (name-capability and stdlib-type cases)

**Finding**: A type in type position has no AST node of its own.
`Type::Parse` (QType.cpp) returns the scope's **shared registered** `Type`
instance for a non-generic name, so no per-use location or definition link
exists. The locator therefore lands on the enclosing `VariableDeclaration` and
answers null. The struct-literal route (`net.HttpServer { … }`) is not usable
either: Sema's `findType(mTypeName)` does not resolve the qualified literal
name, and fixing that would be a resolution change.

**Decision**: Add a **type-reference record** in the parser, at the point
where the compiler itself resolves the type token:

- In `QType.cpp`, after `nsScope->findType(memberName)` (qualified) or
  `s->findType(symName)` (bare) succeeds, look up the same scope's symbol of
  that name.
- If it is a `StructDefinition`, `EnumDefinition` or `ProtocolDefinition`,
  push `TypeReference { SourceLocation of the name token, name length,
  non-owning def pointer }` into a sink.
- The sink is installed per module parse by `ProjectCompile::run` **only when
  `CompilePlan::recordTypeReferences` is true**. blangd sets it; `qcc` never
  does, so `qcc` does zero extra work and its behavior cannot change.
- The records live on the `ModuleRecord`, owned by the `CompileContext`, which
  is owned by the `CompileResult` (KG-7). Generic parameters and core types
  are not symbols, so they are never recorded.

**Rationale**: The definition the editor jumps to is exactly the one the
parser bound the token to. That covers name-capability, qualified stdlib types
and prelude types alike, with no second resolver. Records carry exact token
spans, which is more precise than the locator's start-column approximation.
They are consulted **before** `locate()` for a cursor inside a span.

**Same-file constraint (FR-007)**: The type-reference arm answers only when the
target's (mapped) source file differs from the document's path. Same-file type
positions keep answering null, so pre-U4 behavior holds exactly. No existing
golden queries a type position (`grep` of every `defn_*`/`hover_*` position);
the restriction is kept anyway for strict compliance.

**Alternatives considered**:

- *Re-resolve the type name in blangd from the declaration's scope.* This
  re-implements qualified lookup, `isModuleImported` and the ambiguity rules.
- *Use only construction sites* (`Counter(…)`, `Map<…>()`). This works for
  `Counter` and `Map`, but `net` has no constructible public type, so the
  stdlib-type case would be impossible.

## R5 — The viewing module, the defining module, and qualifier bindings (FR-008 data)

**Decision**: All inputs to the display rule come from the shared compile's
records and AST:

- **Viewing module**: the `ModuleRecord` whose `path` equals the document's
  path (U3 M6: the document's input uses its URI path exactly). Its
  `Module::getImports()` gives `ImportStatement { getModuleName() (real),
  getLocalQualifier() (alias or name) }`.
- **Defining module of a declaration**: the `ModuleRecord` whose `path` equals
  the declaration's (pre-mapping) `SourceLocation.file`. For an interface
  declaration this is the interface record. Its `moduleName` is the dependency
  name, which equals the module name a `.bmod` `// foreign-type:` header prints.
  Its `role` identifies a prelude provider (`isPreludeProvider`), which renders
  bare.
- **Qualifier**: the set of `getLocalQualifier()` values over imports whose
  `getModuleName() == defining.moduleName`.
  - Not empty: use the lexicographically smallest.
  - Empty (use-capability only): use `defining.moduleName`.
  - Core types (no record) and the declaration's own generic parameters
    render bare.
- **Type names inside a signature** (`Box<int>` in `get_box`'s return,
  `HttpResponse` in `http_ok`): `Type` carries only a name. Resolve it to a
  definition the way the parser does for that declaration:
  1. look the name up in the **declaring module record's scope**
     (`findSymbol`), and keep the result if it is a struct, enum or protocol;
  2. otherwise, use the unique same-named entry in `useCapabilityTypes` (R3).
     This covers interface placeholders that the foreign-type pre-scan
     registered;
  3. otherwise, render the name as written.

  Then apply the rule to the definition's record.

**Rationale**: Every value is deterministic and derived from data the compile
already holds. No new name table is introduced.

## R6 — Hover format

**Decision**: For a **foreign** target, meaning one whose defining record is
not the viewing record, hover uses a new renderer:

| Target | Rendering |
|--------|-----------|
| free function | `fn <disp>.<name>(<params>) [-> <ret>]` |
| method / `init` | `fn <ownerDisp>.<name>(<params>) [-> <ret>]` |
| struct | `struct <disp>[<G1, G2>]` |
| enum | `enum <disp>[<…>]` |
| protocol | `protocol <disp>` |

- Parameters and the return type use the existing `declText`/`typeText`
  shapes, with each nested foreign type substituted per R5. `self` stays bare.
- The text sits in the existing ` ```blang ` markdown block.
- Same-module targets go through the **unchanged** `AstLocator::hoverText`
  renderer.
- The renderer reads the **mapped source** declaration where one exists, so
  parameter names come from the source. It falls back to the interface
  declaration, which has identical signature text because the emitter copies
  signatures verbatim. If neither exists, hover returns null, consistent with
  definition.

## R7 — Goldens and determinism

**Decision**: New fixtures go under `test_files/lsp/`:

- `xmod_defn_*.lsp.jsonl` and `xmod_hover_*.lsp.jsonl`, one per case group;
  the contract has the list;
- `xmod_defn_not_open`, where the target file is not open;
- `xmod_unsaved_dep_defn`, where the target file is open with edits that move
  the declaration.

Every fixture sets `initializationOptions.stdlibPath` to `${ROOT}/stdlib`, so
URIs never contain a build directory. Fixtures open the existing projects in
`test_build/` and `test_files/lsp/projects/` read-only; no new compiler
fixture is needed. The latency and corpus legs are unaffected, because
navigation runs only on request.

## R8 — Ownership and no-subprocess

**Decision**: The only new state is in two places:

- `InterfaceSourceMap` holds `std::map<std::string, std::shared_ptr<Run>>`,
  sharing the `Run`s that `CompileResult::deps` already holds. It is declared
  after `deps` in `CompileResult`.
- `TypeReference` vectors live on the context's `ModuleRecord`s.

Both die with the result. Nothing is cached across reparses. `parser_no_leak`
gains a loop of 50 reparses of usebox plus a definition request under LSan. No
code path added here performs I/O, except reading the AST already in memory.
`blangd_no_subprocess` is unchanged.
