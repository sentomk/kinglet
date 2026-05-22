const vscode = require('vscode');
const { spawn } = require('child_process');

let proc;
let channel;
let stdoutBuffer = '';
let pendingResolve = null;
let nextId = 100;

function send(obj) {
  const body = JSON.stringify(obj);
  proc.stdin.write(`Content-Length: ${body.length}\r\n\r\n${body}`);
}

function parseMessages() {
  while (true) {
    const idx = stdoutBuffer.indexOf('Content-Length:');
    if (idx < 0) break;
    const rest = stdoutBuffer.substring(idx);
    const headerEnd = rest.indexOf('\r\n\r\n');
    if (headerEnd < 0) break;
    const match = rest.match(/Content-Length: (\d+)/);
    if (!match) { stdoutBuffer = rest.substring(4); continue; }
    const len = parseInt(match[1]);
    const bodyStart = headerEnd + 4;
    if (rest.length < bodyStart + len) break;
    const body = rest.substring(bodyStart, bodyStart + len);
    stdoutBuffer = rest.substring(bodyStart + len);
    try {
      const msg = JSON.parse(body);
      if (pendingResolve && msg.id === pendingResolve.id) {
        pendingResolve.resolve(msg.result || null);
        pendingResolve = null;
      }
    } catch {}
  }
}

function request(method, params) {
  return new Promise((resolve) => {
    const id = nextId++;
    pendingResolve = { id, resolve };
    send({ jsonrpc: '2.0', id, method, params });
    setTimeout(() => {
      if (pendingResolve && pendingResolve.id === id) {
        pendingResolve.resolve(null);
        pendingResolve = null;
      }
    }, 1000);
  });
}

function activate(context) {
  proc = spawn('/Users/bytedance/bin/kinglet-lsp', [], { stdio: ['pipe', 'pipe', 'pipe'] });
  channel = vscode.window.createOutputChannel('Kinglet LSP');
  proc.stdout.on('data', (d) => { stdoutBuffer += d.toString(); parseMessages(); });
  proc.stderr.on('data', (d) => channel.appendLine(d.toString().trim()));
  send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { capabilities: {}, processId: process.pid } });
  send({ jsonrpc: '2.0', method: 'initialized', params: {} });

  const provider = vscode.languages.registerCompletionItemProvider(
    { scheme: 'file', language: 'kinglet' },
    {
      async provideCompletionItems(document, position) {
        try {
          channel.appendLine(`C: line=${position.line} ch=${position.character}`);
          send({ jsonrpc: '2.0', method: 'textDocument/didChange', params: {
            textDocument: { uri: document.uri.toString(), version: document.version },
            contentChanges: [{ text: document.getText() }]
          }});
          const result = await request('textDocument/completion', {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
          });
          const items = [];
          if (!result || !Array.isArray(result)) return items;
          for (const item of result) {
            const label = item.label || '';
            const ci = new vscode.CompletionItem(label, item.kind || vscode.CompletionItemKind.Keyword);
            if (item.detail) ci.detail = item.detail;
            if (item.textEdit) {
              ci.textEdit = new vscode.TextEdit(
                new vscode.Range(
                  item.textEdit.range.start.line, item.textEdit.range.start.character,
                  item.textEdit.range.end.line, item.textEdit.range.end.character
                ),
                item.textEdit.newText
              );
            } else if (item.insertText) {
              ci.insertText = new vscode.SnippetString(item.insertText);
            }
            items.push(ci);
          }
          return items;
        } catch (e) {
          channel.appendLine(`Err: ${e.message}`);
          return [];
        }
      }
    }
  );

  // Manual trigger on ':' to work around VSCode trigger character issues
  context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(e => {
    if (e.document.languageId !== 'kinglet') return;
    for (const change of e.contentChanges) {
      if (change.text === ':') {
        vscode.commands.executeCommand('editor.action.triggerSuggest');
      }
    }
  }));

  context.subscriptions.push(provider, {
    dispose: () => { proc.kill(); channel.dispose(); }
  });
  vscode.window.showInformationMessage('Kinglet LSP ready');
}

function deactivate() {
  if (proc) proc.kill();
}

module.exports = { activate, deactivate };
