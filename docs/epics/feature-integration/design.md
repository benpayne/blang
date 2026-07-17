# Design: feature-integration

**Epic**: [overview.md](overview.md)

## Context (measured 2026-07-16)

Common ancestor: `e8c6fc5`. Local `master` ahead 40 / behind `origin/master` 27.

**Local stream (the target architecture):**
- Split monolithic `CodeGen.cpp` into 7 modules: `CGEnum`, `CGExpressions`,
  `CGLambda`, `CGRuntime`, `CGStatements`, `CGStruct`, `CGTypes`.
- New `Sema` pass (`Sema.h/.cpp`) between parse and codegen, running in **all**
  build modes; owns type checking, member resolution, match-exhaustiveness,
  generic-constraint checks, ownership/move, concurrency rules.
- New `DiagnosticEngine`, `SourceLocation` on every AST node, `LocationDumper`.
- test-validation infra: golden comparison in `test_codegen.sh`, `bcc test`
  runner, `BLANG_SANITIZE`, CTest runtime units, `fuzz_parse`, CI legs.

**Origin stream (features to port in):**
- Added ~1,100 lines to the **monolithic** `CodeGen.cpp` (+1102/−103): channel
  send/recv, exhaustive match, built-in `Option`/`Result`, event loop + timers,
  HTTP routing, database codegen. Origin has **none** of the `CG*` files or `Sema`.
- New separable files: DB layer (`SQLGen`, `SchemaMigration`, `ProjectConfig`
  changes, `runtime/blang_db`), touched `QType.cpp`, demos (`13_http_server.b`,
  todo app).

**Merge shape:** 9 direct conflicts (`ci.yml`, `CLAUDE.md`, `CMakeLists.txt`,
`CodeGen.cpp`, `CodeGen.h`, `Type.h`, `bcc.cpp`, `qcc.cpp`, `test_codegen.sh`),
but the real work is that origin's inline `CodeGen.cpp` additions have no home in
local's `CG*` structure and must be redistributed, and semantic checks routed
through `Sema`.

## Architecture decision (the crux)

**Local's architecture is the end state; origin's features port into it.**

| # | Decision | Rationale | Rejected |
|---|----------|-----------|----------|
| D1 | Keep `CG*` split + `Sema`; port origin's inline `CodeGen.cpp` features into the right module + route checks through `Sema` | Local is the production-hardening direction (enforcement, diagnostics, testability); reverting to the monolith throws away two verified epics | Keep origin's monolithic `CodeGen.cpp`; re-inline local's modules |
| D2 | Merge commit, not rebase | Preserve both histories + the devbot audit trail; avoid rewriting 40 verified commits | Rebase local onto origin |
| D3 | Match-exhaustiveness: local's `Sema` version wins; port missing cases from origin's; delete origin's inline copy | One implementation; `Sema` is where checking lives now | Keep both (dead/divergent code) |
| D4 | Land units on a shared integration **base branch**, not `master`; only U8 touches `master`/`origin` | Units build on each other's ports; keeps `master` clean until fully verified | Merge each unit to master (half-integrated master) |
| D5 | U1 parks un-homed origin codegen behind `// TODO(feature-integration U#)` so the tree compiles from the start | A compiling baseline makes every later unit independently testable | Big-bang resolve everything in one unit |

## Conflict map (which unit owns each)

| File(s) | Nature | Owner |
|---------|--------|-------|
| `ci.yml` | both added job sets | U1 mechanical, U7 full reconcile |
| `CLAUDE.md`, `CMakeLists.txt`, `test_codegen.sh` | additive (docs/build/test) | U1 |
| `CodeGen.h` — `setTestMode` vs `setDbConfig`/`addDbNamedConn` | additive, orthogonal | U1 (keep both) |
| `qcc.cpp` — Sema/Diagnostic includes vs SchemaMigration; setTestMode vs setDbConfig call | additive | U1 (keep both) |
| `Type.h` — `setType` vs `setVariableType` (same `mType`) | rename collision | U1 (unify to one, fix callers) |
| `bcc.cpp` — test subcommand vs migrate/db-config forwarding | additive | U1 (keep both) |
| `CodeGen.cpp` — origin's +1100 inline features vs local's split | **structural** | U1 parks; U2–U6 port per subsystem |
| `SQLGen*`, `SchemaMigration*`, `ProjectConfig*`, `blang_db*` | origin-only files | U1 brings in; U5 wires codegen |

## Interfaces & contracts (must hold post-merge)
- `Sema` still runs in every build mode; blang-ast's `fail/sema/*` fixtures
  still rejected with located diagnostics.
- test-validation gates unchanged: goldens compared, `--leak-check` fatal on
  leaks, `ctest ≥30`, fuzz replay clean, `bcc test` runner behavior.
- Origin's public surface preserved: channel send/recv syntax, `Option`/`Result`
  built-ins, event loop `run()`, `bcc migrate`, DB query expressions, HTTP routing.
- Default (non-sanitizer) build output unchanged; the `CG*` module layout is the
  canonical codegen structure.

## Invariants — must not break
- The 26 `fail/sema` fixtures (incl. `audit_01..10`); the 57 goldens; the 45
  ctest runtime tests; the fuzz corpus replay.
- Origin's DB codegen test (SQLite-guarded) and the todo app.
- No file resurrects monolithic-`CodeGen.cpp` logic that now lives in `CG*`.
