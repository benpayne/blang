# Epic: feature-integration — Reconcile the devbot epics with origin/master's feature line

**Archetype**: evolve (structural + semantic integration of two diverged histories)

**Status**: launched

**Owner**: Ben Payne

**Created**: 2026-07-16 · **Updated**: 2026-07-16

**Source documents**: this session's reconciliation analysis (2026-07-16);
`git` divergence between local `master` and `origin/master` from common
ancestor `e8c6fc5`.

## Why

Local `master` and `origin/master` diverged into **two independent feature
streams** from common ancestor `e8c6fc5` (ahead 41 / behind 27):

- **Local (40 commits)** — the devbot epics: **blang-ast** (a dedicated `Sema`
  pass, source locations, `DiagnosticEngine`, type checking, match/generics
  soundness, ownership, concurrency) which **split the monolithic
  `CodeGen.cpp` into 7 `CG*.cpp` modules**; and **test-validation** (golden
  tests, real `bcc test`, ASan/fuzz CI, runtime unit tests).
- **origin/master (27 commits, PRs #125–#128)** — a parallel feature line:
  **channel send/recv**, **exhaustive match checking**, **built-in
  `Option`/`Result`**, an **event loop with timers**, **HTTP routing**, and a
  **database layer** (SQLite + libpq driver dispatch, `bcc migrate`, query →
  `Array<T>`, a todo web app). These were added inline to the **still-monolithic
  `CodeGen.cpp`** (+1102/−103 lines).

A naive `git merge` is **broken by construction**: origin's ~1,100 lines of new
codegen land in a `CodeGen.cpp` whose surrounding logic local moved into `CG*`
modules, and the semantic checks local moved into `Sema`. The full merge touches
**65 files / +12,484 lines**, with a genuine **duplicate feature** —
match-exhaustiveness is implemented on *both* sides, differently (origin inline
in `CodeGen.cpp`; local in `Sema`/`CGEnum`). This epic ports origin's features
into local's architecture and reconciles the overlaps, ending with one history
that has both feature sets and both test suites green.

## Done condition (epic level)

On the integration branch, merged to `master` as a single merge commit and
pushed to `origin`, all of the following hold (concrete commands in
`evaluation.md`):

1. **Both build configs clean**: LLVM build, parse-only build
   (`-DBLANG_ENABLE_LLVM=OFF`), and the sanitizer build (`-DBLANG_SANITIZE`)
   all build with no errors.
2. **Both test suites green**: `./run_tests.sh` (LLVM + parse-only) and
   `./test_codegen.sh` (with goldens) pass; `ctest --test-dir build` passes;
   `build-fuzz/fuzz_parse test_files/fuzz/corpus/ -runs=0` exits 0. (The
   test-validation gates survive the merge.)
3. **blang-ast enforcement intact**: `test_files/fail/sema/*.b` (26 fixtures
   incl. `audit_01..10`) still rejected with located `file:line:col: error:`;
   `Sema` still runs in all build modes.
4. **Origin's features work**: channel send/recv, event loop, `Option`/`Result`,
   exhaustive match, HTTP routing, and the database layer each pass their named
   origin `codegen_*.b` test (per evaluation.md); the todo-app E2E
   (`examples/todo_app/test_todo_app.sh`) passes and `bcc migrate` functions;
   `codegen_parked.txt` is empty.
5. **No duplicate/dead code**: match-exhaustiveness has a single implementation
   (reconciled through `Sema`); no orphaned copies of ported functions remain in
   `CodeGen.cpp` vs the `CG*` modules.
6. **History**: `git log` shows one merge commit joining the two lines; `git
   status` reports `master` up to date with `origin/master` (pushed), working
   tree clean.

## Requirements

| ID | Requirement | Priority | Verified by |
|----|-------------|----------|-------------|
| REQ-001 | The base merge is performed and all mechanical/additive conflicts (`ci.yml`, `CLAUDE.md`, `CMakeLists.txt`, `test_codegen.sh`, additive `CodeGen.h`/`qcc.cpp`/`bcc.cpp` sites, the `Type.h` setter rename) resolved keeping both sides; a compiling baseline exists. | P1 | Done-condition #1 (builds clean) |
| REQ-002 | Origin's **database layer** (SQLGen, SchemaMigration, ProjectConfig, `blang_db`, driver dispatch, `bcc migrate`, query→`Array<T>`) is integrated into local's structure and works. | P1 | Done-condition #4 (DB tests + `bcc migrate`) |
| REQ-003 | Origin's **channel send/recv** codegen + runtime is ported into the `CG*` module structure and routed through `Sema`. | P1 | Done-condition #4 (channel test) |
| REQ-004 | Origin's **event loop + timers** (`on` handler registration, run()/idle-exit) is ported and works. | P1 | Done-condition #4 (event-loop test) |
| REQ-005 | **Built-in `Option`/`Result`** and **exhaustive match** are integrated and **de-duplicated against local's `Sema` match-exhaustiveness** — a single implementation. | P1 | Done-conditions #4, #5 (exhaustive-match test; no dup) |
| REQ-006 | Origin's **HTTP routing** (.get/.post route table) + builtin `to_json` is integrated with local's stdlib/net BLang-native HTTP. | P2 | Done-condition #4 (HTTP routing demo/test) |
| REQ-007 | **CI reconciled**: both epics' CI legs coexist (parse/golden/sanitizer/leak/ctest/fuzz/demos + origin's DB codegen leg); the merged `ci.yml` runs green. | P1 | Done-condition #2 + CI run green |
| REQ-008 | **Full verification + push**: both test suites and all "Verified by" checks pass on the merge commit; `master` is pushed to `origin`. | P1 | Done-condition #6 |

