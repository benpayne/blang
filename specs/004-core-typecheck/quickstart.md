# Quickstart: Validating U4
Build both configs, then:
- audit_01..05, audit_10 each: `build/qcc --parse-only test_files/fail/sema/audit_0N.b` exits non-zero with a canonical located line; same in build-parse.
- `grep -c CreateIntToPtr CGStatements.cpp` == 0.
- `./run_tests.sh && ./test_codegen.sh` (Gate A) exit 0; `BUILD_DIR=build-parse ./run_tests.sh` (Gate B) exit 0.
- `out=$(build/qcc --parse-only test_files/pass/func_simple.b 2>&1); test -z "$out"`.
