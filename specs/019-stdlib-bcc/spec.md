# Spec: Stdlib-via-`bcc` integration (+ seeded S2 fix)

**Epic**: functional-hardening · **Unit**: U4 · **Branch**: `epic/functional-hardening/u4-stdlib-bcc`
**Covers**: REQ-004 (stdlib exercised through the real `bcc` driver) + REQ-005 (seeded fix S2: `Map` from the `collections` module usable via `bcc`)
**Speckit**: `stdlib-bcc` · **Status**: Approved (independent spec audit: REQUIRED CHANGES folded in — fs/net tests reframed as driver-integration coverage; fs centered on read_into; deliberate content-grep gate noted)

## Problem

The 2026-07-19 coverage evaluation found the codegen harness (`test_codegen.sh`)
**combines stdlib manually**, so a module like `collections` can "pass" in the
suite while being **unusable through the real `bcc` driver**. The headline case is
the seeded bug **S2**: a program that does `import collections;` and declares a
`Map<string,int>` fails to compile. This unit exercises stdlib **through `bcc`**
(the driver users actually run), fixes S2, and adds behavioral coverage for
`net`/`fs` utility functions that work via `--combine` today but have never been exercised through the real `bcc` driver.

### Grounding investigation (run on this branch's base — real current behavior)

Confirmed by compiling probes through the real driver at `build/bcc` and by
reading `bcc.cpp` / `qcc.cpp`:

- **S2 reproduces.** `import collections;` + `Map<string,int> m = Map<string,int>
  { keys: [], values: [] }` fails at **parse**:
  `error: Failed parse varible` on the `Map<...>` variable declaration. It fails
  the same way through `bcc <file>.b` **and** through `qcc --combine
  stdlib/collections.b <file>.b`.
- **`collections.b` IS already combined by `bcc`'s main compile path.** `bcc -v`
  shows `qcc --combine .../sys.b .../buffer.b .../fs.b .../net.b
  .../collections.b <file>` when the program `import collections;` — so the
  driver's main-path stdlib resolution (`resolveStdlibFiles`, `bcc.cpp:~792`,
  used at `bcc.cpp:1388` for single-file compile) already pulls in
  `collections.b` on import. **So "collections missing at `bcc.cpp:279`" is NOT
  the main-path cause** — line 279 is the separate **`bcc test`** path, which
  hardcodes `{sys,buffer,fs,net}` and omits collections (a completeness gap for
  `bcc test`, addressed below, but not the S2 compile failure).
- **Root cause of S2 is in `qcc.cpp` combine-mode scoping.** In `--combine`, each
  non-user stdlib file is parsed into its **own namespace scope**
  (`kScope_Namespace`, `qcc.cpp:~780-788`) whose parent is `combineScope`; the
  user's file (last) parses directly into `combineScope`. `buffer` is
  **special-cased** (`isBufferLib`, `qcc.cpp:~776`) to parse into `combineScope`
  directly, which is why `Buffer` is globally visible. `collections` is **not**
  special-cased, so its `Map` **type** lands in the collections namespace and is
  **not visible unqualified** when the user file parses `Map<...> m` → the parser
  doesn't recognize `Map` as a type → `Failed parse varible`.
- **Confirmed by construction:** copying `collections.b` to a file named
  `buffer.b` (hijacking the `isBufferLib` path) makes `Map<string,int> m` parse
  cleanly and **monomorphize** to `%Map_string_int` in the emitted IR. So the fix
  is **type visibility of combined-module public types**, not a codegen bug.
- **`codegen_map.b` passes because it defines `Map` INLINE** in the same file
  (into `combineScope`/`gScope` directly) — it never exercises the cross-module
  path. That is exactly the blind spot D4 describes.
- **`fs`/`net` utilities work through `bcc` today** when the program `import`s the
  module and calls **namespace-qualified**: `import fs; fs.write_all(p, s);
  fs.read_all(p)` and `import net; net.http_status_text(200)` both compile and run
  through `bcc`. (Free functions stay namespace-qualified; only *types* need the
  unqualified-visibility fix.) **Driver-integration gap (the real novelty of the
  fs/net tests):** these `fs` File methods (`read_into`/`read_line`/`seek`/`tell`/
  `size`) and `net` pure helpers (`build_http_response`/`parse_http_request_line`/
  `dispatch_request`/`http_status_text`) ARE covered functionally by existing
  `--combine` codegen tests (`codegen_file_io.b`, `codegen_http_blang.b`,
  `codegen_http_routing.b`), but **`test_codegen.sh` never invokes `bcc`** — it
  calls `qcc --combine` directly — so none of them has ever been exercised through
  the real `bcc` driver. "Compiles+runs through `bcc`" is therefore the genuinely
  new signal (D4), and it is the fs/net tests' reason to exist. The one `fs`
  method with **no** existing codegen coverage at all is `read_into`, which the fs
  test centers on.

## Scope

