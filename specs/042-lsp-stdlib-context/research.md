# Research: blangd stdlib/prelude context + stdlib discovery (U2)

All measurements were taken on branch `042-lsp-stdlib-context` @ `3b093f6`, which is U1's `5d9b7b6` plus the spec. The binaries are from `build/` (LLVM). Every Technical Context unknown is resolved here; none remain.

## R1. How blangd gets `bcc file.b`'s context without copying a rule

- **Decision.** blangd builds a combine-mode `CompilePlan` in two steps:
  - the stdlib inputs are `BlangStdlib::selectStdlibFiles( dir, BlangStdlib::scanImports( text ) )`;
  - the document is the last input, `inMemory = true`, with `identity = kNone`.

  blangd then calls `ProjectCompile::run`. That is the plan `bcc <file>` hands `qcc` (`bcc.cpp` `main` → `selectStdlibFiles( exeDir + "/stdlib", scanImports( readFileToString( file ) ) )`, then `qcc --combine <stdlib...> <file>`).
- **Rationale.** Every rule (selection order, routing, prelude LOAD/PROMOTE, the import pass, Sema) already lives in the U1 shared units, so blangd only assembles inputs (design D1, overview constraint). The one input-side difference is that blangd scans the editor buffer, not the disk file (spec FR-002). `scanImports` takes text since U1 for exactly this reason.
- **Alternatives rejected.**
  - Shelling out to `qcc`: a subprocess, which violates C4, and it loses the AST.
  - Re-deriving the stdlib list in `lsp/`: a rule copy.

## R2. Is full-codegen reference parity reachable by a parse+Sema-only server?

- **Finding.** For every `test_files/codegen_*.b` and `examples/*/main.b` (178 files):
  - I ran the exact `qcc` line `bcc -v -S` prints;
  - I re-ran the same line with `-S`/`-o` replaced by `--parse-only`.

  177 files exit 0, and for **all 177 the stderr is byte-identical** between the two. 28 of those 177 carry warnings, so warning parity is really exercised. One file, `codegen_nsprefix_arc.b`, exits 1 under `bcc -v -S` (it needs `nsarc.b`, which only `test_codegen.sh` adds), so it is not a corpus member.
- **Decision.** The corpus check runs the printed reference line as printed (spec Assumptions). It needs no codegen-diagnostic carve-out, and any code-generation-only diagnostic fails the check loudly.

## R3. Which existing LSP goldens change?

I compiled each of the five `test_files/lsp/src/*.b` fixture sources, and the two inline `didChange` texts, both single-file and combined with the base stdlib. The diagnostics are **identical** in every case. None of them has `import` lines, so the now-authoritative import pass adds nothing.

Navigation goldens (`defn_*`, `hover_*`, `symbols_full`) walk the user module only, which is unchanged. **Expected golden diffs: zero.** FR-015's justification rule stays as the safety net.

## R4. Which `fail/sema` fixtures change under stdlib context?

I ran all 58 `test_files/fail/sema/*.b` fixtures through `qcc --parse-only --combine <base stdlib + import-gated> <fixture>`, judged by the LSP leg's own rule: a non-zero exit, a canonical error line, and the expected ERE. **Exactly one** changes: `buffer_without_prelude.b` becomes clean (exit 0), which is correct, since the prelude supplies `Buffer`. The other 57 still reject with their pattern.

This confirms the spec's FR-016 routing. That fixture runs through the stdlib-not-found route; the rest run in stdlib context.

## R5. Latency budget

`qcc --parse-only --combine <5 base modules + env> examples/todo_app/main.b` averages **~0.09 s per whole process**, measured as 10 runs in 0.915 s wall. That includes process start and the global-scope build. blangd does the same work minus process start.

**Decision:** a full recompile per `didChange` with no stdlib cache (design D5). The 500 ms median has more than 5× headroom. FR-012's shared-ownership rule is still written down (D-4) for any future cache. The sequencing notes require raising a question before adding one.

## R6. Executable directory for the blangd-relative route

- **Finding.** `bcc` already has `getExeDir( argv0 )` (`bcc.cpp`, static): it reads `readlink( "/proc/self/exe" )` and falls back to `argv0`'s directory, then `"."`. The `qcc` lookup and the stdlib lookup both key off it. `build/blangd` sits next to `build/stdlib/`, which CMake copies.
- **Decision.** Move it **verbatim** into the dependency-free `StdlibSelect` unit as `BlangStdlib::executableDir( argv0 )`, and have `bcc` call it. This is behavior-neutral (same function, same callers) and follows design D6, "share the helpers rather than copy them". `blangd_main.cpp` changes to `main( int argc, char *argv[] )` and passes `argv[0]`.
- `readlink` is a filesystem read, not a process. Neither the U1 binary check nor C4 is affected.
- **Alternatives rejected.**
  - A second exe-dir helper in `lsp/`: a copy that would drift.
  - `realpath( argv[0] )` only: this fails when blangd is started via `PATH`.

## R7. The base-module list behind the "usable" test

- **Decision.** The five always-combined names live in exactly one place. `StdlibSelect` exposes `BlangStdlib::baseModuleNames()` (`sys, buffer, collections, fs, net`), and `selectStdlibFiles` iterates it instead of its inline initializer list. This is a neutral refactor: same names, same order.
- blangd's usability check (FR-007) and the selection both read that list, so "usable" can never disagree with "what `bcc` always combines".

## R8. Stdlib-not-found diagnostic: text and placement

