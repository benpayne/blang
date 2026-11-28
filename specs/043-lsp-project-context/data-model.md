# Data Model: blangd project context (U3)

Everything here lives in memory and is rebuilt on every reparse. The only things persisted are the committed fixtures and lists.

## Project (`lsp/ProjectContext`)

| Field | Source / rule |
|---|---|
| `root` | The nearest ancestor directory of the document (inclusive) that has a `blang.toml`. The search stops at the first one found. |
| `config` | `ProjectConfig::loadFromText( root/blang.toml, <overlay or disk text>, &issues )`. |
| `issues` | `ConfigIssue { line, message }`. Each issue becomes one Warning at 1:1 (R7/R8). |
| `sources` | `BlangProject::discoverSourceFiles( root )`: root `*.b` files, sorted (`bcc`). |
| `isMember` | The document's `realpath` is one of `sources` (R5). If false, the U2 single-file path is used. |
| `origin` | `realpath( root )`, which is `bcc`'s `canonDir`. |
| `kind` | `bin` or `lib`, from `config->isLibrary()` (R4 decides the plan). |

## Dependency node (graph built per reparse)

| Field | Rule |
|---|---|
| `key`, `line` | The `[deps]` key and `Dependency::line` (new field). |
| `dir`, `origin` | `BlangProject::resolveDependencyDir( declaringRoot, dep, allowFetch = false )`. Path: `resolvePath`. Git: `locateGitCheckout`, only when `warm`. Origin: `realpath(dir)`, else `dir`. |
| `name` | The dependency's `[project] name`. It is the import name and the interface basename `<dir>/<name>.bmod`. |
| `closure` | The dependency's own transitive closure, folded `bcc`'s way: each dependency's closure, then the dependency itself, deduplicated by origin. |
| `state` | `resolved`, or `unresolvable{ reason }`, where the reason is one of `missing-path`, `unfetched-git`, `unpinned-git`, `no-path-or-git`, `compile-failed{first error}` or `cycle{path}`. |
| `run` | `Run` (below) for a resolved dependency. |
| `interface` | The emitted `.bmod` text, from `BmodEmitter::emit` with a text provider. |

**Graph rules.**
- DFS with an on-path set. A back edge is a cycle, and the DFS clears the set on every exit, as in KG-8.
- An unresolvable node and everything reachable only through it are dropped from the closure. Each unresolvable node yields exactly one setup diagnostic.

## Run (audit R2)

`Run { shared_ptr<Resolver> resolver /* AliasMode::kManual */; shared_ptr<CompileContext> ctx; }`.

- `ctx` is destroyed before `resolver`, because `ctx` is declared second.
- The `gScope` alias is set only around `ProjectCompile::run`, by `GlobalAliasScope`.

## Document plan

| Kind | `combine` | Inputs (before U1's interfaces-first reorder) | `moduleOrigin` | `identity` |
|---|---|---|---|---|
| bin member | iff the stdlib set is non-empty and usable | stdlib set (disk) + `sources` (in-memory) + closure interfaces (in-memory, `origin` set) | `realpath(root)` | `kCanonical` |
| lib member | false | `sources` (in-memory) + closure interfaces | `realpath(root)` | `kCanonical` |
| non-member / no project | U2 | U2 | — | `kNone` (U2) |

## CompileResult (extends U2)

Members are listed in declaration order. Destruction runs in reverse.

| Member | Notes |
|---|---|
| `resolver` | The document run's resolver. It is declared first, so it is destroyed last. |
| `context` | The document run's context: stdlib, interfaces and sources. |
| `deps` | `vector<shared_ptr<Run>>`, the dependency source runs, kept for U4 navigation. Nothing in `context` points into them. |
| `module`, `fileScope` | The document's own module and scope, co-owned. |
| `diagnostics` | Setup and toml diagnostics first, then the run's diagnostics located in the document, minus the suppression set. |

## Overlay

`DocumentStore::textForPath( realPath ) -> const std::string *`. It contains open buffers only. Every project file, dependency source and `blang.toml` read goes through it first.

## Corpus list additions (`test_files/lsp/corpus.txt`)

- `project <dir>`: the 10 `test_build` binaries, the 6 `examples/*`, and `test_files/lsp/projects/multifile`.
- `xmodule <fixture dir>`: the 11 `test_files/fail/xmodule/*` fixtures.
- `stdlib` entries lose `test_build/myapp/main.b` and `test_build/counterapp/main.b` (research R1).
