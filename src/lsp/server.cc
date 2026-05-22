#include "lsp/server.h"

#include "lsp/protocol.h"

#include <cctype>
#include <sstream>

namespace kinglet::lsp {

void Server::run() {
  while (true) {
    std::string msg = transport_.read_message();
    if (msg.empty()) break;
    std::size_t pos = 0;
    auto parsed = json::parse(msg, pos);
    handle_message(parsed);
  }
}

void Server::handle_message(const json::Value &msg) {
  if (!msg.is_object()) return;
  const auto &obj = msg.as_object();
  auto method_it = obj.find("method");
  if (method_it == obj.end()) return;

  const std::string &method = method_it->second.as_string();
  json::Value params;
  auto params_it = obj.find("params");
  if (params_it != obj.end()) params = params_it->second;

  json::Value id = json::Value::null();
  auto id_it = obj.find("id");
  if (id_it != obj.end()) id = id_it->second;

  if (method == "initialize") {
    send_response(id, handle_initialize(params));
  } else if (method == "textDocument/didOpen") {
    handle_did_open(params);
  } else if (method == "textDocument/didChange") {
    handle_did_change(params);
  } else if (method == "textDocument/didClose") {
    handle_did_close(params);
  } else if (method == "textDocument/completion") {
    send_response(id, handle_completion(params));
  } else if (method == "textDocument/definition") {
    send_response(id, handle_definition(params));
  } else if (method == "textDocument/hover") {
    send_response(id, handle_hover(params));
// PLACEHOLDER_SERVER_CONTINUE
  } else if (method == "shutdown") {
    send_response(id, json::Value::null());
  } else if (method == "exit") {
    return;
  }
}

void Server::send_notification(const std::string &method, const json::Value &params) {
  json::Object msg;
  msg["jsonrpc"] = json::Value::string("2.0");
  msg["method"] = json::Value::string(method);
  msg["params"] = params;
  transport_.write_message(json::Value(msg));
}

void Server::send_response(const json::Value &id, const json::Value &result) {
  json::Object msg;
  msg["jsonrpc"] = json::Value::string("2.0");
  msg["id"] = id;
  msg["result"] = result;
  transport_.write_message(json::Value(msg));
}

json::Value Server::handle_initialize(const json::Value &) {
  json::Object capabilities;

  json::Object text_doc_sync;
  text_doc_sync["openClose"] = json::Value(true);
  text_doc_sync["change"] = json::Value::number(1);
  capabilities["textDocumentSync"] = json::Value(text_doc_sync);

  json::Object completion_provider;
  json::Array trigger_chars;
  trigger_chars.push_back(json::Value::string("."));
  trigger_chars.push_back(json::Value::string(":"));
  completion_provider["triggerCharacters"] = json::Value(trigger_chars);
  capabilities["completionProvider"] = json::Value(completion_provider);

  capabilities["definitionProvider"] = json::Value(true);
  capabilities["hoverProvider"] = json::Value(true);

  json::Object server_info;
  server_info["name"] = json::Value::string("kinglet");
  server_info["version"] = json::Value::string("0.2.0");

  json::Object result;
  result["capabilities"] = json::Value(capabilities);
  result["serverInfo"] = json::Value(server_info);
  return json::Value(result);
}

void Server::handle_did_open(const json::Value &params) {
  if (!params.is_object()) return;
  const auto &p = params.as_object();
  auto doc_it = p.find("textDocument");
  if (doc_it == p.end()) return;
  const auto &doc = doc_it->second.as_object();
  auto uri_it = doc.find("uri");
  auto text_it = doc.find("text");
  if (uri_it == doc.end() || text_it == doc.end()) return;

  std::string uri = uri_it->second.as_string();
  store_.open(uri, text_it->second.as_string());
  auto *d = store_.get(uri);
  if (d) publish_diagnostics(*d);
}

void Server::handle_did_change(const json::Value &params) {
  if (!params.is_object()) return;
  const auto &p = params.as_object();
  auto doc_it = p.find("textDocument");
  auto changes_it = p.find("contentChanges");
  if (doc_it == p.end() || changes_it == p.end()) return;

  std::string uri = doc_it->second.as_object().at("uri").as_string();
  const auto &changes = changes_it->second.as_array();
  if (changes.empty()) return;

  std::string new_text = changes.back().as_object().at("text").as_string();
  store_.change(uri, new_text);
  auto *d = store_.get(uri);
  if (d) publish_diagnostics(*d);
}

void Server::handle_did_close(const json::Value &params) {
  if (!params.is_object()) return;
  std::string uri = uri_from_params(params);
  if (!uri.empty()) {
    json::Object diag_params;
    diag_params["uri"] = json::Value::string(uri);
    diag_params["diagnostics"] = json::Value(json::Array{});
    send_notification("textDocument/publishDiagnostics", json::Value(diag_params));
    store_.close(uri);
  }
}
// PLACEHOLDER_HANDLERS_CONTINUE

void Server::ensure_analyzed(Document &doc) {
  if (!doc.dirty) return;
  doc.analysis = analyze(doc.text);
  doc.dirty = false;
}

void Server::publish_diagnostics(Document &doc) {
  ensure_analyzed(doc);
  json::Array items;
  for (const auto &[line, col, message] : doc.analysis.diagnostics) {
    items.push_back(protocol::diagnostic(line, col, message));
  }
  json::Object diag_params;
  diag_params["uri"] = json::Value::string(doc.uri);
  diag_params["diagnostics"] = json::Value(items);
  send_notification("textDocument/publishDiagnostics", json::Value(diag_params));
}

json::Value Server::handle_completion(const json::Value &params) {
  json::Array items;
  std::string uri = uri_from_params(params);
  auto [line, character] = position_from_params(params);
  auto *doc = store_.get(uri);
  if (!doc) return json::Value(items);

  ensure_analyzed(*doc);

  std::string prefix = get_word_at(doc->text, line, character);

  // Check for namespace:: completion
  std::string line_text;
  int cur_line = 0;
  std::istringstream stream(doc->text);
  std::string l;
  while (std::getline(stream, l)) {
    if (cur_line == line) { line_text = l; break; }
    ++cur_line;
  }

  std::string ns_name;
  if (character >= 2) {
    int pos = character - 1;
    if (pos < static_cast<int>(line_text.size()) && line_text[static_cast<std::size_t>(pos)] == ':') {
      int end = pos;
      if (end >= 1 && line_text[static_cast<std::size_t>(end - 1)] == ':') --end;
      int start = end;
      while (start > 0 && std::isalnum(static_cast<unsigned char>(line_text[static_cast<std::size_t>(start - 1)])))
        --start;
      if (start < end) ns_name = line_text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    }
  }

  if (ns_name == "io") {
    items.push_back(protocol::completion_item_with_edit("out", 3, "stdout output, no newline", line, character, character));
    items.push_back(protocol::completion_item_with_edit("err", 3, "stderr output", line, character, character));
    items.push_back(protocol::completion_item_with_edit("in", 3, "stdin input", line, character, character));
    return json::Value(items);
  }

  // Scope-aware symbols
  auto visible = doc->analysis.symbols.visible_at(line + 1);
  for (const auto *sym : visible) {
    if (!prefix.empty() && sym->name.find(prefix) == std::string::npos) continue;
    int kind = sym->kind == SymbolKind::Function ? 3 : 6;
    std::string detail = sym->type_name;
    if (sym->kind == SymbolKind::Function) {
      detail = sym->return_type + " " + sym->name + "(";
      for (std::size_t i = 0; i < sym->params.size(); ++i) {
        if (i > 0) detail += ", ";
        detail += sym->params[i].type + " " + sym->params[i].name;
      }
      detail += ")";
    }
    items.push_back(protocol::completion_item(sym->name, kind, detail));
  }

  // Keywords
  for (const char *kw : {"if", "else", "for", "while", "break", "continue", "return",
                          "inspect", "using", "namespace", "const", "import", "export",
                          "struct", "enum", "trait", "spawn", "select",
                          "int", "float", "double", "bool", "string", "void", "byte", "auto",
                          "true", "false", "null", "io"}) {
    if (!prefix.empty() && std::string(kw).find(prefix) == std::string::npos) continue;
    items.push_back(protocol::completion_item(kw, 14));
  }

  return json::Value(items);
}
// PLACEHOLDER_DEF_HOVER

json::Value Server::handle_definition(const json::Value &params) {
  std::string uri = uri_from_params(params);
  auto [line, character] = position_from_params(params);
  auto *doc = store_.get(uri);
  if (!doc) return json::Value::null();

  ensure_analyzed(*doc);
  std::string word = get_full_word_at(doc->text, line, character);
  if (word.empty()) return json::Value::null();

  const auto *sym = doc->analysis.symbols.find_definition(word, line + 1);
  if (!sym) return json::Value::null();

  return protocol::location(uri, sym->location.line, sym->location.column);
}

json::Value Server::handle_hover(const json::Value &params) {
  std::string uri = uri_from_params(params);
  auto [line, character] = position_from_params(params);
  auto *doc = store_.get(uri);
  if (!doc) return json::Value::null();

  ensure_analyzed(*doc);
  std::string word = get_full_word_at(doc->text, line, character);
  if (word.empty()) return json::Value::null();

  const auto *sym = doc->analysis.symbols.find_definition(word, line + 1);
  if (!sym) return json::Value::null();

  std::string content;
  if (sym->kind == SymbolKind::Function) {
    content = sym->return_type + " " + sym->name + "(";
    for (std::size_t i = 0; i < sym->params.size(); ++i) {
      if (i > 0) content += ", ";
      content += sym->params[i].type + " " + sym->params[i].name;
    }
    content += ")";
  } else {
    content = sym->type_name + " " + sym->name;
  }

  json::Object hover;
  json::Object markup;
  markup["kind"] = json::Value::string("markdown");
  markup["value"] = json::Value::string("```kinglet\n" + content + "\n```");
  hover["contents"] = json::Value(markup);
  return json::Value(hover);
}

std::string Server::uri_from_params(const json::Value &params) const {
  if (!params.is_object()) return "";
  const auto &p = params.as_object();
  auto doc_it = p.find("textDocument");
  if (doc_it == p.end()) return "";
  const auto &doc = doc_it->second.as_object();
  auto uri_it = doc.find("uri");
  if (uri_it == doc.end()) return "";
  return uri_it->second.as_string();
}

std::pair<int, int> Server::position_from_params(const json::Value &params) const {
  if (!params.is_object()) return {0, 0};
  const auto &p = params.as_object();
  auto pos_it = p.find("position");
  if (pos_it == p.end()) return {0, 0};
  const auto &pos = pos_it->second.as_object();
  int line = static_cast<int>(pos.at("line").as_number());
  int character = static_cast<int>(pos.at("character").as_number());
  return {line, character};
}

std::string Server::get_word_at(const std::string &text, int line, int character) const {
  int cur_line = 0;
  std::size_t line_start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (cur_line == line) { line_start = i; break; }
    if (text[i] == '\n') { ++cur_line; line_start = i + 1; }
  }
  std::size_t pos = line_start + static_cast<std::size_t>(character);
  if (pos > text.size()) return "";

  std::size_t start = pos;
  while (start > line_start && (std::isalnum(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '_'))
    --start;

  if (start == pos) return "";
  return text.substr(start, pos - start);
}

std::string Server::get_full_word_at(const std::string &text, int line, int character) const {
  int cur_line = 0;
  std::size_t line_start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (cur_line == line) { line_start = i; break; }
    if (text[i] == '\n') { ++cur_line; line_start = i + 1; }
  }
  std::size_t pos = line_start + static_cast<std::size_t>(character);
  if (pos > text.size()) return "";

  std::size_t start = pos;
  while (start > line_start && (std::isalnum(static_cast<unsigned char>(text[start - 1])) || text[start - 1] == '_'))
    --start;
  std::size_t end = pos;
  while (end < text.size() && text[end] != '\n' && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
    ++end;

  if (start == end) return "";
  return text.substr(start, end - start);
}

} // namespace kinglet::lsp
