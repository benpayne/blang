// BLang VS Code extension: starts the blangd language server for .b files.
//
// blangd speaks LSP over stdio. It publishes diagnostics and answers
// go-to-definition and hover, including across modules: it compiles each
// document in the same context `bcc` would (stdlib + prelude, the project's
// blang.toml sources and its path / already-fetched git dependencies).

'use strict';

const fs = require('fs');
const path = require('path');
const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');

let client;

// Resolve the blangd executable: the `blang.serverPath` setting wins; then
// `build/blangd` in an open workspace folder (the usual place in a BLang
// checkout); then plain `blangd` from PATH.
function resolveServerPath() {
	const config = vscode.workspace.getConfiguration('blang');
	const configured = config.get('serverPath');
	if (configured) {
		return configured;
	}
	for (const folder of vscode.workspace.workspaceFolders || []) {
		const candidate = path.join(folder.uri.fsPath, 'build', 'blangd');
		if (fs.existsSync(candidate)) {
			return candidate;
		}
	}
	return 'blangd';
}

function startClient(context) {
	const config = vscode.workspace.getConfiguration('blang');
	const command = resolveServerPath();

	// Only send stdlibPath when the user set it, so blangd's own fallback
	// order (BLANG_STDLIB_DIR, then <blangd-dir>/stdlib) still applies.
	const initializationOptions = {};
	const stdlibPath = config.get('stdlibPath');
	if (stdlibPath) {
		initializationOptions.stdlibPath = stdlibPath;
	}

	const serverOptions = { command, args: [] };
	const clientOptions = {
		documentSelector: [{ scheme: 'file', language: 'blang' }],
		initializationOptions,
		outputChannelName: 'BLang Language Server',
	};

	client = new LanguageClient('blang', 'BLang Language Server', serverOptions, clientOptions);
	client.start().catch((err) => {
		vscode.window.showErrorMessage(
			`BLang: could not start blangd ('${command}'): ${err.message}. ` +
			'Set "blang.serverPath" to the blangd binary.'
		);
	});
	context.subscriptions.push(client);
}

function activate(context) {
	startClient(context);

	context.subscriptions.push(
		vscode.commands.registerCommand('blang.restartServer', async () => {
			if (client) {
				await client.stop();
			}
			startClient(context);
		}),
		vscode.workspace.onDidChangeConfiguration((e) => {
			if (e.affectsConfiguration('blang.serverPath') || e.affectsConfiguration('blang.stdlibPath')) {
				vscode.commands.executeCommand('blang.restartServer');
			}
		})
	);
}

function deactivate() {
	return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
