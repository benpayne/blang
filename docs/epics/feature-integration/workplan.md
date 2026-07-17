# Workplan: feature-integration

**Epic**: [overview.md](overview.md) · **Archetype**: evolve

Each unit is one implementing hire's assignment through the constitution's
five-phase lifecycle (create spec → spec audit → implement → code audit →
merge). Unlike a greenfield epic, the units here land on a shared **integration
base branch** (`epic/feature-integration/base`) rather than `master` directly,
because they build on each other's ported code; only U8 fast-forwards `master`
and pushes. Every unit boundary leaves the integration branch **building** and
its accumulated suites green.

## Unit map

```text
U1 (base merge + mechanical + DB files) ─▶ U2 (channels) ─▶ U3 (event loop) ─┐
                                        ├▶ U4 (Option/Result + match dedup) ──┤
                                        └▶ U5 (database codegen + migrate) ───┼▶ U7 (CI reconcile)
                                                        U6 (HTTP routing) ────┘        │
                                                                                       ▼
                                                                        U8 (full verify + merge to master + push)
```

U1 establishes a compiling base. U2–U6 are the feature ports (parallelizable
after U1, but each touches codegen so the manager should serialize any that edit
the same `CG*` file). U7 reconciles CI. U8 is the capstone: full verification,
merge to `master`, push.

## Units

### U1 — Base merge, mechanical conflicts, and separable DB files
- **Covers**: REQ-001 (+ groundwork for REQ-002)
- **Preconditions**: none
- **Work**: create `epic/feature-integration/base` off `master`; `git merge
  --no-ff origin/master`; resolve the **keep-both / mechanical** conflicts
  (`ci.yml`, `CLAUDE.md`, `CMakeLists.txt`, `test_codegen.sh`, the additive
  `CodeGen.h`/`qcc.cpp`/`bcc.cpp` sites, the `Type.h` `setType`/`setVariableType`
  setter — unify to one name, update callers). Bring in origin's **separable**
  files that don't collide with the `CG*` split (`SQLGen`, `SchemaMigration`,
  `ProjectConfig`, `runtime/blang_db`). For origin's inline `CodeGen.cpp`
  feature code that has no home yet, **stub/park** it so the tree compiles, with
  `// TODO(feature-integration U#)` markers routing each block to its unit.
  Because the merge also brings in origin's `codegen_*.b` feature tests (which
  `test_codegen.sh` auto-globs and would fail while their codegen is parked),
  U1 creates `test_files/codegen_parked.txt` listing every not-yet-ported origin
  feature test and teaches `test_codegen.sh` to **skip** listed tests (report
  `PARKED`, exclude from pass/fail). Also resolve the `demos/Makefile` conflict
  and the **demo #14 collision** (local `14_file_server.b` vs origin
  `14_timer.b` — renumber origin's to `demos/15_timer.b`, update `NET_DEMOS`).
- **Done condition**: the integration branch **builds** in the LLVM config;
  `./run_tests.sh` (both build modes) green; `./test_codegen.sh` green with
  parked tests excluded (`codegen_parked.txt` non-empty, one entry per pending
  U2–U6 feature test); no regression in the blang-ast `fail/sema` fixtures or
  test-validation goldens. Parked burn-down enumerated in the PR.
- **Audit**: per constitution. **Budget**: large (the structural merge).
- **Speckit**: `base-merge`

### U2 — Channels (send/recv) ported into CG*/Sema
- **Covers**: REQ-003 · **Depends on**: U1
- **Work**: move origin's channel send/recv codegen into the right `CG*` module
  (likely `CGExpressions`/`CGStatements`), route the send/recv type checks
  through `Sema`, keep the `__blang_chan_*` runtime. Remove the channel tests
  from `codegen_parked.txt`; relocate origin's `cgfail/chan_recv_non_exhaustive.b`
  to `fail/sema/` with a `.b.expected` located-error pattern (Sema rejects in all
  modes). If any channel test is non-deterministic, add it to both
  `codegen_quarantine.txt` and `approved_quarantine.txt` (keep diff-equal);
  otherwise generate its golden via `--update-goldens`.
- **Done condition**: `./test_codegen.sh test_files/codegen_channel.b
  test_files/codegen_channel_closed.b test_files/codegen_channel_spawn.b` exits 0;
  those tests are no longer in `codegen_parked.txt`; `pass/chan_send_recv.b` parses;
  `chan<T>` send/recv work end-to-end; `Sema` type-checks them; suites green.
- **Speckit**: `channels`

### U3 — Event loop + timers ported
- **Covers**: REQ-004 · **Depends on**: U1
- **Work**: port origin's event loop (timers, `on`-handler registration, explicit
  `run()`, idle-exit, per-timer cancel) into local's structure; reconcile with
  local's `EventHandler` codegen. Port origin's `stdlib/timer.b`. Remove the
  timer tests from `codegen_parked.txt`; generate goldens for deterministic ones
  or quarantine (both lists) the timing-dependent ones. Owns the renumbered
  `demos/15_timer.b` from U1.
- **Done condition**: `./test_codegen.sh test_files/codegen_timer_event.b
  test_files/codegen_timer_cancel.b test_files/codegen_timer_oneshot.b
  test_files/codegen_timer_helper.b` exits 0; those removed from
  `codegen_parked.txt`; `on` registers (not inline-calls); `demos/15_timer.b`
  builds; suites green.
- **Speckit**: `event-loop`

