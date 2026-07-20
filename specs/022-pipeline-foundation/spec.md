# Spec: Pipeline foundation — consolidate bcc's llc/link duplication + flag-threading spine

**Epic**: 001-toolchain-and-stdlib · **Unit**: U0 · **Branch**: `epic/001-toolchain-and-stdlib/u0-pipeline-foundation`
**Covers**: REQ-001 (pipeline foundation everything else builds on)
**Speckit**: `pipeline-foundation` · **Status**: Audited (spec audit passed — architect)
**Depends on**: none (first unit)
**Reviewed-by: architect** (Vera, 2026-07-19) — Verdict PASS-WITH-FINDINGS; recon
verified byte-accurate (4 llc blocks, 3 link sites, lib-sets); 0 blocking findings;
3 non-blocking recommendations (Rec-1 two-site `-g` seam, Rec-2 concrete combined
fixture `test_build/timerapp`, Rec-3 grep-teeth single-site) all folded in.

## Problem

`bcc` drives the toolchain over a CLI + textual-`.ll` boundary:
`bcc → qcc (parse→Sema→IR, prints .ll) → llc -filetype=obj → cc`. Two structural
facts (verified by grep on `master` @ ecd39ce) make every later unit
(U1 `--json`, U2 `-O`/`--target`, U3 `-g`, U4/U5 stdlib link) dangerous to add
naively, because each new flag or lib would have to be edited into 3–4 copies:

1. **The `llc`-invocation + object-emission logic is duplicated 4×** in `bcc.cpp`:
   - `bcc.cpp:328–363` — the `bcc test` path (`runTestFile`)
   - `bcc.cpp:1018–1055` — the lib-build path (emits `.o`, then `ar`-archives; no link)
   - `bcc.cpp:1140–1204` — the `bcc build` combined/per-source path
   - `bcc.cpp:1478–1513` — the single-file `bcc hello.b` path (the main user path)

   Each independently: (a) rediscovers the `llc` tool
   (`#ifdef BCC_LLC_PATH … findTool("llc-18",{"llc"})`), (b) builds the same
   `{ llc, "-filetype=obj", "--relocation-model=pic" }` vector, and (c) appends
   the **host-baked** triple via `#if defined(PLATFORM_DARWIN)/… -mtriple=<arch>-…`.
   The triple is the exact seam U2's `--target` must promote to a runtime flag —
   in **one** place, not four.

2. **The runtime-link library list is triplicated** across the three linking
   paths (the lib-build path archives instead of linking):
   - `bcc.cpp:394–442` — `bcc test`: `[testrunner, sys, fs, net, json, buffer, array, string, runtime]` (**no** db)
   - `bcc.cpp:1225–1286` — combined build: `[db, sys, fs, net, json, buffer, array, string, runtime]`
   - `bcc.cpp:1546–1614` — single-file: `[db, sys, fs, net, json, buffer, array, string, runtime]`

   Each repeats the `findLib`/`findBuildLib` baked-vs-fallback lambda, the nine
   `#ifdef BCC_*_LIB` `const char*` decls, and the ordered push loop. A new
   stdlib `.a` (U4 `blang_math/time/random/env`, U5 hash helper) must be added to
   **all three** or it silently fails to link in one path — exactly the trap
   `design-stdlib.md` step 3 calls out.

## Scope

**In scope — a behavior-preserving refactor, no user-visible change:**

1. **One object-emission helper.** Add a single free function in `bcc.cpp`, e.g.

   ```cpp
   // Resolve the llc tool once (baked path, then PATH). Empty on failure.
   static std::string resolveLlc();
   // Emit an object from a textual .ll. Owns the flag vector + host triple.
   // Returns llc's exit code (0 on success). U2 extends this ONE site with
   // -O<n> and a runtime --target triple; U3 with -g debug-metadata preservation.
   static int emitObject( const std::string &llc, const std::string &llFile,
                          const std::string &objFile, bool verbose );
   ```

   Replace the 4 duplicated llc blocks with `resolveLlc()` + `emitObject(...)`
   calls. Per-path error messages and IR-file cleanup stay at the call sites
   (they differ by path); only the tool discovery + command construction move
   into the helper. The host-triple `#ifdef` block lives in exactly one place.

2. **One runtime-link helper.** Add a single function that appends the ordered
   `libblang_*.a` list to a link command, e.g.

   ```cpp
   struct RuntimeLinkProfile { bool withTestRunner; bool withDb; };
   // Appends resolved libblang_*.a in dependency order (dependents before deps).
   // The ONE place U4/U5 add a new stdlib .a.
   static void appendRuntimeLibs( std::vector<std::string> &cmd,
                                  const std::string &exeDir,
                                  const RuntimeLinkProfile &profile );
   ```

   - `bcc test` calls it with `{ withTestRunner=true, withDb=false }` →
     `[testrunner, sys, fs, net, json, buffer, array, string, runtime]`.
   - combined + single-file call it with `{ withTestRunner=false, withDb=true }` →
     `[db, sys, fs, net, json, buffer, array, string, runtime]`.

   The helper appends **only** the `libblang_*.a` list. The path-specific tail —
   `BCC_DB_LINKFLAGS`, `-lpthread`, `-o <out>`, user `linkerFlags`, `-luv` — stays
   at the call sites, preserving each path's exact argument order **byte-for-byte**.