- **Decision.** Severity **Warning**, at 1:1 of the document (LSP 0:0), code slug `stdlib-not-found`. It is the **first** diagnostic in every publish while the selected route is unusable. The message is fixed-form; see [contracts/stdlib-discovery.md](contracts/stdlib-discovery.md):

  ```text
  stdlib not found: <route> '<path>' is not a usable stdlib directory (missing <name>.b); set initializationOptions.stdlibPath or BLANG_STDLIB_DIR, or install stdlib/ next to blangd — this file is checked without the stdlib
  ```

  `<route>` is one of `initializationOptions.stdlibPath`, `BLANG_STDLIB_DIR` or `blangd-relative`. `<name>` is the first base module, in base order, that is absent; `<path>` itself when the directory is unreadable or not a directory (`missing directory`).
- **Rationale.** Deterministic, so it can be pinned by a golden. It names the route and path (spec FR-008). It is first so that it survives any client-side truncation.
- **Alternatives rejected.** Listing every missing file: a longer, unstable message with no extra guidance.

## R9. Transcript harness support for the routes

- **Finding.** `tools/lsp_client.py` spawns the server in `Driver.__init__`, before reading the script, and inherits the caller's environment. That means a developer's own `BLANG_STDLIB_DIR` would silently change every golden.
- **Decision.**
  1. The driver **always** removes `BLANG_STDLIB_DIR` from the server environment.
  2. A fixture may begin with a `{"server_env": {"NAME": "value"}}` step, `${ROOT}`-substituted, which the driver reads **before** spawning. Parsing the whole script before spawn is a pure reorder of an already-sequential loop.
  3. `--sema-fixture` gains `--stdlib-path DIR`, sent as `initializationOptions.stdlibPath`, for FR-016.

  All of this lives in the shared driver (spec FR-014).
- **Making routes observable.** Success transcripts all look the same (an empty diagnostic list), so the goldens prove precedence through the no-fall-through rule. A higher-precedence route pointed at an **unusable** directory publishes a warning naming *that* route, even though a lower route is valid. The unusable directory is `${ROOT}/test_files/lsp/src`: it exists and has no `sys.b`.

## R10. Corpus tool mechanics

- **Reference command.** Run `<BUILD_DIR>/bcc -v -S <abs file> -o <tmp>.ll` in a temp cwd. Take the first output line that contains `/qcc ` and split it with `shlex.split` (the repo paths have no shell metacharacters; the tool fails loudly otherwise). Execute it with `--json` appended.
- **Why `--json`.** It is a rendering-only flag: U1's `project_compile` C1/C7 assert the human and JSON renderings carry the same diagnostic set. It removes any parsing ambiguity in messages that contain `:`.
- **Comparison.** For each corpus file, compare the reference JSON entries whose `file` equals the given path against blangd's `publishDiagnostics` for the document, mapping 0-based to 1-based and severity 1/2 to error/warning. Compare **sorted multisets** of `(severity, line, col, message)`. Neither side's order is part of the contract.

  The premise check comes first: the reference must contain **no** entry whose `file` differs from the document. That is the FR-004 premise, and it would expose a broken stdlib instead of hiding it.
- **Completeness check.** `--check-list` computes the qualifying set: the six overview files, `examples/*/main.b`, and every `test_files/codegen_*.b` whose reference exits 0. It fails if any qualifying file is missing from the committed list. `test_lsp.sh` runs `--corpus stdlib --check-list`.
- **One server for the whole corpus.** The tool opens each corpus file in one blangd session (didOpen, wait for the publish, didClose), which keeps the run time ~ N × 0.1 s. The reference compiles dominate the cost, at an estimated ~1–2 min total.

## R11. Latency tool mechanics

- **Method.** Use one blangd session per file. didOpen the file, wait for its publish, then send 20 `didChange` full-text edits, each appending `// edit <n>` on a new line (still valid source). Time each edit from send to the matching `publishDiagnostics` (same uri), measured with `time.monotonic()`.
- **Output.** Print `<file>: median <ms> ms (n=20)`. Exit 1 if any file's median is > 500 ms.
- **Sanitizer builds.** `test_lsp.sh` detects one from `<BUILD_DIR>/CMakeCache.txt` (`BLANG_SANITIZE` non-empty) and prints `SKIP: latency leg (sanitizer build)`.

## R12. Display names (design D4)

U2 adds **no** new rendering of a foreign type or module name. Every diagnostic blangd publishes is produced by the shared frontend, with the same text `qcc` prints (C1). The only new text is the fixed stdlib-not-found message (R8). D4's rule stays U4's to confirm, and is neither touched nor pre-empted here.

## R13. The corpus check in a parse-only build (CI's `lsp` job)

- **Finding.** CI's `lsp` job configures with `-DBLANG_ENABLE_LLVM=OFF` and builds only the `blangd` and `jsonTest` targets.

  A parse-only `bcc -v -S <file>` still prints the reference `qcc` line, and that build's `qcc` accepts `-S`/`-o`. It ignores emit flags without LLVM and runs parse + Sema. CMake copies `stdlib/*.b` into `<build>/stdlib/` at configure time.

  I compared the LLVM `build/` against `build-nollvm/` over all 178 candidates, using the same stdlib paths for both. Each build's `qcc` was run on the line its own `bcc -v -S` printed. **The exit code and stderr were byte-identical for all 178.**
- **Decision.**
  - The corpus leg runs in both build modes, with no SKIP.
  - The `lsp` CI job's build step adds the `qcc bcc` targets.
  - A missing `bcc` or `qcc` is a hard FAIL of the leg, never a silent skip.
