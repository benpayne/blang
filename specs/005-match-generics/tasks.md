# Tasks: Match & Generics Soundness (U5)
- [x] T001 Sema match-exhaustiveness (enum subject, no wildcard -> cover all variants).
- [x] T002 Sema generic-constraint check (explicit type args, structural conformance).
- [x] T003 audit_08.b + .expected (generic constraint not satisfied).
- [x] T004 audit_09.b + .expected (non-exhaustive match).
- [x] T005 Quiet the stray FileLexer default-case printf behind -v (REQ-003).
- [x] T006 Gate A + Gate B green both build modes; no corpus false positives.
- [~] T007 DEFERRED (tracked): value-producing match codegen + E2E test — sizable
      codegen change, not required by the epic's five done-condition commands.