**In scope**
- **Fix S2** so `import collections;` + `Map<K,V>` compiles and runs through
  `bcc`. The fix makes a combined stdlib module's **public struct/enum type
  names** visible in `combineScope` (unqualified), mirroring the existing
  `buffer` special-case and the `.bmod` flat-merge that already registers struct
  names as types (`qcc.cpp:~822-831`). Functions remain namespace-qualified
  (preserving the `fs.`/`net.` convention). Lands in `qcc.cpp` (combine-mode
  scoping) — **does NOT touch `CGStruct.cpp`** (no soft-conflict with U1/U3).
- **Wire `collections` into the `bcc test` stdlib path** (`bcc.cpp:~279`) so
  `bcc test` on a file importing collections also resolves `Map` — gated on the
  file's imports (mirroring the main path), for completeness. (The primary S2
  gate is the main `bcc` compile path, which already includes collections.)
- **Teach `test_codegen.sh` to combine `collections.b` when a test file contains
  `import collections;`** (grep-gated, mirroring the existing filename-substring
  gates for `net`/`fs`/`timer`). This is faithful to `bcc` (identical
  `qcc --combine` invocation) and does **not** conflict with the inline
  `codegen_map.b` (which does not import collections, so it never gets a second
  `Map` definition).
- **3 behavioral tests** (each a committed `codegen_*.b` with a stdout golden),
  all compiled+run through `bcc` in the acceptance block (REQ-004 "through the
  real driver"):
  - `codegen_bcc_collections_map.b` — S2 regression (Map via `import
    collections;`).
  - `codegen_bcc_fs_utils.b` — `fs` File methods through the `bcc` driver (centered on `read_into`, the one method with no existing codegen coverage).
  - `codegen_bcc_net_utils.b` — `net` pure HTTP helpers through the `bcc` driver.

**Out of scope**
- Operator / ARC / interaction matrices (U1/U2/U3).
- Live network/socket behavior (non-deterministic — quarantined territory); the
  `net` tests use only the **pure** request/response helpers (no socket).
- Unqualified free-function imports (`import fs; write_all(...)` without `fs.`) —
  that is a broader import-semantics change, not required; the `fs.`/`net.`
  qualified convention is the supported form and is preserved.
- New stdlib features; the PostgreSQL/db path.

## Named test cases (the matrix)

Each test asserts concrete values internally (binary exits 0 only when correct)
AND prints a deterministic line sequence captured by a committed
`<name>.expected.out` golden. All are deterministic (file I/O to `/tmp`, pure
string helpers — no sockets/threads), so none is quarantined.

| # | File | Shape covered | Key assertions / golden content |
|---|------|---------------|---------------------------------|
| 1 | `codegen_bcc_collections_map.b` | **S2 regression.** `import collections;` then `Map<string,int>` full-literal init, `set`/`get`/`has`/`length`/`remove` — the exact cross-module path that fails today | `set("a",1)`,`set("b",2)`; `get("a")==1`, `get("b")==2`; `has("a")==true`, `has("z")==false`; `length()==2`; after `remove("a")`, `length()==1`, `has("a")==false`. Golden prints each. **Fails pre-fix** (`Failed parse varible`), **passes post-fix**. Also uses a `Map<string, Point>` (struct value) briefly to confirm a refcounted value type flows through the module `Map` (light ARC touch; not the ARC matrix). |
| 2 | `codegen_bcc_fs_utils.b` | `import fs;` File methods **through the `bcc` driver** (novelty is the driver path — `test_codegen.sh` never runs `bcc`; `read_into` also has no existing codegen coverage) — open a `/tmp` file, `write`, `seek(0)`/`tell()`, `read_into`/`read_line`, `size()`, `close`; then `fs.remove` | deterministic golden of the read-back lines + sizes/offsets; asserts each. Uses a fixed `/tmp/blang_u4_fs_*.txt` path. |
| 3 | `codegen_bcc_net_utils.b` | `import net;` **pure** HTTP helpers (no socket) **through the `bcc` driver** (novelty is the driver path; these helpers are covered by `codegen_http_blang.b`/`codegen_http_routing.b` via `--combine` but never via `bcc`) — `net.http_status_text(200/404/500)`, `net.build_http_response(...)`, `net.parse_http_request_line("GET /path HTTP/1.1")` into its fields, and `net.dispatch_request` against a small route table returning a matched response and a 404 | deterministic golden of status texts, the built response's status line, the parsed method/path, and the dispatch results; asserts each |

That is **3 `codegen_*.b`** tests toward the epic's ≥ 20 target. (Running epic
total after U1+U2+U4: 93 + ~6 + 3 ≈ 102; U3 adds the remainder to clear ≥ 105.)

## The S2 fix (implementation sketch — reviewer confirms at code audit)

In `qcc.cpp` combine-mode, when a non-user stdlib module is parsed into its
namespace scope, **also register its `pub` struct/enum type names as visible
types in `combineScope`** so unqualified references (`Map<...>`) resolve in the
user file — mirroring:
- the `isBufferLib` special-case that already routes `buffer` into `combineScope`
  (precedent for "module defines a globally-visible fundamental type"), and
