# Design: modules-v2-lsp

**Epic**: [overview.md](overview.md)

Product/architecture level. Detailed engineering design belongs to each unit's
speckit `plan.md`, reviewed in the design audit (constitution Principle VI).

## Context

Plan-time recon on `master` @ `7a68fcd` (plus PR #157, the launch base).

**blangd today** (`lsp/`, ~2.2k lines):
- `blangd_main.cpp` → `Server` (`Server.cpp`, JSON-RPC dispatch over
  `Transport` Content-Length framing; `Json.cpp` is an insertion-ordered JSON).
  Handled: `initialize`, `initialized`, `textDocument/didOpen|didChange|didClose`
  (full-sync, `DocumentStore.h` holds open texts), `publishDiagnostics`,
  `textDocument/definition`, `textDocument/hover`, `textDocument/documentSymbol`.
- `Compile.cpp::compileDocument(path, text)` is the whole pipeline:
  - a process-lifetime `static Resolver sResolver` owns the global builtin
    scope;
  - each call makes a fresh `DiagnosticEngine` and one module scope
    (`sResolver.newModuleScope()`), lexes from a `StringLexerReader`, and
    runs `Module::Parse`, then `stampDefiningOrigin`, then `Sema::analyze`;
  - it returns a `CompileResult { module, fileScope, diagnostics }`, so a
    document compiles **alone**.
- `AstLocator` maps a 1-based cursor to the innermost AST node (no end positions —
  "greatest start column at or left of the cursor" approximation, pinned by
  goldens). It produces a definition `SourceLocation` from resolved pointers
  (variables, calls, constructors, enum variants, fn refs, indirect calls,
  Sema-stamped field/method), and a one-line hover string. Today the server
  answers only with locations in the **same document**.
- Tests: `test_lsp.sh` drives `tools/lsp_client.py` over
  `test_files/lsp/*.lsp.jsonl` and compares each transcript with its
  `*.expected.out` (strict framing, `${ROOT}` scrubbing, `--update-goldens`,
  `--selfcheck`). The same script re-verifies every `fail/sema` fixture through
  `publishDiagnostics`. `ci/nvim_smoke.lua` + `test_lsp_nvim.sh` are the
  real-client check. The `ResolverReuseTest` and `parser_no_leak` ctests build
  the resolver the way `lsp/Compile.cpp` does. Baseline: 70/0 transcripts.

**The compile context blangd lacks** lives in two drivers:
- **`qcc.cpp` `main()`** does the combine-mode routing inline:
  - `Resolver resolver` plus driver-owned module scopes
    (`ownedModuleScopes`, KG-7);
  - in `--combine`, a `preludeScope` with `combineScope` as its child; the
    **last** input file is the user file (`fileScope = combineScope`);
  - `buffer`/`collections` LOAD into a prefix-exempt holding namespace and
    are PROMOTEd (only the prelude types `Map`/`Set`/`Buffer`) into the
    prelude scope; every other stdlib file becomes a namespaced module scope
    (`combineScope->addNamespace`);
  - `.bmod` inputs are format-checked and injected into a per-module
    namespace on `gScope` flagged `grantsNameCapability` (D7);
  - each module is stamped with `stampDefiningOrigin` (basename + canonical
    path, KG-9) and `stampModuleDigest` (`--module-origin` /
    `--bmod-origin`, else `realpath(file)`);
  - after the parse loop comes the whole-program import-diagnostics pass
    (unknown-module / unused-import / collision, gated on an
    "authoritative" module set), then `Sema::analyze` per module.
- **`bcc.cpp`** decides *what* the inputs are:
  - `resolveStdlibFiles(exeDir, imports)`: an unconditional
    `sys, buffer, collections, fs, net`, then import-gated
    `timer, math, time, random, env, cli`, then any other imported name with a
    `stdlib/<name>.b`;
  - `discoverSourceFiles(projectDir)`;
  - `buildProject` recursion: deps in topological order, a deduped transitive
    `DepArtifact` closure, each dep's origin = `realpath(depDir)`; git deps are
    fetched into `~/.cache/blang/git/<name>-<hash(url@pin)>` by
    `resolveGitDependency` (the only subprocess/network path);
  - the final `qcc --combine <stdlib...> <sources...> <dep .bmods...>
    --module-origin … --bmod-origin …` invocation.
- **Dependency interfaces** today exist only as `.bmod` text produced by
  `BmodEmitter::emit` (format 5). It carries `pub` declarations only, opaque
  struct bodies, `impl` blocks of bodyless `pub` method/`init` signatures, full
  generic bodies, D15 table/`@json` field metadata, protocol-conformance
  records and `// foreign-type:` headers. That filtering *is* the export model
  (D9, D15–D17, P9). The editor must see exactly that interface or it will
  disagree with `bcc` on privacy and capability.

**Measured cost**: `qcc --parse-only --combine` over the five base stdlib files +
`examples/kv/main.b` takes ~0.09 s wall, 22 MB peak (whole process, 16-core
dev box). A full context recompile per edit fits a 500 ms budget with wide
margin at today's stdlib size (1,625 lines).

## Architecture

```text
              ┌──────────────── shared frontend (no LLVM) ────────────────┐
              │  CompilePlan  ── ordered inputs: {path|in-memory text,     │
              │                  role: stdlib|prelude|project|dep-iface,   │
              │                  origin, import-name/alias}                │
 bcc  ──────▶ │  selectStdlibFiles(stdlibDir, imports)   (from bcc)        │
 qcc  ──────▶ │  ProjectCompile::run(plan) ─▶ Resolver (owned scopes)       │
 blangd ────▶ │      routing · .bmod/iface inject · stamping ·             │
              │      import-diagnostics pass · Sema                        │
              │  ──▶ CompileContext { modules, scopes, diagnostics }        │
              └────────────────────────────────────────────────────────────┘
 blangd only:  ProjectLocator (blang.toml walk-up, sources, dep graph from
               source, no subprocess) ─▶ builds the CompilePlan;
               DocumentStore overlay supplies unsaved text for any input path;
               AstLocator/Server map SourceLocation.file ─▶ file:// URI.
```

- **U1** creates `CompilePlan` / `ProjectCompile` / `selectStdlibFiles`
  (names indicative; the spec settles them) and turns `qcc` into a caller.
  Building a plan from a `blang.toml` stays in `bcc` for building; blangd gets
  its own read-only `ProjectLocator` in U3.
- **U2** has blangd build a single-file plan: the stdlib selection plus the
  document as the user input.
- **U3** has blangd build a project plan: project sources plus dependency
  interfaces derived from source (D2), in `bcc`'s topological order with
  `bcc`'s origins.
- **U4** makes navigation context-aware. `SourceLocation.file` already records
  the defining file, so cross-file definition is mostly the server returning
  that file's URI instead of assuming the open document.

## Interfaces & contracts

- **C1 — One compile, two front ends.** For any input set, the diagnostics `qcc`
  reports and the diagnostics blangd publishes for the user document are
  identical: same file, line, column, severity and message. This is the
  contract the corpus harness enforces (done conditions 2–3).
- **C2 — `qcc`/`bcc` observable behavior is frozen.** Exit codes, diagnostic
  text, emitted IR/`.bmod`, goldens. U1 is a pure extraction.
- **C3 — Ownership.** Every module, scope and AST built for a document's context
  is owned by that document's `CompileResult` (or a cache structure it owns) and
  released before the process `Resolver` is destroyed. Nothing is adopted by the
  global builtin scope. `parser_no_leak` is the regression lock.
- **C4 — No subprocess, no network in blangd.** The project locator only reads
  files. An unfetched git dep is a diagnostic, never a fetch.
- **C5 — Diagnostics are about the open document.** Errors inside stdlib or
  dependency sources are not published against the user's document. An
  unresolvable dependency is reported once, located at the importing
  `import` statement, or at the `blang.toml` line when there is no import
  site.
- **C6 — Definitions point at source.** A definition result is always a real
  `.b` source location (dependency source, stdlib file, sibling file), never a
  generated `.bmod`, and never a location with no file.
- **C7 — Transcript determinism.** Every new behavior is pinned by a
  `test_files/lsp/` golden that `test_lsp.sh` runs. `--selfcheck` continues to
  prove the harness goes red on a corrupted golden.

## Key decisions

| # | Decision | Rationale | Alternatives rejected |
|---|----------|-----------|----------------------|
| D1 | Extract the combine routing into shared frontend code that **both** `qcc` and blangd call (U1), rather than teaching blangd to replicate it | The editor and compiler can only agree if they run the same code (C1). Epic B's `Resolver` was built as exactly this seam | blangd shelling out to `qcc` (subprocess, violates C4, slow, loses the AST for navigation); a blangd-only reimplementation (drifts) |
| D2 | **Dependencies from source (owner decision)**, with **interface equivalence**: the recommended mechanism is to parse the dependency's sources, run `BmodEmitter::emit` **in memory**, and parse that text as the dependency's interface. The dependency's source AST is kept alongside for navigation (C6) | Reusing the emitter guarantees that what blangd resolves against is byte-for-byte the interface `bcc` would produce: privacy, D7, D15 metadata, P9 filtering, foreign-type headers. No second export-model implementation can drift | Resolving against the full dep source AST (would expose private methods/fields that `bcc` hides → editor/compiler disagreement); reading built `.bmod` from the cache (declined by the owner: needs a prior build, and lands definitions in generated files). The U3 architect may propose another mechanism only if it gives the same guarantee, raised as a question first |
| D3 | Stdlib discovery precedence: `initializationOptions.stdlibPath` > `BLANG_STDLIB_DIR` > `<blangd-dir>/stdlib`; when none is found, one diagnostic plus single-file fallback | Explicit config beats environment beats install layout; the build tree already copies `stdlib/*.b` to `build/stdlib/` next to `build/blangd` | A share-dir fallback (open question 2, deferred); hardcoding the repo path |
| D4 | **D3 display-name rule (proposed; U4 confirms):** a foreign type/function in hover or diagnostic text renders as `<q>.<name>`, where `<q>` is the qualifier the **viewing module** binds the defining module to: its import name, or its alias after `import x as y`. Prelude and core types render bare (`Map`, `int`). A type the viewer holds only via use-capability (no binding) renders with the **defining module's human name** (`getDefiningFile()` basename, as `.bmod` foreign-type headers print it). Ties (one module bound under two qualifiers in the same viewer) are broken by the lexicographically smallest qualifier | It is deterministic from data every diagnostic site already has. It renders what the user could actually type where a type is nameable, and never invents a composed `A.B.name` form in source position (D3 permits composed forms only as display, and this rule needs none) | Shortest import route (needs graph search; unstable under edits); always the defining module (shows names the user can't write after aliasing) |
| D5 | A full context recompile per `didChange`; caching only after a measured REQ-009 miss and an explicit ownership design | Measured ~0.09 s, and caching across reparses is the KG-7 bug class | Incremental reparsing (non-goal); an always-on stdlib cache in v1 |
| D6 | The project locator mirrors `bcc`'s rules exactly: `blang.toml` walk-up, `discoverSourceFiles`, dep paths relative to the declaring `blang.toml`, git checkout path `~/.cache/blang/git/<name>-<hash(url@pin)>`, origin = `realpath(depDir)` | Identity and mangling digests must match `bcc` or capability and collision diagnostics diverge. Where possible, share the helpers rather than copy them | Workspace-root-relative resolution (would disagree with `bcc` for nested projects) |

