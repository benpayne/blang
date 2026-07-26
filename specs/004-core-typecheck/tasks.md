# Tasks: Core Type Checking (U4)

- [ ] T001 Baseline: build both configs; run all suites; note counts.
- [ ] T002 Add `typesCompatible(Type*,Type*)` helper in Sema (R3, closed set).
- [ ] T003 Return-path checking in `visitFunction` (FR-001,002,003; R2).
- [ ] T004 Initializer/assignment compatibility in `visitStmt` (FR-004).
- [ ] T005 Delete dropped-initializer `initVal = nullptr` fallback in CGStatements.cpp (FR-009).
- [ ] T006 Call arity + arg-type checking in `visitExpr` CallExpression (FR-005,006; R4).
- [ ] T007 Operand validity for OperationsExpression/UnaryExpression (FR-007; R5).
- [ ] T008 Delete return-fabrication (`getNullValue`/`CreateIntToPtr`) in CGStatements.cpp (FR-009).
- [ ] T009 ICE-harden the CodeGen.cpp expression-dispatch fallback (FR-009; R6).
- [ ] T010 Author test_files/fail/sema/audit_{01,02,03,04,05,10}.b + .expected (FR-010).
- [ ] T011 Migration sweep: fix corpus/stdlib/demos fallout; keep suites green (FR-012).
- [ ] T012 Docs: CLAUDE.md/language_design.md where enforcement is now real.
- [ ] T013 Gate A: `./run_tests.sh && ./test_codegen.sh` exit 0.
- [ ] T014 Gate B: `BUILD_DIR=build-parse ./run_tests.sh` exit 0.
- [ ] T015 Gate D quiet compile + U1 goldens clean; SC-002 grep checks.
