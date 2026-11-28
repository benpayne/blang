# Editor integration for BLang

BLang ships a language server, **blangd**, speaking the Language Server
Protocol over stdio. Any LSP-capable editor can use it.

Build it (no LLVM required):

```bash
cmake -S . -B build          # -DBLANG_ENABLE_LLVM=OFF also works
cmake --build build --target blangd
```

## What blangd provides (v1)

- **Diagnostics** — the full compiler frontend (parser + semantic pass) runs
  on every keystroke (full-document sync); errors and warnings appear as you
  type, same messages as `qcc`.
- **Go to definition** — variables, functions, struct constructors, enum
  variants, fields, and methods; **across modules** too. A qualified
  dependency call (`mathlib.add`, or `m.add` after `import mathlib as m;`), a
  dependency type, a method on a value you hold but never named, a stdlib
  symbol (`net.http_ok`), the prelude `Map`, or a sibling project file all
  jump to the defining **source** file, even when it is not open. They never
  jump to a generated `.bmod`.
- **Hover** — declaration/signature text (`fn add(int a, int b) -> int`,
  `Array<int> nums`) or the expression's resolved type. A declaration from
  another module is qualified the way you would write it, using the import
  name or alias (`fn mathlib.add(...)`, `fn m.add(...)`). Prelude and core
  types stay bare (`struct Map<K, V>`).
- **Document symbols** — outline of structs (fields/methods), enums
  (variants), protocols, functions, and test blocks.

Scope notes: each open file compiles in the same **stdlib/prelude context**
`bcc file.b` uses (the prelude `Map`/`Set`/`Buffer`, the base stdlib modules,
and any stdlib module the file imports), so blangd reports exactly the
compiler's diagnostics — see "Stdlib discovery" below. A file that belongs
to a `blang.toml` project compiles in that project's `bcc build` context — see
"Projects" below. Ranges are zero-length
(the compiler tracks start positions only); position encoding is negotiated
as `utf-8` (LSP 3.17).

## Stdlib discovery

blangd finds the stdlib directory once, when the editor sends `initialize`, by
this precedence — the **first route that is set wins**:

1. `initializationOptions.stdlibPath` (a string, from your editor config);
2. the `BLANG_STDLIB_DIR` environment variable;
3. `stdlib/` next to the blangd executable (the build tree's `build/stdlib/`,
   which CMake populates, works out of the box).

A directory is usable only if it contains the five base modules (`sys.b`,
`buffer.b`, `collections.b`, `fs.b`, `net.b`). If the selected route is not
usable, blangd does **not** try the next one (that would silently use a stdlib
you did not configure): every file instead shows one warning at line 1,

```text
stdlib not found: <route> '<path>' is not a usable stdlib directory (missing <what>); set initializationOptions.stdlibPath or BLANG_STDLIB_DIR, or install stdlib/ next to blangd — this file is checked without the stdlib
```

and is checked alone, without the stdlib. blangd only reads files there; it
never runs a process or touches the network.

## Projects

When an open file sits in a `blang.toml` project, blangd checks it exactly as
`bcc build` would compile it:

- **Discovery and membership.** blangd walks up from the file's directory to
  the nearest `blang.toml`. The file gets the project context only if it is one
  of the files `bcc build` compiles (the project's `.b` sources). A file in a
  project subdirectory that `bcc build` does not compile is checked on its own,
  in the stdlib context.
- **Sources and dependencies.** Sibling sources are visible (`import helpers;`
  then `helpers.triple(...)`). Path dependencies (`{ path = "../mathlib" }`)
  and **already-fetched** git dependencies are read **from source**: blangd
  compiles each one as `bcc build` would and derives its interface in memory,
  so no `bcc build` is needed first. blangd **never fetches**: it finds a git
  checkout in the same cache `bcc` uses, `$XDG_CACHE_HOME/blang/git/` (else
  `~/.cache/blang/git/`), using **blangd's own** environment. Start your editor
  with the same `XDG_CACHE_HOME`/`HOME` you build with. Run `bcc build` once to
  fetch a new git dependency.
- **Unsaved buffers.** An open, unsaved buffer (a dependency's `lib.b`, a
  sibling, or `blang.toml` itself) is what blangd compiles against. The
  consumer picks up the change the next time it is edited; blangd does not
  re-check other open files when one changes.
- **Setup problems** appear as one diagnostic at the file's `import` of the
  affected module (or at line 1 with `(declared at <blang.toml>:<line>)`), and
  follow-on errors about that module are suppressed:

  | Problem | Message |
  |---|---|
  | missing path dependency | `dependency 'x' cannot be resolved: '<path>' has no blang.toml (declared at …)` |
  | unfetched git dependency | `git dependency 'x' is not fetched (expected a checkout at '<dir>'); run 'bcc build' to fetch it` |
  | unpinned git dependency | `git dependency 'x' must pin a version: add tag = "..." or rev = "..."` |
  | a dependency that does not compile | `dependency 'x' failed to compile: <file>:<line>:<col>: <its first error>` |
  | a dependency cycle | `circular dependency: a -> b -> a` |
  | a bad `blang.toml` line | Warning at line 1: `<blang.toml>:<line>: parse error: …` |
  | an unreadable `blang.toml` | Warning at line 1: `<blang.toml>: cannot read blang.toml`, then the file is checked on its own |

## Neovim (>= 0.8)

```lua
-- ~/.config/nvim/init.lua
vim.filetype.add({ extension = { b = "blang" } })
vim.api.nvim_create_autocmd("FileType", {
	pattern = "blang",
	callback = function()
		vim.lsp.start({
			name = "blangd",
			cmd = { "/path/to/blang/build/blangd" },
			root_dir = vim.fs.dirname(vim.fs.find({ "blang.toml", ".git" }, { upward = true })[1]),
			-- Optional: only needed when blangd is not next to its stdlib/.
			-- init_options = { stdlibPath = "/path/to/blang/stdlib" },
		})
	end,
})
```

Verified continuously by the headless smoke test: `./test_lsp_nvim.sh`
(runs `ci/nvim_smoke.lua` — starts blangd through the real Neovim LSP
client and asserts diagnostics arrive; SKIPs when nvim is not installed).

## VS Code

`editors/blang-vscode/` is currently a **TextMate grammar only** (syntax
highlighting, no language-server client). Until a client extension exists,
a generic LSP bridge extension can run blangd; point it at the binary with
language id `blang` and file pattern `*.b`.

## Any other LSP client

blangd is a plain stdio server:

- **command**: `/path/to/blang/build/blangd` (no arguments)
- **transport**: stdio (`Content-Length` framed JSON-RPC)
- **language**: `blang`, files `*.b`
- **stdlib**: optional `initializationOptions: { "stdlibPath": "/path/to/stdlib" }`
  in `initialize`, or `BLANG_STDLIB_DIR` in its environment (see above)

## Testing without an editor

The whole protocol surface is tested editor-free by the golden-transcript
harness: `./test_lsp.sh` drives blangd through scripted conversations
(`test_files/lsp/*.lsp.jsonl`) with `tools/lsp_client.py` and compares
canonical transcripts against committed goldens. See CLAUDE.md's testing
section.