### D4 addendum — settled for U4 (manager ruling, 2026-11-26)

The U4 architect **confirmed** D4 for types and free functions and **amended** it
for members and for type names inside a signature. The manager accepted the
amendment, recording that D4 belongs to this epic's design document and is not
part of the frozen modules-v2 record. Done-condition 5 needs hover on the
use-capability method case, and the original text did not cover it.
`specs/044-lsp-xmodule-navigation/spec.md` FR-008 is the normative statement.
In summary:

- **Bound module**: the hover shows `<q>.<name>`, where `<q>` is the viewer's
  import name or alias for the defining module. This applies to dependencies,
  stdlib modules and sibling project sources alike.
- **Prelude, core and generic parameters**: shown bare.
- **Unbound (use-capability only)**: the hover shows `<m>.<name>`, where `<m>`
  is the defining module's human name (the module name a `.bmod`
  `// foreign-type:` header prints).
- **Ties**: when one module is bound under two qualifiers, the
  lexicographically smallest qualifier wins.
- **Members (amendment)**: a method or `init` is shown as
  `fn <owner>.<member>(...)`, where `<owner>` is the owning type's display name.
  Example: `fn boxq.Box.get(self) -> T`.
- **Signature (amendment)**: the declared signature is shown, and every foreign
  type named inside it follows the same rule, relative to the viewing module.
- **Scope**: the rule governs hover text only. It does not change any `qcc`
  diagnostic. A declaration of the viewing document's own module still renders
  exactly as it did before U4.

## Invariants — must not break

- Every pre-existing gate stays green: `run_tests.sh` (both modes),
  `test_codegen.sh` + `--leak-check` + `--selfcheck`,
  `test_build/run_build_tests.sh`, `ctest` (`build` and `build-asan`, including
  `parser_no_leak`, `resolver_component`, `resolver_reuse`), the reach-in gate,
  demos, examples.
- `qcc`/`bcc` user-visible behavior, `.bmod` format 5, `BuildCache` keys,
  D1–D17.
- Same-file LSP behavior and its goldens (`defn_*`, `hover_*`, `symbols_full`,
  `lifecycle`, the `fail/sema` re-verification leg). The only allowed exception
  is U2's prelude-resolution diffs, each justified individually.
- blangd's strict stdio framing (no stray bytes).
- blangd memory stays bounded across reparses (C3).