3. **Flag-threading spine (establish, don't extend).** Confirm/annotate the two
   extension points so U1–U5 add a flag in one site each:
   - the `bcc` `Options` struct (`bcc.cpp:40–47`) — where U1/U2/U3 add
     `jsonDiagnostics` / `optLevel`/`release`/`targetTriple` / `debugInfo`;
   - the `qcc` arg loop (`qcc.cpp:505+`) — where the matching qcc flags are parsed.
   U0 adds **no new flags or options** — it only makes the seam single-sited and
   documents (in code comments) where each future flag hooks in.

   **Note (architect Rec-1): U3's `-g` is a two-site seam, not a one-helper hook.**
   Debug info must both (a) be emitted by qcc into the textual `.ll` (qcc emit
   side, reached via the qcc arg loop) **and** (b) survive/pass through the `llc`
   step in `emitObject` (llc side). Scope-1's `emitObject` comment names only the
   llc side; the qcc-emit side is the arg-loop seam here in Scope 3. A later
   reviewer must not treat `emitObject` as U3's sole hook.

**Out of scope (belongs to later units, must NOT appear in U0):**
- Any new CLI flag, option, or behavior (`--json`, `-O`, `-g`, `--release`,
  `--target`, new stdlib libs). U0 is a pure no-op refactor.
- Changing lib order, the lib set per path, the triple value, or any generated
  `.ll`/`.o`/link command for existing inputs.
- Touching the lib-build `ar`-archive tail or `qcc`'s IR emission.

## Requirements traceability

| REQ | Covered by |
|-----|-----------|
| REQ-001 (consolidate the 4 llc + 3 link blocks into one; establish the flag-threading path U1–U5 each extend in one site) | Scope 1 (emitObject/resolveLlc), Scope 2 (appendRuntimeLibs), Scope 3 (Options + qcc arg-loop seam) |

## Test plan / done condition

This is a **pure refactor** (Constitution Principle II carve-out for U0 per
`evaluation.md`: no new language feature → no new `codegen_*.b`). It is gated by
*byte-identical behavior* + the duplication being gone:

1. **Duplication gone (grep-checkable):** the `-filetype=obj` object-emission
   command vector is constructed in exactly **one** place (the helper); the
   `-mtriple=` host-triple `#ifdef` block appears **once**; the nine
   `#ifdef BCC_*_LIB` baked-lib decls appear **once**. A `grep -c` of the old
   duplicated patterns drops from 4→1 (llc) and 3→1 (link).
2. **Byte-identical `.ll`/`.o`/link behavior:** for the single-file path
   (`demos/01_fibonacci.b`) **and** the `bcc build` combined path (the committed
   `test_build/timerapp` project, which imports stdlib `timer` and exercises the
   `--combine` `.o`-naming + db-bearing link profile — architect Rec-2), the
   generated `.ll`, the emitted `.o` (compared via `llvm-objdump -d` / `cmp`), and
   the `bcc -v` llc + link command lines are **identical** pre- vs post-refactor.
   The refactor moves no argument and changes no order. (BEFORE artifacts for the
   single-file path are captured on-branch: llc + link command lines and the `.o`
   objdump.)
3. **Suites green, unchanged counts:** `./run_tests.sh` (LLVM) 195/0 and
   parse-only 195/0; `./test_codegen.sh` 107/0 with all goldens matching;
   `ctest --test-dir build` unchanged; `./test_codegen.sh --leak-check` 0 leaks.
   (Baseline captured on branch: run_tests 195/195, codegen 107/107.)
4. **`bcc test`, `bcc build`, single-file, and `-c`/`-S` all still work:** the
   four consolidated paths each exercised (test_codegen drives single-file +
   combined; a `bcc test` fixture and a `bcc build` project exercise the others).
5. **The seam is single-sited (grep-teeth — architect Rec-3):** after the
   refactor, `blang_testrunner` appears in exactly **one** link-list site and
   `blang_db` in exactly **one** (today they are spread across the 1 and 2 link
   sites respectively); the `-filetype=obj` command vector and the `-mtriple=`
   host-triple `#ifdef` block each appear exactly **once**. This converts the
   single-site invariant into a `grep -c` check — adding a hypothetical flag to
   `Options` + qcc arg loop, or a hypothetical `.a` to `appendRuntimeLibs`, would
   touch exactly one site (the architect's coherence invariant for U1–U5).

## Risks

- **Silent behavior drift in one of the 4 paths** (the whole point of the unit —
  the paths look identical but differ in lib set/tail). Mitigated by: keeping the
  path-specific tail at call sites (helper covers only the identical core),
  encoding the two lib profiles explicitly, and the byte-identical `.o`/link-line
  diff in done-condition #2 across single-file **and** combined paths, plus the
  `bcc test` and `bcc build` fixtures.
- **Lib-order regression** breaking the link (order is load-bearing: dependents
  before deps). Mitigated: `appendRuntimeLibs` reproduces the exact existing
  order; the full `test_codegen.sh` suite (which links every runtime lib) is the
  guard.
- **Over-consolidation** folding path-specific tails into the helper and changing
  argument order. Mitigated by the explicit "helper appends only libblang_*.a"
  boundary in Scope 2 and the byte-identical link-line check.