### U4 — Built-in Option/Result + exhaustive-match de-duplication
- **Covers**: REQ-005 · **Depends on**: U1
- **Work**: make `Option<T>`/`Result<T,E>` built-in generic types in the type
  system as origin did, reconciled with local's generic handling. **De-duplicate
  match-exhaustiveness**: keep local's `Sema` implementation as the single
  source of truth; port any variant/case coverage origin's inline version had
  that local's lacks; **delete origin's inline `CodeGen.cpp` copy**. Relocate
  origin's `cgfail/match_non_exhaustive.b` and `cgfail/builtin_option_non_exhaustive.b`
  to `fail/sema/` with `.b.expected` located-error patterns (Sema rejects in all
  build modes; the `fail/sema` floor rises accordingly). Remove the ported
  positive tests from `codegen_parked.txt`.
- **Done condition**: `./test_codegen.sh test_files/codegen_builtin_option.b
  test_files/codegen_builtin_result.b test_files/codegen_match_exhaustive.b
  test_files/codegen_match_wildcard_enum.b` exits 0; the relocated `fail/sema`
  non-exhaustive fixtures are rejected in `--parse-only` with the canonical
  `^[^:]+\.b:[0-9]+:[0-9]+: error: ` diagnostic; **single-impl check**:
  `test "$(grep -rl 'non-exhaustive match' Sema.cpp | wc -l)" -eq 1` AND
  `grep -rE 'non-exhaustive' CodeGen.cpp CG*.cpp` finds nothing (origin's inline
  copy deleted); suites green.
- **Audit**: per constitution + confirm single implementation. **Speckit**: `option-result-match`

### U5 — Database layer codegen + `bcc migrate`
- **Covers**: REQ-002 · **Depends on**: U1
- **Work**: wire origin's query/insert/update/delete codegen and `@db` routing
  into the `CG*` structure; connect the driver dispatch (SQLite/libpq),
  connection config from `blang.toml`, and `bcc migrate` (with the destructive-
  change guard). Relocate `cgfail/query_bad_field.b` if it becomes a Sema-mode
  rejection. Remove the DB tests from `codegen_parked.txt`.
- **Done condition**: `./test_codegen.sh test_files/codegen_db_query.b
  test_files/codegen_db_query_rows.b` passes (guarded by SQLite availability as
  origin's CI does); `bash examples/todo_app/test_todo_app.sh` passes
  (`bcc build` → `bcc migrate --apply` → run); those tests removed from
  `codegen_parked.txt`; suites green.
- **Speckit**: `database`

### U6 — HTTP routing + builtin to_json
- **Covers**: REQ-006 · **Depends on**: U1
- **Work**: integrate origin's `.get/.post` route table + closure-escape lifetime
  fix + builtin `to_json` with local's BLang-native `stdlib/net.b` HTTP. Resolve
  any overlap between origin's HTTP layer and local's HttpServer. Relocate
  origin's `cgfail/to_json_not_annotated.b` if it becomes a Sema-mode rejection.
  Remove the HTTP tests from `codegen_parked.txt`; `codegen_http_routing.b` binds
  a socket → add it to both quarantine lists (keep diff-equal) rather than
  goldening. `demos/13_http_server.b` is build-only (NET_DEMOS).
- **Done condition**: `./test_codegen.sh test_files/codegen_http_routing.b
  test_files/codegen_http_json_response.b test_files/codegen_to_json_builtin.b`
  exits 0; those removed from `codegen_parked.txt`; `demos/13_http_server.b`
  builds (`make -C demos all`); `to_json` works; suites green.
- **Speckit**: `http-routing`

### U7 — CI reconciliation
- **Covers**: REQ-007 · **Depends on**: U2, U3, U4, U5, U6
- **Work**: merge the two `ci.yml` job sets so both coexist — test-validation's
  golden/bcc-test/sanitizer/leak/ctest/fuzz/demos legs **and** origin's DB
  codegen leg (libpq/sqlite link). Ensure the workflow runs green.
- **Done condition**: `.github/workflows/ci.yml` has both job sets; a CI run on
  the integration branch is green.
- **Speckit**: `ci-reconcile`

### U8 — Full verification, merge to master, push
- **Covers**: REQ-008 · **Depends on**: U1–U7
- **Work**: run the full epic acceptance block (both suites, both feature sets,
  origin demos, blang-ast sema fixtures, test-validation goldens/leak/fuzz);
  confirm `codegen_parked.txt` is **empty** (every feature ported) and no
  duplicate/dead code; fast-forward `master` to the integration base (single
  merge commit preserved), push to `origin`. Update `CLAUDE.md` (both feature
  sets now on master; active-epics).
- **Done condition**: the `overview.md` done condition holds in full; `master`
  pushed to `origin`, working tree clean.
- **Audit**: per constitution; **functional review** = full epic done condition.
- **Speckit**: `verify-and-merge`

## Sequencing notes for the manager
- U1 is the hardest and gatekeeps everything — do not start U2–U6 until the base
  branch compiles and both suites are green.
- U2–U6 that edit the same `CG*` file must be serialized (merge conflicts on the
  base branch); the manager rebases each onto the latest base after a merge. See
  `design.md` §"Predicted `CG*` ownership" — serialize **U2 with U5**
  (`CGExpressions.cpp`) and **U2 with U3** (`CGStatements.cpp`); U4 and U6 can run
  alongside. "Parallel" here is best-effort, not guaranteed.
- If a feature port surfaces a genuine architecture question (e.g. how origin's
  event loop should interact with local's spawn/async), raise an Open Question
  rather than guessing.
- U8 is the only unit that touches `master`/`origin`; everything else lands on
  the integration base branch.
