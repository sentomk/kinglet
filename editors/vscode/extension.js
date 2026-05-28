const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function resolveServerPath() {
  const configured = vscode.workspace.getConfiguration('kinglet').get('server.path', '');
  if (configured) {
    if (path.isAbsolute(configured) && fs.existsSync(configured)) return configured;
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
      const resolved = path.resolve(workspaceFolders[0].uri.fsPath, configured);
      if (fs.existsSync(resolved)) return resolved;
    }
  }

  const candidates = [
    path.join(process.env.HOME || '', 'bin', 'kinglet-lsp'),
    '/usr/local/bin/kinglet-lsp',
  ];
  for (const p of candidates) {
    if (fs.existsSync(p)) return p;
  }
  return 'kinglet-lsp';
}

function activate(context) {
  const serverPath = resolveServerPath();

  const serverOptions = {
    command: serverPath,
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'kinglet' }],
  };

  client = new LanguageClient('kinglet', 'Kinglet', serverOptions, clientOptions);

  client.onDidChangeState(e => {
    client.outputChannel.appendLine(`[state] ${e.oldState} -> ${e.newState}`);
  });

  client.start().then(() => {
    client.outputChannel.appendLine(`[ready] Server path: ${serverPath}`);
  }).catch(err => {
    client.outputChannel.appendLine(`[error] ${err.message}`);
  });

  context.subscriptions.push({ dispose: () => client.stop() });
}

function deactivate() {
  if (client) return client.stop();
}

module.exports = { activate, deactivate };
