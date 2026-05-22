const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function resolveServerPath() {
  const configured = vscode.workspace.getConfiguration('kinglet').get('server.path', '');
  if (configured) return configured;

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
  const serverOptions = {
    command: resolveServerPath(),
    transport: TransportKind.stdio,
  };

  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'kinglet' }],
  };

  client = new LanguageClient('kinglet', 'Kinglet', serverOptions, clientOptions);
  client.start();
  context.subscriptions.push({ dispose: () => client.stop() });
}

function deactivate() {
  if (client) return client.stop();
}

module.exports = { activate, deactivate };
