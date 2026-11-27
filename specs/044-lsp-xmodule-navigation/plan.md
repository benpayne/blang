# Implementation Plan: Cross-module go-to-definition and hover in blangd

**Branch**: `044-lsp-xmodule-navigation` | **Date**: 2026-11-26 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/044-lsp-xmodule-navigation/spec.md`
(epic `docs/epics/modules-v2-lsp/`, unit U4, REQ-007/REQ-008).

## Summary

blangd already compiles each document in `bcc`'s context (U2/U3), and its
definition path already emits the declaration's own file. A probe of the U3
head ([research.md](research.md) baseline) shows the remaining gaps:

- **(1)** Dependency declarations come from in-memory interfaces, so
  definitions land in `<depDir>/<name>.bmod`.
- **(2)** A method on a use-capability-only value is unresolved, because Sema
  resolves the receiver by name in a scope that cannot name the type (D7).
- **(3)** Type names in type position have no navigable node.
- **(4)** Hover renders foreign declarations as if they were local, with no
  display name.

U4 closes each gap using data and rules the shared compile already owns:

- an **interface → source map** over U3's kept dependency `Run`s, matching by
  kind, name and owner (R2);
- the **use-capability type set**, lifted verbatim from `qcc.cpp` into
  `CompileContext` so that `qcc` and blangd share one copy (R3);
- parser-recorded **type references**, emitted where `QType.cpp` itself
  resolves the token, and enabled only for blangd (R4);
- a **foreign hover renderer** implementing FR-008, which is design.md D4 plus
  the accepted addendum (R5/R6).

Same-module navigation keeps its pre-U4 code path.

## Technical Context

**Language/Version**: C++17 (house style, constitution V); Python 3 stdlib (the existing `tools/lsp_client.py`, unchanged).

**Primary Dependencies**: none new. The work touches the shared frontend
(`ProjectCompile`, `QType.cpp`, `Sema`, `Type.h`) and blangd (`lsp/`). There
is no LLVM in any new code.

**Storage**: N/A. Everything is in memory per compile.

**Testing**: `test_lsp.sh` transcript goldens (`test_files/lsp/xmod_*`), the `parser_no_leak` ctest (LSan), `blangd_no_subprocess`, and the full epic gate list.

**Target Platform**: Linux; blangd over stdio.

**Project Type**: compiler and language server.

**Performance Goals**: Navigation is per request, over an AST already built. The `didChange`→publish median stays ≤ 500 ms (REQ-009); type-reference recording adds one vector push per resolved type token.

**Constraints**: same-file results byte-identical; no subprocess or network; no `.bmod` format, `BuildCache` or D1–D17 change; `qcc`/`bcc` output byte-identical; KG-7 ownership.

**Scale/Scope**: 8 reference kinds × {definition, hover}, plus the not-open, unsaved-buffer and failed-dependency goldens.

There are no unresolved items; research.md R1–R8 resolve every unknown.

## Constitution Check

*GATE: must pass before Phase 0 research; re-checked after Phase 1 design.*

| Principle / Gate | Status | How |
|---|---|---|
| I. One Right Way | PASS | One copy of each rule. The use-capability set and the member lookup are **moved** into shared code that both `qcc` and blangd call; they are not copied. Type references record the parser's own resolution. The display rule is written once (spec FR-008 / design.md D4 addendum). No language change; the `CLAUDE.md` `lsp/` entry is updated. |
| II. Test-Gated | PASS | Every SC maps to a committed golden or ctest (table below). Existing goldens: zero edits. |
| III. Reject, Don't Coerce | PASS | An unmappable target answers `null`, never a guessed `.bmod` or ambiguous location. Ambiguous use-capability names answer `null`. |
| IV. Memory Safety Verified | PASS | All new state is owned by `CompileResult`/`CompileContext` (data-model.md). `parser_no_leak` gains a usebox reparse-and-navigate loop under LSan. |
| V. House Style | PASS | |
| VI. Principled Design | PASS | Grounded in this plan, research R1–R8 and design.md D2/D4. The rejected alternatives (changing Sema stamping, a blangd-only struct search, annotating the `.bmod`) are recorded with reasons. |
| Gate 1 both build configs | PASS | All new code is frontend/LSP and needs no LLVM. The goldens run in both modes. |
| Gate 5 docs | PASS | `CLAUDE.md` `lsp/` and `ProjectCompile` entries are updated, and the D4 addendum is committed. |
| Gate 7 security | PASS | No new input surface: requests read in-memory ASTs only. The cursor's line/column comes from untrusted client input and is bounds-safe (locator plus span compare; no indexing into text). |

There are no violations.

**Post-design re-check**: PASS. Phase 1 introduced no new dependency, no I/O
and no change visible to `qcc`.

## Project Structure

### Documentation (this feature)

```text
specs/044-lsp-xmodule-navigation/
├── spec.md · plan.md · research.md (R1–R8) · data-model.md · quickstart.md
├── contracts/navigation.md     # resolution order, guarantees G1–G4, hover renderings, golden inventory, shared API additions
└── checklists/requirements.md
```

### Source Code (repository root)

```text
ProjectCompile.h/.cpp   EDIT  TypeReference; ModuleRecord::typeReferences; CompilePlan::recordTypeReferences;
                              install/uninstall the parse sink per module parse;
                              CompileContext::useCapabilityTypes (lifted from qcc.cpp)