## Non-goals

- **New features** beyond what already exists on either side — this is an
  integration, not a feature epic. Bugs found are fixed; scope is not widened.
- **Rewriting origin's DB/event-loop/channel designs** — port and wire them
  into local's architecture; don't redesign them.
- **Reworking the `CG*` split or `Sema`** — local's architecture is the target
  end state; origin's features move into it, not vice versa.

## Companion documents

| File | Purpose |
|------|---------|
| `workplan.md` | 8 units, dependency map, per-unit done conditions |
| `design.md` | integration strategy, the architecture decision, conflict map |
| `evaluation.md` | harness commands (both suites + origin demos), audit rubric |
| `manifest.yaml` | machine-readable run definition |

## Constraints & context for the manager

- **Target architecture is LOCAL's** (`CG*` modules + `Sema` + `DiagnosticEngine`).
  Origin's features are *ported into* it; do not resurrect the monolithic
  `CodeGen.cpp`.
- **Do the merge with a merge commit** (not rebase) so both histories and the
  full devbot audit trail are preserved.
- **Match-exhaustiveness is duplicated** — keep local's `Sema` version, port any
  cases origin's version covered that local's doesn't, delete origin's inline copy.
- **Both prior epics are verified/complete** — their gates (sema fixtures,
  goldens, leak-check, fuzz) must stay green; a regression in them is a merge bug.
- Work on `epic/feature-integration/uN-<slug>` branches off an integration base
  branch; the final result merges to `master` and pushes to `origin`.
- Constitution applies: `.specify/memory/constitution.md`.

## Open questions

