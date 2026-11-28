# Research: Shared Project-Compile Entry Point (U1)

Phase 0 of [plan.md](plan.md). No `NEEDS CLARIFICATION` remained in Technical Context. Each item below records a design decision taken from recon of the base (`epic/modules-v2-lsp` @ `2f68efe`, which contains PR #157 / `efb099a`).

## R1. Extraction technique: lift verbatim, never rewrite

- **Decision**: Move `qcc.cpp`'s parse loop (L248–L824) into `ProjectCompile::run` **textually**, preserving statement order. Driver locals become `CompileContext` fields, and CLI flags become `CompilePlan` fields. There is no "cleanup while moving". Readability improvements (e.g. de-duplicating the three basename-stripping blocks into `moduleNameOf`) are allowed only where the replaced code computes the identical string for every input.
- **Rationale**: Behavior neutrality is the unit's hard constraint, and the loop has many order-sensitive details: incremental `.bmod` injection, eager PROMOTE after Sema, per-file `continue`s, and `hadError` accumulation. A verbatim lift makes the review a diff-by-inspection.
- **Alternatives rejected**:
  - *Redesign the routing as a role table first.* That changes control flow and invites subtle reordering.
  - *Keep the loop in qcc and expose only helpers.* That leaves the routing decision in the driver, so blangd would have to reimplement it, which violates REQ-001.

## R2. Where the code lives and how `bcc` links the stdlib rule

- **Decision**:
  - `ProjectCompile.{h,cpp}` at the root, in `BLANG_FRONTEND_SOURCES`;
  - `StdlibSelect.{h,cpp}` at the root, in `BLANG_FRONTEND_SOURCES` **and** in the `bcc` target's source list;
  - `StdlibSelect` depends only on the standard library and `<unistd.h>`.
- **Rationale**: `bcc` is built from `bcc.cpp ProjectConfig.cpp BuildCache.cpp sha256.c` (CMakeLists.txt L324) and does not link the parser. Linking the stdlib rule into `bcc` must not drag in the frontend. The root location matches `Resolver.*`/`Sema.*`.
- **Alternatives rejected**:
  - *A static library `blang_stdlibselect`.* Workable, but more CMake surface for a two-function unit. A plain source in two lists is the existing pattern (`sha256.c` is likewise shared).
  - *Putting it in `ProjectConfig.cpp`.* That file is a toml parser and the concern does not belong there.

## R3. Order-sensitive details the move must preserve (the neutrality checklist)

Each item becomes a review checkbox in tasks.md.

1. Stable `.bmod`-first reorder of inputs (L379–L389), applied **inside** `run`, so every plan builder gets it.
2. The prelude and combine scopes are created only in combine mode, from `resolver.newModuleScope`, and are adopted by the context.
3. Module-name derivation is the basename with everything from the last `.` removed (three identical blocks: L462, L719, L746).
4. The user file is the **last input in the reordered list** (`fileIdx == inputs.size()-1`), not "the last `.b`". The two are the same because interfaces sort first. Keep the index rule.
5. Prelude providers are exactly `buffer` / `collections`, by module name.
6. The foreign-type pre-scan runs **before** lexing. A malformed line produces an error **and `continue`s the line loop**. After the scan, `if (hadError) continue;` skips the file. Note that this tests the *global* `hadError`, so an earlier file's error also skips a later `.bmod`'s parse. That is quirky, but it is today's behavior and must be preserved.
7. A parse failure sets `hadError` and `continue`s. The module is not recorded, so `modules`, `moduleScopes` and `moduleNames` stay aligned with each other. They are **not** aligned with the inputs.
8. `stampDefiningOrigin` runs before origin selection.
9. Origin selection: an interface input uses the `bmodOrigins[path]` exact-string lookup; a source input uses `moduleOrigin`; otherwise `realpath(path)`. A `realpath` failure produces a located error at 1:1 and `continue` (the module is not recorded).
10. For interfaces, the format probe reads at most 8 lines and a missing marker means version 1. A mismatch produces the located error and `continue`. Then `setExtern`, `setFromInterface` on every struct, `bmodMap[name]`, and `injectBmodSymbols(mod, name)`.
11. Record the module, its scope and its name. `bmodEmitScope` is updated for each source input.
12. `PARSE_TRACE("Completed parse")`, then `Sema::analyze` (source inputs only), then eager PROMOTE (combine mode only, prelude-provider modules only, prelude-manifest names only, into `preludeScope`, via `addSymbol` + `addType(new Type)`).
13. After the loop, the import-diagnostics pass is gated on `combine || !bmodFiles.empty()`. It iterates modules in order and skips extern/null ones. Within a module it walks imports in order. It emits an unknown-module **error** (`findNamespace(realName)`) or else an unused-import **warning** (`wasModuleUsed(localQ)`, slug `unused-import`), with the exact message text.
13a. **Recorded vs. dropped (audit F1).** A module can parse successfully and still be dropped by a `realpath` failure (L640–656) or a wrong-format `.bmod` (L670–700), before it reaches `modules.push_back` (L737). The extraction keeps that distinction explicit: `ModuleRecord::recorded` / `module` are set only at the L737-equivalent step, and every post-loop pass reads only recorded modules. Locked by ctest C7.
14. `bmodMap` is write-only today. It stays as a context field only if something reads it. It is dropped if not, which is neutral because nothing reads it.

## R4. blangd's single-file behavior and `IdentityPolicy::kNone`

- **Finding**: `lsp/Compile.cpp` today does:
  - `newModuleScope()`;
  - `Module::Parse(StringLexerReader(text, path))`;
  - then, **if non-null**, `stampDefiningOrigin` and `Sema::analyze`.

  It never stamps a digest and never calls `realpath`. Running it through qcc's non-combine path verbatim would add `realpath(path)`. That fails for unsaved/untitled documents and would emit a new "cannot resolve a canonical origin" error, which is a behavior change.
- **Decision**: `CompilePlan::identity = IdentityPolicy::kNone` skips the entire origin/digest block. qcc always uses `kCanonical`.
- **Rationale**: This makes the difference explicit and searchable, instead of overloading "empty origin" (which in qcc *means* "use realpath"). It is diagnostic-neutral by construction: the digest feeds only generic mangling (`mangleGenericName`, codegen), never Sema.
- **Alternatives rejected**:
  - *blangd passes `origin = path`.* That stamps a digest blangd never had. It is harmless for diagnostics today, but it changes AST state that a future Sema rule could read, which is the M-3 drift hazard.
  - *Have `run` detect in-memory inputs and skip.* That is an implicit policy; the explicit enum is clearer.

## R5. Stdlib path-string identity

- **Finding**: `qcc` diagnostics and `bcc -v` print stdlib paths verbatim (e.g. `build/stdlib/net.b:12:3: …`), so the path string is observable.
- **Decision**: `selectStdlibFiles( stdlibDir, … )` builds `stdlibDir + "/" + name + ".b"`, and `bcc` passes `exeDir + "/stdlib"`. The concatenation is identical to today's `exeDir + "/stdlib/" + name + ".b"`. No normalization (no `realpath`, no trailing-slash trimming) is applied.

## R6. In-memory vs. disk reading

- **Decision**:
  - **Disk inputs** use today's readers unchanged (two `ifstream` line scans plus `LexerReader( path )`).
  - **In-memory inputs** use `StringLexerReader` and run the same pre-scan/format-probe logic over the text. The line loop is factored into a helper taking a `std::istream&`, which is an `ifstream` for disk and an `istringstream` for memory.

  U1 exercises in-memory only for one source input (blangd), so the in-memory interface path exists but no shipped caller uses it until U3.
- **Rationale**: `StringLexerReader` claims to mirror `LexerReader` exactly, but the disk path is what every golden was produced through, so it is not swapped.
- **Risk / stop rule**: if the `istream` refactor changes any disk-input diagnostic (it should not: the same `getline` loop), stop and raise it.

## R7. `StringLexerReader` location

- **Decision**: Move `lsp/StringLexerReader.h` to the root as `StringLexerReader.h` in `namespace QLang`. `lsp/StringLexerReader.h` becomes `#include "../StringLexerReader.h"` plus `namespace lsp { using StringLexerReader = QLang::StringLexerReader; }`.
- **Rationale**: The frontend must not include `lsp/` (a layering inversion). The alias keeps `ParserLeakTest.cpp`, `ResolverReuseTest.cpp` and other existing includes compiling without edits, so FR-020's "unmodified" holds.
- **Alternatives rejected**: duplicating the class, which is exactly the drift this epic exists to remove.

## R8. `project_compile` oracle and fixtures

- **Decision**: C1's expected text is the live stderr of the built `qcc --parse-only --combine` on the same paths, via `popen`, with the binary path passed by CTest (`$<TARGET_FILE:qcc>`). Fixtures are new files under `test_files/project_compile/`.
- **Rationale**: The done-condition says "equals `qcc --combine` on the same inputs". A live oracle cannot drift from qcc the way a committed expected string can.
  - The ctest **does** spawn a process. That is fine: the no-subprocess rule (C4) is a blangd invariant, and the ctest is neither blangd nor the system under test's runtime. The SUT, `ProjectCompile::run`, runs in-process.
  - Real files are needed because the rendered diagnostic paths must match.
  - The directory is globbed by no existing runner: `run_tests.sh` globs only `pass/`, `fail/`, `cgfail/`, `xfail/` and `golden/`; `test_codegen.sh` only `test_files/codegen_*.b` at maxdepth 1; `test_lsp.sh` only `test_files/lsp/` and `fail/sema/`. So no existing suite's count or result changes.
- **Alternatives rejected**:
  - *Committed `.expected` text.* It is static, could drift, and adds a new golden that must be regenerated whenever stdlib line numbers move.
  - *An in-process "qcc main" call.* qcc's `main` is not a library function, and making it one is out of scope.

## R9. `CompileResult` ownership in blangd

- **Decision**: Add `std::shared_ptr<QLang::CompileContext> context` to `lsp::CompileResult`, and keep `module` / `fileScope` as `SmartPtr`s copied from `record(0)`.
- **Rationale**: `Server` keeps `std::map<std::string, CompileResult>` and assigns by value (`Server.cpp:280`), so the type must stay copyable, and `unique_ptr` would break it. Keeping the two existing fields leaves `Server.cpp` and `AstLocator` untouched. Releasing the result releases the context, then the scopes, then the AST. All of that happens before the static `Resolver`'s global scope (process exit), which is today's KG-7 ordering.

## R10. Prelude-shadow set and module prefix as context data

- **Decision**: `run` computes `suppressedPrelude` exactly as today's L918–L935 does (F6):
  - over the **recorded** modules, take each module `mn` for which `!preludeModuleNames.count(mn) && !moduleNamespaces.count(mn)` holds. That is a name-keyed test, so it admits `.bmod` interface modules and the combine user file, not just "the user file";
  - collect those modules' struct names;
  - intersect them with the prelude manifest. It also exposes per-record `role` and `codegenPrefix()`. The LLVM block in `qcc.cpp` consumes these. `codegenPrefix` and the shadow filter's "routed to the user scope" test stay **name-keyed** (they look up `moduleNamespaces` / `preludeModuleNames` by basename, as today), not role-keyed. A role-keyed rule would differ when two inputs share a basename, e.g. a user file named `net.b`.
- **Rationale**: These are non-LLVM routing facts. Leaving them in qcc would keep a second copy of "what is a namespaced module", which is the SC-002 "one implementation" rule.
- **Neutrality note**: today the shadow set is computed only on the LLVM path, after the error exit. Computing it earlier, unconditionally, is side-effect free: it reads struct lists and writes a set. Codegen consumes the identical set.
