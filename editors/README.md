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
  variants, fields, and methods.
- **Hover** — declaration/signature text (`fn add(int a, int b) -> int`,
  `Array<int> nums`) or the expression's resolved type.
- **Document symbols** — outline of structs (fields/methods), enums
  (variants), protocols, functions, and test blocks.

v1 scope notes: each file compiles alone (cross-file `import` resolution is
`bcc build`'s combine mode, not yet in the server); ranges are zero-length
(the compiler tracks start positions only); position encoding is negotiated
as `utf-8` (LSP 3.17).

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

## Testing without an editor

The whole protocol surface is tested editor-free by the golden-transcript
harness: `./test_lsp.sh` drives blangd through scripted conversations
(`test_files/lsp/*.lsp.jsonl`) with `tools/lsp_client.py` and compares
canonical transcripts against committed goldens. See CLAUDE.md's testing
section.
