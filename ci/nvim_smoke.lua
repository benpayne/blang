-- Headless-Neovim smoke test for blangd: the one "real LSP client" check.
-- Starts blangd via vim.lsp.start, opens a fixture with a known semantic
-- error, and waits for diagnostics to arrive through the full client stack
-- (initialize handshake, didOpen, publishDiagnostics). Exits non-zero
-- (cquit) on any failure so the wrapper script can gate CI.
--
-- Run: nvim --headless -u NONE -l ci/nvim_smoke.lua <path-to-blangd>
-- (cwd must be the repo root; requires Neovim >= 0.8 for vim.lsp.start.)

local blangd = arg and arg[1] or "build/blangd"
local fixture = "test_files/lsp/src/undef_var.b"

local function fail(msg)
	io.stderr:write("nvim_smoke: FAIL: " .. msg .. "\n")
	vim.cmd("cquit 1")
end

if vim.fn.executable(blangd) ~= 1 then
	fail("blangd not executable at " .. blangd)
end

-- Open the fixture buffer first so the client attaches to it on start.
vim.cmd("edit " .. fixture)
local bufnr = vim.api.nvim_get_current_buf()

local client_id = vim.lsp.start({
	name = "blangd",
	cmd = { blangd },
	root_dir = vim.fn.getcwd(),
})
if not client_id then
	fail("vim.lsp.start returned no client")
end

-- Wait for the publishDiagnostics round trip.
local ok = vim.wait(10000, function()
	return #vim.diagnostic.get(bufnr) > 0
end, 50)
if not ok then
	fail("no diagnostics arrived within 10s")
end

local diags = vim.diagnostic.get(bufnr)
local d = diags[1]
-- The fixture's error: `return missing;` on line 4 (0-based 3), an
-- undefined-variable error mentioning the name.
if d.severity ~= vim.diagnostic.severity.ERROR then
	fail("expected an ERROR severity diagnostic, got " .. tostring(d.severity))
end
if not string.find(d.message, "missing", 1, true) then
	fail("diagnostic message does not mention 'missing': " .. d.message)
end
if d.lnum ~= 3 then
	fail("diagnostic on line " .. d.lnum .. ", expected 3 (0-based)")
end

-- Orderly shutdown so blangd exits 0 (also exercises shutdown/exit).
vim.lsp.stop_client(client_id)
vim.wait(2000, function()
	return vim.lsp.get_client_by_id(client_id) == nil
end, 50)

print("nvim_smoke: OK")
vim.cmd("quitall!")
