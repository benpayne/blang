# Tasks: Ownership & Move Analysis (U6)
- [x] T001 Sema bounded move analysis: moved-set, loop/spawn depth, reassign-clears.
- [x] T002 Move on init (own Y = X) and on call (own param); use-after-move error.
- [x] T003 Move-in-loop + own-capture-in-spawn located errors.
- [x] T004 Remove codegen use-after-move rejection (sema authoritative).
- [x] T005 Relocate own_use_after_move/move_in_loop/spawn_capture to fail/sema + .expected.
- [x] T006 own_indirect_move.b (reject) + own_reassign_after_move.b (accept).
- [x] T007 Gate A + Gate B green; leak-check 6/6 0 leaks; goldens clean.
