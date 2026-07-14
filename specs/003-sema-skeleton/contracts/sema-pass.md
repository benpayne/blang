# Contract: Semantic Pass (invocation + build modes)

Defines when the sema pass runs and what happens on success/failure. Enforced
by REQ-004 and the Gate A/B suites.

## Invocation

- The driver invokes the sema pass once per parsed source module, **after**
  `Module::Parse` returns a non-null module and **before** any code generation,
  in the same per-module loop in `qcc.cpp`.
- The pass is invoked in **every** build configuration: the LLVM build, the
  non-LLVM (`BLANG_ENABLE_LLVM=OFF`) build, and every `--parse-only` compile.
  It is **not** guarded by `BLANG_HAS_LLVM`.
- Extern-only modules (`.bmod`) are not sema-analyzed as user code (they provide
  types only), consistent with how codegen skips them.

## Success / failure

- On success (no diagnostic reported), compilation proceeds unchanged
  (`--dump-locations`, `--emit-bmod`, codegen, etc. behave as before).
- On failure (≥ 1 located diagnostic reported through the `DiagnosticEngine`),
  the driver exits non-zero and does **not** run code generation for that
  module. No `.ll`/`.o` is emitted for a semantically-rejected module.
- A clean compile remains byte-silent (the pass prints nothing on success) —
  U2 Gate D still holds.

## Scope model

- Sema resolves against the **same** `Scope` the parser used for the module
  (including the `--combine` combined scope, per-module namespace scopes, and
  `.bmod` flat-merged symbols). It does not build a parallel symbol table.

## Verification hooks

- SC-002: the non-LLVM build rejects an unknown-member program with a located
  diagnostic (sema ran without LLVM).
- SC-005: Gate A (`./run_tests.sh && ./test_codegen.sh`) and Gate B
  (`BUILD_DIR=build-parse ./run_tests.sh`) both exit 0.
- SC-007: `out=$(build/qcc --parse-only test_files/pass/func_simple.b 2>&1);
  test -z "$out"` succeeds (quiet preserved).