QType.cpp               EDIT  after a successful type resolution, record {token loc, len, def} into the sink (if installed)
Type.h                  EDIT  StructDefinition::findMethod(name); the sink hook declaration (Frontend.h if better placed)
Sema.h/.cpp             EDIT  use findMethod + a shared receiver-type helper (no behavior change)
qcc.cpp                 EDIT  call ctx->useCapabilityTypes(...) in place of the inline allStructs/allEnums loop
lsp/Compile.h/.cpp      EDIT  CompileResult += InterfaceSourceMap; set recordTypeReferences on the document plan
lsp/ProjectContext.h/.cpp EDIT expose interface-path → dep Run pairs (each closure node's bmodPath + run)
lsp/Navigation.h/.cpp   NEW   target resolution (contract order 1–5), the interface → source map, use-capability
                              member lookup, FR-008 display names, the foreign hover renderer
lsp/Server.cpp          EDIT  definition/hover delegate to Navigation (same-module branch is the pre-U4 code)
lsp/AstLocator.*        UNCHANGED renderer for same-module targets (pre-U4 output preserved)
ParserNoLeakTest (ctest) EDIT  usebox: 50 × (compile + definition/hover on b.get()) under LSan
test_files/lsp/xmod_*.lsp.jsonl + .expected.out   NEW  (contracts/navigation.md inventory)
CMakeLists.txt          EDIT  lsp/Navigation.cpp into blangd (+ the ctest that links lsp sources)
CLAUDE.md               EDIT  lsp/ + ProjectCompile entries
```

**Structure Decision**: Navigation logic moves out of `Server.cpp` into a new
`lsp/Navigation` unit. `AstLocator` is left untouched so the same-module
output cannot drift. Every rule blangd consumes lives in shared frontend
files, which `qcc` also compiles.

## Key design decisions

- **N-1 — Map, don't annotate** (R2). An interface declaration is mapped to
  its source declaration by kind, name and owner, within the dependency's own
  kept `Run`. The mapping must yield a unique match, or the result is `null`.
- **N-2 — Use-capability comes from the compiler's set** (R3). The resolution
  is `CompileContext::useCapabilityTypes`, then a unique name match, then
  `findMethod`. Sema stamping is untouched, so no `qcc` diagnostic can appear
  or disappear.
- **N-3 — Type references are recorded where they resolve** (R4). Recording
  is gated by a plan flag that only blangd sets. Same-file targets are not
  answered (FR-007).
- **N-4 — Two renderers** (R6). Same-module targets use the pre-U4
  `AstLocator::hoverText`. Foreign targets use the FR-008 renderer, keyed on
  the defining `ModuleRecord`.
- **N-5 — Ownership** (R8). `InterfaceSourceMap` shares the `Run`s that
  `CompileResult::deps` already owns. Type references live in the context.
  There is no cache.

## How navigation is sourced from the shared entry point

| Navigation input | Produced by (shared) |
|---|---|
| Declarations, their `SourceLocation`s, and module records (path, `moduleName`, role) | `ProjectCompile::run` over the document plan |
| Dependency source declarations | the dependency's own `ProjectCompile::run` (U3 `Run`), the same compile that fed `BmodEmitter::emit` |
| Use-capability type set (with the prelude shadow filter) | `CompileContext::useCapabilityTypes`, also called by `qcc.cpp` |
| Method-by-name rule | `StructDefinition::findMethod`, also called by `Sema` |
| Type-name bindings | `QType.cpp`'s own resolution, recorded into `ModuleRecord::typeReferences` |
| Import bindings / aliases | `Module::getImports()` (`ImportStatement::getModuleName`/`getLocalQualifier`) |
| Prelude provider | `CompileContext::isPreludeProvider` |

blangd adds only lookups over these structures: a kind/name match and a
qualifier choice. It adds no resolution, visibility or export rule.

## Success criteria → tests

| SC | Test |
|----|------|
| SC-001 | 8 `xmod_defn_*` goldens and `xmod_defn_not_open` |
| SC-002 | 8 `xmod_hover_*` goldens, whose strings equal the contract table |
| SC-003 | `grep -l '\.bmod"' test_files/lsp/xmod_*.expected.out` is empty (quickstart §1); plus the code-level guarantee G1 (null on unmapped) |
| SC-004 | `test_lsp.sh` green with zero edits to pre-existing goldens (quickstart §2) |
| SC-005 | full gate list (workplan), plus `parser_no_leak` (LSan) and `blangd_no_subprocess` |
| SC-006 | `test_lsp.sh` transcript count increases (≥ 19 new) |

## Risks

- **R2 attribution of transitive generic bodies.** These may be attributed
  to `midx.bmod` rather than `boxq.bmod`. Mitigation: the fallback search over
  all closure runs still requires a unique match. The usebox golden pins the
  outcome.
- **Type-reference spans in generic arguments** (`Map<string, Counter>`).
  Each argument token is recorded separately. Spans never overlap, so the
  innermost one is unambiguous.
- **IR byte-identity of the `useCapabilityTypes` lift.** It is guarded by
  goldens and an explicit spot diff against the `043` `qcc`.

## Complexity Tracking

There are no constitution violations.
