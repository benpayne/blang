# U2 corpus, latency and leak-lock evidence

- **Corpus** (`tools/lsp_diag_corpus.py --corpus stdlib --check-list`): **179 passed, 0 failed** in `build` (1m29s) and in `build-nollvm` (1m15s). The list holds 171 `codegen_*.b` files (172 minus `codegen_nsprefix_arc.b`, whose reference compile exits 1; research R2), all 6 `examples/*/main.b`, and the 2 project overview files. The completeness check passes.

  | Overview file | Published = reference |
  |---|---|
  | `test_files/codegen_prelude_no_import.b` | 0 diagnostics (was 14 false errors) |
  | `test_files/codegen_http_routing.b` | 0 (was 16) |
  | `examples/kv/main.b` | 0 (was 7) |
  | `examples/todo_app/main.b` | 2: the compiler's own `unused variable` **warnings** at 170:3 and 179:6, which `bcc` prints too (was 4 false errors). SC-001's "zero diagnostics" wording overstated this file; "clean" means exit 0 per the overview's definition, and parity is exact. |
  | `test_build/myapp/main.b` | 15 (the single-file reference's unresolved-dependency errors, matched exactly; U3 moves it to the project reference) |
  | `test_build/counterapp/main.b` | 8 (likewise) |

  150 of the 179 files have zero diagnostics; 29 carry warnings or errors, all matched.
- **Latency** (`tools/lsp_latency.py`, `build/`): `examples/todo_app/main.b` median **75.6–78.2 ms** across runs (n=20, budget 500 ms).
- **KG-7 lock** (`parser_no_leak` 50-reparse stdlib loop, `build-asan`): LSan-clean.

  Teeth check, not committed: retaining each `CompileResult` with `new lsp::CompileResult( res )` made LSan fail with **35,049,220 bytes leaked in 431,606 allocations**. That is about 700 KB per stdlib context, so the lock would catch a context that is not released. The change was reverted.
- **D-1 neutrality**: see `baseline.md`.
