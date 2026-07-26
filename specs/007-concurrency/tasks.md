# Tasks: Concurrency Safety (U7)
- [x] T001 Sema: reject field assignment through a shared value (audit_06).
- [x] T002 Sema: reject unguarded heap capture in spawn (capture analysis; audit_07).
- [x] T003 audit_06.b + audit_07.b + .expected.
- [x] T004 Migrate codegen_shared_lambda.b (shared->sync) and stdlib/net.b spawn.
- [x] T005 Gate A + Gate B green both build modes; 14 demos compile; leak-check 6/6 0 leaks.
- [~] T006 DEFERRED (tracked): sync field RMW lock emission + codegen_sync_field_rmw.b
      + grep __blang_sync_lock proof — codegen change, not an epic done-condition command.
