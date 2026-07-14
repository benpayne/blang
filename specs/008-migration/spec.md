# Feature Specification: Strict Migration & Negative-Suite Completeness (U8)

**Branch**: `epic/blang-ast/u8-migration` — Unit U8, covers **REQ-013**, **REQ-011** (suite half).

## Scope delivered
- Brings `test_files/fail/sema/` to **26** fixtures (epic done-condition #3:
  >= 25), all ten `audit_01.b`..`audit_10.b` present, adding per-diagnostic
  coverage for the U4-U7 diagnostics that lacked a dedicated fixture:
  argument-type mismatch, argument-count (too few), invalid arithmetic operand,
  struct-from-string initializer, string-from-int return, unknown field, unknown
  method, and a second non-exhaustive match. Each rejects in `--parse-only` in
  both build modes with a canonical `file:line:col: error:` line matching its
  `.expected` pattern.
- Corpus migration to type-correctness under the strict checker was performed
  incrementally within U4-U7 as each check landed (result_option/try_operator
  enum construction; query_basic/join query binding; codegen_shared_lambda
  shared->sync; stdlib/net.b selector spawn), keeping `./run_tests.sh` and
  `./test_codegen.sh` green at every unit boundary (REQ-013).
- Docs: `CLAUDE.md` test-suite counts + fail/sema coverage updated; `cgfail/`
  8 -> 5 (own_* relocated to fail/sema in U6).

## Epic done-condition (verified literally, all five commands)
1. LLVM build: `./run_tests.sh && ./test_codegen.sh` exit 0.
2. Parse-only build: `BUILD_DIR=build-parse ./run_tests.sh` exit 0.
3. `ls test_files/fail/sema/*.b | wc -l` = 26 (>= 25); audit_01..10 present; every
   file rejected in `--parse-only` with the canonical regex + its pattern.
4. `out=$(build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"`.
5. leak-check `codegen_spawn*/sync*/shared*` exit 0, 0 leaks.

## Tracked deferrals (recorded in prior unit specs; NOT done-condition commands)
- Value-producing `match` value codegen + E2E (U5, specs/005-match-generics).
- Explicit sync-field-RMW lock emission + grep proof (U7, specs/007-concurrency).
Both are codegen enhancements outside the epic's five machine-checkable
done-condition commands, which are all satisfied.
