# Data Model: blangd stdlib/prelude context (U2)

All structures live in memory; nothing is persisted, except the committed corpus list. For the exact signatures, see [plan.md](plan.md) D-2/D-3.

## StdlibLocation (`lsp/StdlibLocation.h`)

It is resolved once per session, at `initialize`, and is immutable afterwards.

| Field | Type | Rule |
|---|---|---|
| `route` | `kInitOptions \| kEnv \| kRelative` | This is the **first set** route, in order: `initializationOptions.stdlibPath` (a non-empty string), then `BLANG_STDLIB_DIR` (non-empty), then the relative default. Once a route is selected, no other route is consulted (no fall-through). |
| `path` | `std::string` | This is the route's value as given. For `kRelative` it is `executableDir(argv0) + "/stdlib"`. It is never normalized. |
| `usable` | `bool` | True iff `path` is a readable directory and `<path>/<n>.b` is readable for every `n` in `BlangStdlib::baseModuleNames()`. |
| `missing` | `std::string` | Empty when the location is usable. Otherwise it is `"directory"` or the first absent `"<n>.b"` in base order. It feeds the warning text. |

**State.** `Server::mStdlib` is default-constructed, then set once by `initialize`. A document cannot be compiled before `initialize`; the existing server rejects or drops those requests.

## Document compile plan (built per reparse in `lsp/Compile.cpp`)

| Case | `combine` | `inputs` (in order) | `identity` |
|---|---|---|---|
| `usable` | true | `selectStdlibFiles(path, scanImports(text))` (disk) + `{ docPath, kSource, inMemory, text }` | `kNone` |
| not usable | false | `{ docPath, kSource, inMemory, text }` (today's U1 plan) | `kNone` |

## CompileResult (`lsp/Compile.h`, U1 shape kept)

| Field | Owns | Notes |
|---|---|---|
| `context` (`shared_ptr<CompileContext>`) | every module, the prelude/combine/namespace scopes, and all ASTs, **stdlib included** | This is the KG-7 owner. The user AST's non-owning resolution links point into modules that are owned only here. |
| `module` | the user module (co-owned) | `records().back().module` |
| `fileScope` | the user scope (co-owned) | In combine mode this is the combine scope. It is non-null even on a catastrophic parse. |
| `diagnostics` | — | The engine's list, filtered to `file == docPath` or empty. A not-found warning is prepended when the stdlib is not usable. |

**Lifetime.**
- `Server::mCompiles[uri]` replaces the result on each reparse; the old context is released after the new result is stored.
- `didClose` erases the result.
- All results are released before the process-static `Resolver`.

**Future cache rule (D-4):** any reused stdlib state is held by `shared_ptr`, and every result built against it holds a copy.

## Corpus list (`test_files/lsp/corpus.txt`)

The file has one entry per line, `<corpus> <repo-relative path>`. `#` comments and blank lines are ignored. In U2, `<corpus>` is always `stdlib`; U3 adds `project`.

- **Invariant**, checked by `--check-list`: every qualifying file is listed. The qualifying files are:
  - the six overview files;
  - every `examples/*/main.b`;
  - every `test_files/codegen_*.b` whose reference compile exits 0.
- **Duplicates** are an error.

## Reference compile (computed by the corpus tool, not stored)

It is the `qcc` command line from the build's `bcc -v -S <abs file> -o <tmp>` output, run with `--json`. Its result is the JSON entries `{severity, file, line, col, message, code}`.

The comparison key is `(severity, line, col, message)`, compared as a sorted multiset. The premise is that every entry's `file` equals the given path.
