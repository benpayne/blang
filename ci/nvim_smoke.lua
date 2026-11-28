-- Headless-Neovim smoke test for blangd: the one "real LSP client" check.
-- Starts blangd via vim.lsp.start, opens a fixture with a known semantic
-- error, and waits for diagnostics to arrive through the full client stack
-- (initialize handshake, didOpen, publishDiagnostics). Exits non-zero
-- (cquit) on any failure so the wrapper script can gate CI.
--
-- modules-v2-lsp U5 (done-condition 7, REQ-010): a second leg opens a
-- blang.toml PROJECT with a path dependency (test_build/myapp/main.b, which
-- imports mathlib), asserts zero ERROR diagnostics on it (blangd compiles it in
-- bcc build's context), and asserts a cross-module textDocument/definition on
-- `mathlib.add` lands in mathlib's SOURCE file (a file:// URI, never a .bmod).
--
-- Run: nvim --headless -u NONE -l ci/nvim_smoke.lua <path-to-blangd>
-- (cwd must be the repo root; requires Neovim >= 0.8 for vim.lsp.start.)

local blangd = arg and arg[1] or "build/blangd"
local fixture = "test_files/lsp/src/undef_var.b"
local project_doc = "test_build/myapp/main.b"
local root = vim.fn.getcwd()
-- Pin the repository stdlib (blangd's stdlibPath route) so both legs resolve
-- the same stdlib regardless of where blangd was built.
local init_options = { stdlibPath = root .. "/stdlib" }

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

-- Record every publishDiagnostics URI, so the project leg can wait for a
-- publish that carries ZERO diagnostics (vim.diagnostic.get alone cannot
-- distinguish "clean" from "not published yet").
local published = {}
local default_publish = vim.lsp.handlers["textDocument/publishDiagnostics"]
vim.lsp.handlers["textDocument/publishDiagnostics"] = function(err, result, ctx, config)
	if result and result.uri then
		published[result.uri] = true
	end
	return default_publish(err, result, ctx, config)
end

local lsp_config = {
	name = "blangd",
	cmd = { blangd },
	root_dir = root,
	init_options = init_options,
}
local client_id = vim.lsp.start(lsp_config)
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

-- ---- Project leg: a blang.toml member with a path dependency --------------
vim.cmd("edit " .. project_doc)
local pbuf = vim.api.nvim_get_current_buf()
local puri = vim.uri_from_bufnr(pbuf)
-- Same name + root_dir: vim.lsp.start reuses the running client and attaches
-- this buffer (didOpen).
local pclient = vim.lsp.start(lsp_config)
if pclient ~= client_id then
	fail("expected the project buffer to attach to the running blangd client")
end
if not vim.wait(15000, function() return published[puri] == true end, 50) then
	fail("no publishDiagnostics for " .. project_doc .. " within 15s")
end
for _, pd in ipairs(vim.diagnostic.get(pbuf)) do
	if pd.severity == vim.diagnostic.severity.ERROR then
		fail(project_doc .. " has an ERROR diagnostic (expected none in project context): " ..
			pd.message .. " at line " .. pd.lnum)
	end
end

-- Cross-module go-to-definition on `add` in `mathlib.add(3, 4)` (0-based 5:23).
local responses = vim.lsp.buf_request_sync(pbuf, "textDocument/definition", {
	textDocument = { uri = puri },
	position = { line = 5, character = 23 },
}, 10000)
local loc = nil
for _, resp in pairs(responses or {}) do
	if resp.result then
		loc = resp.result
		if vim.tbl_islist(loc) then
			loc = loc[1]
		end
	end
end
if loc == nil or loc.uri == nil then
	fail("textDocument/definition on mathlib.add returned no location")
end
local want = vim.uri_from_fname(root .. "/test_build/mathlib/math.b")
if loc.uri ~= want then
	fail("definition of mathlib.add is " .. loc.uri .. ", expected " .. want)
end
if loc.range.start.line ~= 0 then
	fail("definition of mathlib.add on line " .. loc.range.start.line .. ", expected 0 (pub fn add)")
end
print("nvim_smoke: project leg OK (0 errors; mathlib.add -> " .. loc.uri .. ")")

-- Orderly shutdown so blangd exits 0 (also exercises shutdown/exit).
vim.lsp.stop_client(client_id)
vim.wait(2000, function()
	return vim.lsp.get_client_by_id(client_id) == nil
end, 50)

print("nvim_smoke: OK")
vim.cmd("quitall!")
