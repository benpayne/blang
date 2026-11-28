# Data Model: Shared Project-Compile Entry Point (U1)

These are in-memory compiler structures; nothing is persisted. Final API names are settled here, and the design audit may amend them.

## CompileInput

One compile input.

| Field | Type | Meaning / rule |
|---|---|---|
| `path` | `std::string` | The path as the driver received it. It is used verbatim in diagnostics, in the module name (`moduleNameOf`), and for qcc's IR file name. It is never normalized. |
| `kind` | `Kind { kSource, kInterface }` | `kInterface` iff `path` ends in `.bmod` (`classifyInput`, the one shared copy of qcc's suffix test). |
| `inMemory` | `bool` | `false`: read from disk with today's readers. `true`: lex and scan `text`. |
| `text` | `std::string` | Only meaningful when `inMemory`. |
| `origin` | `std::string` | An explicit canonical origin. For a `kInterface` input it is qcc's `--bmod-origin` value for this exact path, and may be empty. It is ignored for `kSource`, which uses the plan's `moduleOrigin`. |

## CompilePlan

An ordered, explicit description of one compile. It holds no parse state.

| Field | Type | Meaning / rule |
|---|---|---|
| `inputs` | `std::vector<CompileInput>` | In command-line order. `run` applies the stable interfaces-first reorder (research R3.1). |
| `combine` | `bool` | qcc `--combine`. |
| `moduleOrigin` | `std::string` | qcc `--module-origin`. Applies to every `kSource` input. |
| `identity` | `IdentityPolicy { kCanonical, kNone }` | `kCanonical`: today's origin selection plus digest stamping, with a `realpath` failure being a located error. `kNone`: skip origin/digest entirely (blangd U1, research R4). |
| `lexerTrace` | `bool` | The per-token lexer trace (qcc: `verbose && !dumpLocations`). The process-wide parse trace stays a driver call (`setParseTraceEnabled`). |

Validation: none at plan construction. All input errors surface during `run` as today's located diagnostics. An empty `inputs` vector is a driver usage error that qcc rejects before building a plan, as today. If `run` receives one, it returns an empty, error-free context.

## ModuleRole

| Value | When |
|---|---|
| `kUser` | Combine mode, the last input; parses into the user (combine) scope. |
| `kPreludeProvider` | Combine mode, module name `buffer` or `collections`; parses into a prefix-exempt holding namespace and is PROMOTEd. |
| `kNamespaced` | Combine mode, any other non-last source input; parses into its own namespace registered on the user scope. |
| `kInterface` | A `.bmod` input (either mode); parses into its own module scope, extern, injected into its dependency namespace. |
| `kStandalone` | Non-combine source input; parses into its own module scope. |

## ModuleRecord

One record per input that reached the parse step, in processing order.

| Field | Type | Meaning |
|---|---|---|
| `path` | `std::string` | The input path. |
| `moduleName` | `std::string` | `moduleNameOf(path)`. |
| `role` | `ModuleRole` | See above. |
| `scope` | `Scope *` (non-owning; the context owns it, see Ownership) | The scope the input was routed to. It is **set at routing time** and is non-null whenever a record exists, so blangd always has its file scope (today's `CompileResult.fileScope` is non-null even on a catastrophic parse failure). |
| `recorded` | `bool` | **Set only at the recorded step (audit F1)**, i.e. after the parse, the origin/digest stamping and, for an interface, the format check and injection. That is the exact point where today's code pushes onto `modules` (qcc.cpp L737). It stays `false` for a dropped input: a malformed foreign-type header, the post-pre-scan `hadError` skip, a catastrophic parse, a `realpath` failure (L640–656) or a wrong-format `.bmod` (L670–700). |
| `module` | `SmartPtr<Module>` | **Assigned only at the recorded step**, together with `recorded = true`. Null for every dropped input, *including one that parsed successfully and was dropped later*. That module's AST is released at the end of its iteration, as today's local `mod` is. |

**The recorded-only view (F1).** `moduleIndices()` lists the indices of records with `recorded == true`, in order. It is exactly qcc's `modules` vector today, parallel to `moduleScopes` and `moduleNames`. **Every** downstream consumer reads only this view, never `records()` directly:
- Sema;
- eager PROMOTE;
- the import-diagnostics pass;
- the prelude-shadow set;
- `bmodEmitScope`;
- qcc's dump-locations, emit-bmod, emit-schema and codegen stages.

So an error path can never add diagnostics. `records()` is for callers that need the routed scope of a possibly-dropped input (blangd `record(0).scope`) and for tests.

## CompileContext

The result of `ProjectCompile::run`. It is move-only (`std::unique_ptr` from `run`; blangd wraps it in a `shared_ptr`).

| Accessor | Returns | Consumer |
|---|---|---|
| `records()` / `record(i)` | `const std::vector<ModuleRecord>&` | blangd (`record(0)`), tests |
| `moduleIndices()` | indices of **recorded** records, in order (the recorded-only view, F1) | qcc dump/emit/codegen; all internal post-loop passes |
| `hadError()` | `bool`: today's `hadError`. It does **not** include `diag.hasErrors()`. Callers test `ctx->hadError() \|\| diag.hasErrors()`, as qcc's error exit does today (F6). | qcc exit; ctest C1/C7 |
| `userScope()` / `preludeScope()` | `Scope *`, null outside combine mode | tests; U2+ |
| `bmodEmitScope()` | `Scope *`: the scope of the last **recorded** source module, else the global scope | qcc `--emit-bmod` |
| `isPreludeProvider(i)`, `codegenPrefix(i)` | **Name-keyed, exactly as today** (not role-keyed). `codegenPrefix(i)` = `moduleName(i)` iff that name is in the set of namespace-registered names (`moduleNamespaces`) and not in the prefix-exempt set; else `""`. `isPreludeProvider(i)` = `moduleName(i)` ∈ the prelude-provider name set. The shadow filter's "routed to the user scope" test is likewise name-keyed and verbatim: `!preludeModuleNames.count(mn) && !moduleNamespaces.count(mn)`, over recorded modules. It therefore also admits `.bmod` interface modules and the combine user file (F6). This matters for neutrality when a user or interface file shares a basename with a stdlib module (e.g. a user `net.b`): today's name lookup would prefix it too, and a role-based rule would not. | qcc LLVM codegen |
| `suppressedPrelude()` | `const std::set<std::string>&` (research R10) | qcc `allStructs` filter; test C4 |

## Ownership (KG-7)

```text
Resolver (caller-owned; outlives the context)
 └─ global Scope  ── owns ──▶ dependency namespaces created by .bmod injection (setParent(gScope))   [A3: U3 item]
CompileContext
 ├─ mOwnedModuleScopes : vector<SmartPtr<Scope>>   ◀── every resolver.newModuleScope() result
 │    ├─ preludeScope ─(child, unowned link)─ combineScope ── owns (mChildren) ──▶ namespace scopes
 │    │                                                         (prelude holding + stdlib namespaces; setParent)
 │    └─ per-input module scopes (kInterface / kStandalone)
 └─ mRecords[i].module : SmartPtr<Module>          ◀── each parsed AST
```

- **Destruction order.** The caller destroys the context before the `Resolver`:
  - **qcc**: `Resolver resolver;` is declared before `auto ctx`, so destruction runs in reverse declaration order;
  - **blangd**: the `CompileResult` is released before the process-static `Resolver`;
  - **ctest**: a nested block per case.
- **What is not owned.** Nothing the context creates is adopted by the global scope, **except** the dependency namespaces from `.bmod` injection. That exception is preserved for neutrality (A3) and is inert in U1's blangd, which has no interface inputs.
- **What `run` restores.** On return, `run` restores `gDiag` to its value on entry.
- **The `gScope` alias (F2).** The **caller** installs it: qcc via `Resolver`'s constructor, and blangd via `gScope = sResolver.globalScope()` on every `compileDocument` call, kept verbatim. `run` asserts `gScope == resolver.globalScope()` and never writes it.

## State transitions

`run` is a single pass. Per input, in order:

```text
routed (record.scope set) ──▶ (interface pre-scan: fail → DROP)
       ──▶ parsed (fail → DROP)
       ──▶ stamped (realpath fail → DROP)
       ──▶ (interface: format check fail → DROP; else extern/from-interface + inject)
       ──▶ RECORDED (record.module set, recorded = true, appended to moduleIndices)
       ──▶ analyzed (source)
       ──▶ promoted (prelude provider)
```

After the loop: the import-diagnostics pass (gated, recorded only), then the shadow-set computation (recorded only). Every DROP sets `hadError`, leaves `recorded == false` and `module == null`, and emits only the one diagnostic it emits today. This matches research R3.
