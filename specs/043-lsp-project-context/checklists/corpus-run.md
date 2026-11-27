# U3 corpus run record (T024–T027, T038, T042)

## T024: project corpus (`--corpus project --check-list`, `build`, LLVM + llc)

The reference is the last `/qcc ` line that `bcc build -v` prints for a scratch copy (scratch `XDG_CACHE_HOME`), re-run with `--json`. xmodule layouts are compared against `qcc --parse-only --json consumer.b lib.bmod`; a missing **or** extra error fails.

**29/29 PASS**:

- 17 projects. Every one matches its reference, and all have 0 diagnostics except `examples/todo_app`, whose 2 diagnostics are the compiler's own:
  - examples: calculator, chat, kv, todo_app, wordfreq, worker_pool;
  - test_build: aliasapp, boxapp, **counterapp (0)**, diamondapp, **myapp (0)**, printapp, sizeapp, timerapp, todoapp, usebox;
  - the multifile fixture.
- 11 xmodule layouts: alias_rebind, imported_datacontract_field, imported_field_access, imported_generic_field_access, imported_generic_struct_literal, imported_struct_literal, name_capability_requires_import, private_init, private_method, unknown_module_import, unqualified_import_call. These give SC-002.

**SC-001**: `test_build/myapp/main.b` and `test_build/counterapp/main.b` each have 0 diagnostics.

**SC-007 / M1 accounting**: 179/179 accounted for.

- The stdlib corpus is **177/177** PASS in both `build` and `build-nollvm`.
- The two files moved out of it are `test_build/myapp/main.b` and `test_build/counterapp/main.b`. They are project members with `[deps]`, and both PASS in the project corpus.
- No file drops out of both sets.

**Multifile teeth**: renaming `helpers.triple` makes `bcc build` fail. blangd then reports `Module 'helpers' has no member 'triple'` at `main.b` 5:26, the same location as the compiler.

## T025: interface byte-parity (corpus tool, `interfaceParityTest`)

All **9/9** library interfaces are byte-identical to the `.bmod` that a cold in-place `bcc build` writes under a scratch cache: mathlib (913 B), boxa (304), boxb (270), counterlib (241), boxq (195), midx (154), printlib (256), sizelib (345), todolib (199).

## T026: wiring

- **`test_lsp.sh`**:
  - `build`: 79/0, including the project-corpus leg (29/29).
  - `build-nollvm`: 78/0. The project corpus prints `SKIP: project corpus (needs llc + an LLVM qcc; runs in CI build-system)`; under `CI=true` that skip is a FAIL.
- **CI**:
  - the `lsp` job passes `--no-project-corpus`;
  - the `build-system` job runs `--corpus project --check-list` after `run_build_tests.sh`.

## T027: `parser_no_leak` project loop

- 50 reparses of `test_build/usebox/main.b` using a fresh `StdlibLocation` and an `EmptyOverlay`. Each reparse asserts `deps >= 2` and zero diagnostics.
- **`build-asan`**: `ParserLeakTest: OK`, LSan-clean, about 20 s.
- **Teeth (not committed)**: leaking one `CompileResult` per iteration fails the run with `LeakSanitizer: detected memory leaks` (37 MB in 453k allocations). Reverted afterwards.

## T038: latency (`tools/lsp_latency.py`, `build`, 20 edits each)

| File | Median | Min / max | Budget |
|---|---|---|---|
| `examples/todo_app/main.b` | 78.1 ms | 77.5 / 87.7 | ≤ 500 ms: OK |
| `test_build/usebox/main.b` (two dependencies derived from source on every reparse) | 75.9 ms | 75.3 / 80.6 | ≤ 500 ms: OK |
