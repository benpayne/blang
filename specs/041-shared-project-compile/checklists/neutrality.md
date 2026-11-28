# Neutrality checklist — R3 ordering (U1 code-audit contract)

Each item from [research.md](../research.md) R3, with the pre-unit `qcc.cpp`
line (at `epic/modules-v2-lsp` @ `2f68efe`) and the `ProjectCompile.cpp` line
that now carries it (branch `041-shared-project-compile`). Every item was lifted
in its original order; the byte-identity sweep (see [baseline.md](baseline.md))
is the empirical proof.

- [x] 1. Stable `.bmod`-first reorder — qcc.cpp L376–L389 → ProjectCompile.cpp L247–L257 (applied inside `run` to a local view of `plan.inputs`).
- [x] 2. Prelude + combine scopes only in combine mode, from `resolver.newModuleScope`, context-owned — qcc.cpp L424–L429 → L279–L286 (`mOwnedModuleScopes`).
- [x] 3. Module name = basename minus last-dot suffix — qcc.cpp L462/L722/L747 (three copies) → `moduleNameOf` (one copy, L37–L48), used at L302.
- [x] 4. User file = last input in the reordered list — qcc.cpp L498 → L324.
- [x] 5. Prelude providers exactly `buffer`/`collections`, by name — qcc.cpp L499–L500 → L325–L326.
- [x] 6. Foreign-type pre-scan before lexing; malformed line = error + `continue` of the line loop; then the GLOBAL-`hadError` skip — qcc.cpp L552–L591 → `prescanForeignTypes` (L171–L210) called at L372–L392.
- [x] 7. Catastrophic parse failure sets `hadError` and `continue`s, module not recorded — qcc.cpp L598–L606 → L404–L411.
- [x] 8. `stampDefiningOrigin` before origin selection — qcc.cpp L615 → L418.
- [x] 9. Origin: interface = `--bmod-origin` exact-path lookup (now `CompileInput::origin`, filled by qcc from `bmodOrigins[path]`), source = `--module-origin`, else `realpath` with the located 1:1 error + `continue` — qcc.cpp L625–L660 → L429–L466 (skipped under `kNone`).
- [x] 10. Format probe (≤ 8 lines, missing marker = 1), mismatch error + `continue`; then `setExtern`, `setFromInterface`, injection — qcc.cpp L662–L735 → L468–L521 (`probeFormatVersion` L214–L231; `injectBmodSymbols` L101–L165). `bmodMap` dropped (item 14).
- [x] 11. Record module/scope/name; `bmodEmitScope` per source input — qcc.cpp L737–L755 → L523–L531.
- [x] 12. `PARSE_TRACE("Completed parse")`, then Sema (source only), then eager PROMOTE (combine, provider, manifest names, `addSymbol` + `addType`) — qcc.cpp L756–L789 → L533–L561.
- [x] 13. Import-diagnostics pass after the loop, gated on `combine || any interface input`, in module order, skip extern/null, exact error/warning text + `unused-import` slug — qcc.cpp L807–L845 → L563–L607.
- [x] 13a. Recorded vs. dropped (F1): `record.module`/`recorded` set only at the L737-equivalent step (L525–L527); every post-loop pass iterates `mModuleIndices` (L575, L620). Locked by ctest C7.
- [x] 14. `bmodMap` was write-only — dropped (neutral; nothing read it).

Also moved: the non-LLVM half of qcc.cpp L919–L937 (prelude-shadow set) → L609–L632, consumed by qcc's `allStructs` filter via `suppressedPrelude()`; the module-prefix rule (qcc.cpp L990–L1006) → `CompileContext::codegenPrefix` (L55–L67), name-keyed as before.