| # | Question | Blocking | Status | Answer |
|---|----------|----------|--------|--------|
| — | (none yet — the match-exhaustiveness reconciliation is design guidance in design.md, decided at implement time) | | | |
| OQ-1 | **Unit ordering: U2 (channels) depends on U4 (built-in Option/Result).** Origin's channel `recv()` returns the built-in `Option<T>` (`genChanMethodCall` reads `mEnumDefMap["Option"]` / `getOrCreateEnumType(optEnum)` / `EnumDefinition::CreateBuiltinOption()`); its named tests (`codegen_channel*.b`) and the parked `chan_recv_non_exhaustive.b` all `match ch.recv() { some/none }` and require exhaustive-match-over-`Option` — both are **U4's** scope. Local has no built-in Option/Result yet (bare `Option` match ICEs). The workplan graphs U2 and U4 as U1-only siblings, but there is a real edge **U2 → U4**. **Recommendation:** reorder to `U1 → U4 → {U2, U3, U5, U6} → U7 → U8` (promote U4's built-in Option/Result + match-dedup ahead of U2), since Option/Result is foundational for channels (U2) and likely U5's `\|> first`. Alternative: fold a minimal built-in-Option registration into U2, but that bleeds U4's scope and risks conflicting with U4's match-exhaustiveness dedup. | **Yes — blocks U2 completion** | **RESOLVED (manager-approved 2026-07-16)** | **Reorder approved. Promote U4 ahead of U2. New execution sequence: `U1 → U4 → {U2, U3, U5, U6} → U7 → U8`.** Rationale accepted: hard dependency edge U2→U4; Option/Result foundational for channels (U2) + likely U5 pipeline ops; U4 depends only on U1 and is ready now; folding minimal Option into U2 would duplicate U4 work and collide with the "single implementation" match-exhaustiveness gate. Only the U2↔U4 order changes; all other deps and unit scopes stand. |

## Status log

| Date | Run | Event | Notes |
|------|-----|-------|-------|
| 2026-07-16 | — | epic created | Reconciliation of local (devbot epics) vs origin (feature line) found to be a structural+semantic re-integration; scoped as an epic. Ancestor e8c6fc5; local ahead 41 / behind 27; merge = 65 files / +12k lines. |
| 2026-07-16 | — | readiness review round 1 | Fresh-context + self audit; 7 findings. Fixes: named every origin feature test (all exist); added codegen_parked.txt skip mechanism (F2 — suite was going red at U1); fixed exhaustiveness grep to 'non-exhaustive match' string anchor (1 in Sema, 0 in codegen); corrected todo-app gate (examples/todo_app/test_todo_app.sh) + demos #14 collision (origin->15_timer) + NET_DEMOS build-only; relocate origin cgfail non-exhaustive → fail/sema; quarantine/golden reconciliation on U2/U3/U6; CG* ownership matrix; max_turns 500→700; ahead 40→41. done_condition re-synced verbatim. |
| 2026-07-16 | — | readiness review passed (10/10); status → ready | All 7 findings resolved; every named origin test verified to exist; parked-skip mechanism; exhaustiveness grep anchored; todo/demos gates corrected; done_condition verbatim. |
| 2026-07-16 | bb4873a4 | launched on devbot | endpoint http://localhost:8000, dir a6b2f628, fully_autonomous, no_progress_threshold 10, 50M tokens, max 700 turns / 14 days. PM approved autonomous push (gated behind all-tests-green in U8). |
| 2026-07-16 | bb4873a4 | U1 merged | Base merge (2-parent, `a31892d`) into `epic/feature-integration/base`; all conflicts resolved keeping local's CG*/Sema arch; monolith not resurrected; codegen_parked.txt (18) + parked/ (4) burn-down established; net.b→U6, 15_timer→U3. Auditor BLOCKER (dropped origin 535058b array-of-struct ARC) fixed by porting `emitArrayElemRetain` + IndexExpression retain-on-bind into CG* modules; deterministic, 0 leaks. Auditor cleared. |
| 2026-07-16 | bb4873a4 | OQ-1 raised at U2 | Porting channels surfaced a real dependency edge U2→U4: channel `recv()`→built-in `Option<T>` + exhaustive-match-over-Option are U4's scope. Recommended reordering U4 ahead of U2. U2 implementation paused pending manager decision. |
| 2026-07-16 | bb4873a4 | OQ-1 resolved → reorder | Manager approved reorder. **New execution sequence: `U1 → U4 → {U2, U3, U5, U6} → U7 → U8`** (U4 promoted ahead of U2; all other deps/scopes unchanged). Proceeding to U4 (built-in Option/Result + match-exhaustiveness dedup) next. |
| 2026-07-16 | bb4873a4 | U4 merged (auditor cleared) | Built-in Option/Result registered in CG*/Sema; 5 codegen fixes for the erased-payload model (8-byte payload slot, match-tail terminator, concrete-type-arg recovery in Sema+codegen, generic-aware enum scope-cleanup, temporary-subject payload release). Single match-exhaustiveness impl in Sema (origin inline copy gone with monolith). 6 tests un-parked, 2 non-exhaustive fixtures → fail/sema (floor 26→28). Commit e2bb7e4; 0 leaks. |
| 2026-07-16 | bb4873a4 | U2 merged (auditor cleared) | Channel send/recv/close ported into CGStruct.cpp + genMethodCall dispatch; recv→built-in Option<T>. Sema::resolveMethodCall annotates ch.recv() as Option<T> (fixes the U4-flagged dependency: exhaustiveness fires + no null-recovery leak). **Decision (accepted as in-scope safety guard, no OQ):** channel element types are restricted to VALUE types — a refcounted heap element (string/Array/Buffer/struct) is rejected by Sema with a located error (byte-copy transport cannot own a refcount; origin only ever supported chan<int>). 3 channel tests un-parked (parked 12→9; codegen_channel_spawn quarantined), chan_recv_non_exhaustive + new chan_refcounted_element → fail/sema (floor 28→30). Commit 428779e; 0 leaks. |
| 2026-07-16 | bb4873a4 | U3 merged (auditor cleared) | Event-loop/timer port: genEventHandler (CGLambda.cpp) now registers `on EXPR{}` on the global loop via __blang_event_on (was an inline-call stub) — fires when timer.run() enters the loop; getOrDeclareEventOn defined in CGRuntime.cpp. Runtime channel-registry fix (blang_runtime.c): channels freed at __blang_runtime_shutdown after threads joined — resolves a pre-existing U2 intermittent channel leak (codegen_channel_spawn). 4 timer tests un-parked (parked 9→5), demos/15_timer.b in NET_DEMOS. Commit 1650a9b; 0 leaks, auditor-verified thread-safe/no-double-free. |
