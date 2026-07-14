# Data Model (U4)
No new entities. Reuses U3's typed-AST resolved-type slot and the Scope/Symbol
model. Adds Sema-internal helpers (typesCompatible, return-path analysis). New
fixtures under test_files/fail/sema/ (audit_01..05, audit_10) + .expected.