- the `.bmod` flat-merge (`qcc.cpp:~822-831`) that already does
  `gScope->addType(new Type(s->getName()))` for public structs/enums.

Minimal viable form: after parsing each combined non-user module, iterate its
public `StructDefinition`/`EnumDefinition` symbols and add their type names (and
the definitions themselves, so method/monomorphization lookup works) into
`combineScope`. Keep the module's **functions** in the namespace scope
(preserving `fs.`/`net.` qualification and avoiding free-function collisions).
This is additive and low-risk: `collections.b` defines no free functions, only
`Map` + its `impl`, so there is nothing to collide. Verified end-to-end by
compiling+running `codegen_bcc_collections_map.b` through `bcc` under
`--leak-check`-clean discipline (Map holds refcounted string keys).

If, during implementation, the visibility fix proves insufficient for
`Map<_, struct>` monomorphization from a module (a possible second layer), that
`Map<_,struct>` case is the *only* candidate for fix-or-file; the core
`Map<string,int>` S2 gate must still pass (S2 may never be filed).

## Fix-or-file policy (this unit)

Any stdlib bug a test surfaces beyond S2 is, in order of preference:
1. **Fixed** — the test passes, committed into the suite; or
2. **Filed** — a structured `### KI-N` entry in
   `docs/epics/functional-hardening/known-issues.md` (fenced `Repro:` block +
   `Justification:` line); the failing test is NOT committed passing. Filing is
   only for large/risky/language-decision fixes and is raised to the manager as
   an Open Question first. Global cap: ≤ 3 `### KI-` entries across the epic.
   **S2 may never be filed.**

## Acceptance (this unit — reviewer re-runs independently)

```bash
# builds clean, both modes
cmake --build build -j"$(nproc)"
cmake --build build-parse -j"$(nproc)"

# suites green, both modes
./run_tests.sh && BUILD_DIR=build-parse ./run_tests.sh
./test_codegen.sh                      # all pass, incl. the 3 new codegen_bcc_*.b tests w/ goldens
ctest --test-dir build                 # runtime units stay green

# S2 fixed THROUGH THE REAL DRIVER (bcc), per the epic acceptance
printf 'import collections;\nfn main() -> int { Map<string,int> m = Map<string,int> { keys: [], values: [] }; m.set("a", 1); assert m.get("a") == 1, "map"; return 0; }\n' > /tmp/mapchk.b
bcc /tmp/mapchk.b -o /tmp/mapchk && /tmp/mapchk

# the 3 U4 tests also compile+run through bcc directly (not only the harness)
for t in codegen_bcc_collections_map codegen_bcc_fs_utils codegen_bcc_net_utils; do
  bcc test_files/$t.b -o /tmp/$t && /tmp/$t >/dev/null
done

# S2 is NOT filed
! grep -qi 'collections\|Map via bcc\|Map from the collections' docs/epics/functional-hardening/known-issues.md 2>/dev/null

# fix-or-file bounded
ki=$(grep -c '^### KI-' docs/epics/functional-hardening/known-issues.md 2>/dev/null || echo 0); test "$ki" -le 3
```

## Success criteria

- **SC-001**: `bcc` compiles+runs a `Map` program via `import collections;` (S2
  fixed); the committed `codegen_bcc_collections_map.b` fails pre-fix and passes
  post-fix, with a golden.
- **SC-002**: `codegen_bcc_fs_utils.b` and `codegen_bcc_net_utils.b` pass with
  goldens and exercise `fs`/`net` helpers **through the real `bcc` driver** (the
  new signal — `test_codegen.sh` never invokes `bcc`); `read_into` additionally
  has no prior codegen coverage.
- **SC-003**: S2 does not appear in `known-issues.md`; fix-or-file bounded (≤ 3).
- **SC-004**: both `run_tests.sh` modes, `test_codegen.sh`, and `ctest` stay
  green; `codegen_*.b` count increases by 3.

## Open Questions (raised to the manager before/at implementation)

- **OQ-U4-1**: The S2 root cause is `qcc.cpp` combine-mode namespace scoping (a
  combined module's public *types* aren't visible unqualified), **not** the
  `bcc.cpp:279` wiring the design doc anticipated (the main compile path already
  combines `collections.b` on import). Confirming the fix target is `qcc.cpp`
  type-visibility (buffer-precedent), with `bcc.cpp:279` handled as a smaller
  `bcc test`-path completeness item. No decision needed unless the manager wants
  a different scoping model; proceeding with the additive type-visibility fix.

## Assumptions

- Existing `test_codegen.sh` golden machinery is reused; the only harness change
  is an additive `import collections;` gate mirroring the net/fs gates (necessary
  because the seeded module is collections).
- `fs.`/`net.` qualified calls are the supported form (unqualified free-function
  import is out of scope).
- The S2 fix is additive/correctness-only; default build output for existing
  tests is unchanged, and inline `codegen_map.b` is untouched.
```
