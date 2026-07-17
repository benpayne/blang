# Parked negative/positive fixtures (feature-integration epic)

These origin/master test fixtures exercise features whose codegen has **not yet
been ported** from origin's monolithic `CodeGen.cpp` into local's `CG*`/`Sema`
architecture. `run_tests.sh` does **not** scan `test_files/parked/`, so these are
excluded until their owning unit lands.

Each owning unit **restores** its fixture into the correct active category as it
ports the feature (per `design.md` §"Conflict map"). By epic end (U8), this
directory is **empty** — mirroring the `codegen_parked.txt` burn-down.

| Fixture | Owning unit | Destination when ported | Status |
|---------|-------------|-------------------------|--------|
| ~~`builtin_option_result.b`~~ | U4 (Option/Result) | `test_files/pass/` (built-in Option/Result parses+checks) | **DONE (U4)** — restored to `pass/` |
| `chan_recv_non_exhaustive.b`  | U2 (channels)      | `test_files/fail/sema/` + `.b.expected` (Sema rejects, all modes) | pending U2 |
| `query_bad_field.b`           | U5 (database)      | `test_files/fail/sema/` + `.b.expected` (if it becomes a Sema-mode rejection) | pending U5 |
| `to_json_not_annotated.b`     | U6 (to_json/http)  | `test_files/fail/sema/` + `.b.expected` (if it becomes a Sema-mode rejection) | pending U6 |

**U4 completed** its relocations: origin's `cgfail/match_non_exhaustive.b` and
`cgfail/builtin_option_non_exhaustive.b` (which local's `Sema` rejects once
built-in `Option` is registered) moved to `fail/sema/` with `.b.expected`
patterns, and `builtin_option_result.b` restored to `pass/`.

## Deferred stdlib: `stdlib/net.b` HTTP routing (U6)

The base merge auto-merged origin's HTTP route-table code (`HttpServer.get/.post`,
`dispatch_request`, `Route`, builtin `to_json` responses) into `stdlib/net.b`, but
that code depends on parked codegen (built-in `Option`/`to_json`) and failed to
parse under local's compiler (`net.b:494: Unexpected token`), breaking every
`net`/`http`/`selector`/`tcp` codegen test. For U1, `stdlib/net.b` was restored to
local's HEAD version (local's BLang-native HTTP utilities). **U6** re-integrates
origin's HTTP routing from `git show origin/master:stdlib/net.b` once builtin
`to_json` and the routing codegen land.

