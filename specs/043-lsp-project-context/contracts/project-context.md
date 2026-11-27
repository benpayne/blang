# Contract: blangd project context

## Discovery and membership

1. `root = findProjectRoot( dirname( docPath ) )`. With no root, the U2 single-file context applies.
2. If `realpath(docPath)` is not among `discoverSourceFiles(root)`, the U2 single-file context applies (R5).
3. Otherwise the document is compiled with its project's plan. The plan is the same whichever member file is open.

## Plan (mirrors `bcc build`'s final `qcc` line)

The plan is passed to U1's `ProjectCompile::run`.

**Binary project.**
- combine;
- the stdlib from `selectStdlibFiles( stdlib, scanImports( all source texts ) )`;
- the sources;
- the closure interfaces;
- `moduleOrigin = realpath(root)`.

**Library project.** No stdlib and no combine (R4).

**Interfaces.**
- Each is the exact `.bmod` text the dependency's own `bcc` compile would emit.
- It is derived in memory from source, with buffers first, by `BmodEmitter::emit` with a text provider.
- Each is added at `<depDir>/<[project] name>.bmod` with the dependency's canonical origin.

## Setup diagnostics (exactly one per problem; never a crash; never a process)

| Problem | Severity | Location | Message |
|---|---|---|---|
| missing path dependency | Error | first `import <name>` in the document, else 1:1 | `dependency '<key>' cannot be resolved: '<path>' has no blang.toml (declared at <toml>:<line>)` |
| git dependency not fetched | Error | the same | `git dependency '<key>' is not fetched (expected a checkout at '<dir>'); run 'bcc build' to fetch it` |
| git dependency not pinned | Error | the same | `git dependency '<key>' must pin a version: add tag = "..." or rev = "..."` |
| dependency fails to compile | Error | the same | `dependency '<name>' failed to compile: <file>:<line>:<col>: <message>` |
| dependency cycle | Error | the same | `circular dependency: <a> -> <b> -> … -> <a>` |
| malformed `blang.toml` line | Warning | the document at 1:1 | `<toml>:<line>: parse error: <message>` |

- **Suppression.** While a dependency `<name>` is unresolvable, document diagnostics whose message contains `'<name>'` or `'<name>.` are not published. All others are.
- **No-import case.** When nothing imports the dependency, the Error is placed at 1:1 with ` (declared at <toml>:<line>)`.

## Overlay

Every project source, dependency source and `blang.toml` is read from the open editor buffer when one exists, and from disk otherwise. That includes the text the interface's verbatim generic bodies are sliced from. Opening or changing a document never re-publishes other documents.

## Guarantees

- Each compile is isolated: it has its own global scope, and nothing is reused across documents or reparses.
- No `git`, no subprocess, no network.
- Git dependencies are found under `blangd`'s own `XDG_CACHE_HOME`/`HOME` (R9).
- No byte is ever written to stderr.
