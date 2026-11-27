# Data model: U4 cross-module navigation

Every entity below is **per compile**. It is built during `compileDocument`,
owned by that document's `CompileResult` (directly, or through its
`CompileContext`), and destroyed with it (KG-7 / C3). Nothing persists across
reparses.

## TypeReference (shared frontend, `ProjectCompile.h`)

One type name written in type position, recorded by the parser where it
resolved the token. Recording happens only when
`CompilePlan::recordTypeReferences` is set.

| Field | Type | Notes |
|-------|------|-------|
| `location` | `SourceLocation` | The type-name token. For a qualified `net.HttpServer`, this is the `HttpServer` token. |
| `length` | `uint32_t` | Length of the name token in bytes; the span is `[col, col+length)`. |
| `target` | `const Symbol *` (non-owning) | A `StructDefinition`, `EnumDefinition` or `ProtocolDefinition`. It is never null; a reference with no definition is not recorded. |

Records are stored on `ModuleRecord::typeReferences` (a `std::vector`, in
parse order). Validation: only symbols that downcast to the three kinds are
recorded. Generic parameters and core types are never recorded.

## CompilePlan::recordTypeReferences (shared frontend)

This is a new `bool` that defaults to `false`. `qcc` never sets it, so its
behavior cannot change. blangd sets it for the document compile. Dependency
interface-derivation runs leave it off, because nobody navigates their type
positions.

## CompileContext::useCapabilityTypes (shared frontend)

`void useCapabilityTypes(std::vector<SmartPtr<StructDefinition>> &structs,
std::vector<SmartPtr<EnumDefinition>> &enums) const` is the collection loop
lifted verbatim from `qcc.cpp`'s `allStructs`/`allEnums`:

- it covers every recorded module, in record order;
- it filters out the prelude-provider structs named in `suppressedPrelude()`.

`qcc` calls it in place of its inline loop. blangd calls it for R3/R5 lookups.

## Member lookup helper (shared frontend)

`FunctionDefinition *StructDefinition::findMethod(const std::string &name)`
is the method-by-name loop Sema runs in its method-call resolution
(Sema.cpp:1716), factored so that Sema and blangd share one copy. The
receiver-type derivation Sema uses for a `MethodCallExpression` is exposed
the same way, as a static helper.

## InterfaceSourceMap (blangd, `lsp/`)

| Field | Type | Notes |
|-------|------|-------|
| `byInterfacePath` | `std::map<std::string, std::shared_ptr<Run>>` | The key is the `CompileInput::path` of each closure interface (`<depDir>/<name>.bmod`). The value is that dependency's kept source compile, shared with `CompileResult::deps`. |

This is a member of `CompileResult`, declared after `deps`. A dependency whose
source compile failed has no entry.

Operation: `const Symbol *toSource(const Symbol *interfaceDecl)`. It returns
the unique source declaration of the same kind, name and owning type, or
`nullptr` (research R2).

## NavigationTarget (blangd, transient per request)

| Field | Notes |
|-------|-------|
| `decl` | The resolved declaration, mapped to source when it came from an interface (null means the answer is null). |
| `definingRecord` | The `ModuleRecord` of the pre-mapping declaration's file. It drives the display name. |
| `isForeign` | `definingRecord` is not the viewing record. Same-module targets use the pre-U4 path unchanged. |

## DisplayName (FR-008; design.md D4 + addendum)

`render(typeOrDecl, viewingRecord)` produces a deterministic string:

| Case | Rendering |
|------|-----------|
| core type, generic parameter, or prelude-provider record | bare |
| bound by the viewer's imports | `<smallest local qualifier>.<name>` |
| unbound | `<definingRecord.moduleName>.<name>` |
| member | `<owner display>.<member>` |

There are no state transitions: every entity is rebuilt on each reparse.
