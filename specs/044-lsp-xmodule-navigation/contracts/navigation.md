# Contract: cross-module `textDocument/definition` and `textDocument/hover` (U4)

## Request handling (unchanged wire format)

- **definition**: the result is a `Location`
  `{ uri: "file://<abs path>", range: { start, end } }`. The range is
  zero-length at the declaration's 1-based location converted to 0-based (the
  pre-U4 convention). When no target exists, the result is `null`.
- **hover**: the result is
  `{ contents: { kind: "markdown", value: "```blang\n<one line>\n```" } }`,
  or `null`.

## Resolution order for a cursor (blangd)

1. **Type reference.** If the cursor lies inside a recorded `TypeReference`
   span of the document's module, and its target is **foreign**, that
   reference is the target. A same-file target falls through, which preserves
   pre-U4 null.
2. **Existing locate.** Otherwise, `AstLocator::locate`, then
   `definitionLocation`/`hoverText`, as before U4.
3. **Use-capability method.** If the located node is a `MethodCallExpression`
   with no `mResolvedMethod`, use the use-capability member lookup (research
   R3).
4. **Interface mapping.** If the target declaration's file is an interface
   input, map it to the source declaration (research R2). An unmapped target
   answers `null`.
5. **Same-module targets.** A target in the viewing module keeps the pre-U4
   output byte for byte, for both definition and hover.

## Guarantees

- **G1**: No result URI ends in `.bmod`, and no result has an empty file
  (FR-002, C6).
- **G2**: A URI is the `file://` form of the path the compile read: the
  dependency source, `stdlibPath`/…, or a sibling file. The target file need
  not be open (FR-004). When the file is open, the location comes from the
  buffer that compile used (FR-005).
- **G3**: Requests never spawn, fetch, recompile or write; they read the
  current `CompileResult` (FR-009).
- **G4**: A failed or unresolved dependency answers `null` and produces no
  output on stdout or stderr (FR-010).

## Hover renderings (FR-008), pinned by goldens

| Case | Fixture document | Expected hover line |
|------|------------------|---------------------|
| qualified dep fn | `test_build/myapp/main.b` `mathlib.add` | `fn mathlib.add(int a, int b) -> int` |
| aliased | `test_build/aliasapp/main.b` `m.add` | `fn m.add(int a, int b) -> int` |
| name-cap type (type position and construction) | `test_build/counterapp/main.b` `Counter` | `struct counterlib.Counter` |
| use-cap method | `test_build/usebox/main.b` `b.get()` | `fn boxq.Box.get(self) -> T` |
| foreign type inside a signature | `test_build/usebox/main.b` `midx.get_box` | `fn midx.get_box(int n) -> boxq.Box<int>` |
| stdlib fn | stdlib fixture, `net.http_ok` | `fn net.http_ok(string body) -> net.HttpResponse` |
| stdlib type | stdlib fixture, `net.HttpServer` in type position | `struct net.HttpServer` |
| prelude type | `test_files/codegen_prelude_no_import.b` `Map` | `struct Map<K, V>` |
| sibling | `test_files/lsp/projects/multifile/main.b` `helpers.triple` | `fn helpers.triple(int x) -> int` |

These are exact expected strings. A mismatch found during implementation
is an implementation bug, or a spec amendment that must be recorded. It is
never a silent golden regeneration.

## Golden inventory (FR-011)

There is one definition golden per case: `xmod_defn_{qualified,alias,namecap,usecap,stdlib_fn,stdlib_type,prelude,sibling}`.
There is one hover golden per case: `xmod_hover_{…same eight…}`.
`xmod_defn_not_open` covers FR-004; it requests a definition in a target file
that was never opened, and the expected result is its on-disk URI.
`xmod_defn_unsaved` covers FR-005 and the R2 edge case; it opens the
dependency file, inserts lines above the declaration, and expects the shifted
location.
`xmod_defn_failed_dep` covers FR-010; the referenced dependency's compile
failed, and the expected result is `null`.
Grouping several requests into one fixture is allowed, provided every case
remains its own pinned request.

## Shared-frontend API additions (behavior-neutral for qcc)

- `CompilePlan::recordTypeReferences` (default `false`), and
  `ModuleRecord::typeReferences`.
- `CompileContext::useCapabilityTypes(structs, enums)`: `qcc.cpp` calls this in
  place of its inline loop.
- `StructDefinition::findMethod(name)` and Sema's receiver-type helper, now
  shared by Sema and blangd.

Neutrality is proven by unchanged `run_tests.sh`, `test_codegen.sh` goldens and
`test_build`, plus an IR/`.bmod` byte-identity spot check against the pre-unit
`qcc`.
