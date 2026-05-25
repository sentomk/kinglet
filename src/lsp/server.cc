#include "lsp/server.h"

#include "lsp/protocol.h"

#include <cctype>
#include <iostream>
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
  std::cerr << "[LSP] <<< " << method << std::endl;
  json::Value params;
  auto params_it = obj.find("params");
  if (params_it != obj.end()) params = params_it->second;

  json::Value id = json::Value::null();
  auto id_it = obj.find("id");
  if (id_it != obj.end()) id = id_it->second;

  if (method == "initialize") {
    send_response(id, handle_initialize(params));
    std::cerr << "[LSP] >>> initialize (capabilities)" << std::endl;
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
  } else if (method == "textDocument/documentSymbol") {
    send_response(id, handle_document_symbol(params));
  } else if (method == "textDocument/signatureHelp") {
    send_response(id, handle_signature_help(params));
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
  capabilities["documentSymbolProvider"] = json::Value(true);

  json::Object sig_help;
  json::Array sig_trigger_chars;
  sig_trigger_chars.push_back(json::Value::string("("));
  sig_trigger_chars.push_back(json::Value::string(","));
  sig_help["triggerCharacters"] = json::Value(sig_trigger_chars);
  capabilities["signatureHelpProvider"] = json::Value(sig_help);

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
  for (const auto &diag : doc.analysis.diagnostics) {
    items.push_back(protocol::diagnostic(diag.line, diag.col, diag.message, diag.severity, diag.length));
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

  bool in_pipeline = false;
  {
    std::string before_cursor = line_text.substr(0, static_cast<std::size_t>(character));
    auto pipe_pos = before_cursor.rfind("|>");
    if (pipe_pos != std::string::npos) {
      bool only_expr_after = true;
      for (std::size_t i = pipe_pos + 2; i < before_cursor.size(); ++i) {
        char c = before_cursor[i];
        if (c == '(' || c == ')' || c == ';') { only_expr_after = false; break; }
      }
      if (only_expr_after) in_pipeline = true;
    }
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

  if (ns_name == "io" && (doc->analysis.used_namespaces.count("io") || doc->analysis.opened_namespaces.count("io"))) {
    if (in_pipeline) {
      items.push_back(protocol::completion_item_with_edit("out", 3, "stdout output, no newline", line, character, character));
      items.push_back(protocol::completion_item_with_edit("err", 3, "stderr output", line, character, character));
      items.push_back(protocol::completion_item_with_edit("in", 3, "stdin input", line, character, character));
    } else {
      items.push_back(protocol::snippet_item_with_edit("out", 3, "stdout output, no newline",
          "out($1)", line, character, character));
      items.push_back(protocol::snippet_item_with_edit("err", 3, "stderr output",
          "err($1)", line, character, character));
      items.push_back(protocol::snippet_item_with_edit("in", 3, "stdin input",
          "in($1)", line, character, character));
    }
    return json::Value(items);
  }

  if (!ns_name.empty()) {
    auto visible = doc->analysis.symbols.visible_at(line + 1);
    for (const auto *sym : visible) {
      if (sym->kind == SymbolKind::Enum && sym->name == ns_name) {
        for (const auto &variant : sym->variants) {
          items.push_back(protocol::completion_item_with_edit(variant, 20, ns_name + " variant", line, character, character));
        }
        return json::Value(items);
      }
    }
  }

  // Check for . (dot) completion — offer struct fields or io methods
  if (character >= 1) {
    int dot_pos = character - 1;
    if (dot_pos < static_cast<int>(line_text.size()) &&
        line_text[static_cast<std::size_t>(dot_pos)] == '.') {
      int end = dot_pos;
      int start = end;
      while (start > 0 && (std::isalnum(static_cast<unsigned char>(line_text[static_cast<std::size_t>(start - 1)])) ||
             line_text[static_cast<std::size_t>(start - 1)] == '_' ||
             line_text[static_cast<std::size_t>(start - 1)] == ':'))
        --start;
      std::string before_dot = line_text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));

      if ((before_dot == "io::out" || before_dot == "io::err") &&
          (doc->analysis.used_namespaces.count("io") || doc->analysis.opened_namespaces.count("io"))) {
        if (in_pipeline) {
          items.push_back(protocol::completion_item_with_edit("line", 3, "print with newline", line, character, character));
        } else {
          items.push_back(protocol::snippet_item_with_edit("line", 3, "print with newline",
              "line($1)", line, character, character));
        }
        return json::Value(items);
      }
      if (before_dot == "io::in" &&
          (doc->analysis.used_namespaces.count("io") || doc->analysis.opened_namespaces.count("io"))) {
        if (in_pipeline) {
          items.push_back(protocol::completion_item_with_edit("secret", 3, "read without echo", line, character, character));
        } else {
          items.push_back(protocol::snippet_item_with_edit("secret", 3, "read without echo",
              "secret($1)", line, character, character));
        }
        return json::Value(items);
      }

      // Regular struct field completion or array method completion
      if (start < end) {
        std::string var_name = line_text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
        auto visible_syms = doc->analysis.symbols.visible_at(line + 1);
        for (const auto *sym : visible_syms) {
          if (sym->name == var_name && !sym->type_name.empty()) {
            if (sym->type_name.size() >= 2 &&
                sym->type_name.substr(sym->type_name.size() - 2) == "[]") {
              items.push_back(protocol::snippet_item_with_edit("len", 2, "int — array length",
                  "len()", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("push", 2, "void — append element",
                  "push($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("pop", 2, "T — remove last element",
                  "pop()", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("remove", 2, "T — remove at index",
                  "remove($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("contains", 2, "bool — check membership",
                  "contains($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("clear", 2, "void — remove all elements",
                  "clear()", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("insert", 2, "void — insert at index",
                  "insert($1, $2)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("index_of", 2, "int — find element position (-1 if not found)",
                  "index_of($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("slice", 2, "T[] — sub-array from start to end",
                  "slice($1, $2)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("reverse", 2, "void — reverse in place",
                  "reverse()", line, character, character));
              return json::Value(items);
            }
            for (const auto *type_sym : visible_syms) {
              if (type_sym->kind == SymbolKind::Struct && type_sym->name == sym->type_name) {
                for (const auto &field : type_sym->fields) {
                  items.push_back(protocol::completion_item(field.name, 5, field.type_name + " " + field.name));
                }
                return json::Value(items);
              }
            }
          }
        }
      }
    }
  }

  // Scope-aware symbols
  auto visible = doc->analysis.symbols.visible_at(line + 1);
  for (const auto *sym : visible) {
    if (!prefix.empty() && sym->name.find(prefix) == std::string::npos) continue;
    int kind = 6;
    if (sym->kind == SymbolKind::Function) kind = 3;
    else if (sym->kind == SymbolKind::Struct) kind = 22;
    else if (sym->kind == SymbolKind::Enum) kind = 13;
    std::string detail = sym->type_name;
    if (sym->kind == SymbolKind::Function) {
      detail = sym->return_type + " " + sym->name + "(";
      for (std::size_t i = 0; i < sym->params.size(); ++i) {
        if (i > 0) detail += ", ";
        detail += sym->params[i].type.to_string() + " " + sym->params[i].name;
      }
      detail += ")";
      // Build snippet: name(${1:param1}, ${2:param2})
      std::string snippet = sym->name + "(";
      for (std::size_t i = 0; i < sym->params.size(); ++i) {
        if (i > 0) snippet += ", ";
        snippet += "${" + std::to_string(i + 1) + ":" + sym->params[i].name + "}";
      }
      snippet += ")";
      items.push_back(protocol::completion_item(sym->name, kind, detail, snippet, 2));
    } else {
      items.push_back(protocol::completion_item(sym->name, kind, detail));
    }
  }

  // Bare io members when 'using namespace io;' is active — insert as-is
  if (doc->analysis.opened_namespaces.count("io")) {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"out", "io::out — stdout output"},
             {"err", "io::err — stderr output"},
             {"in", "io::in — stdin input"}}) {
      if (!prefix.empty() && name.find(prefix) == std::string::npos) continue;
      items.push_back(protocol::completion_item(name, 3, detail));
    }
  } else if (doc->analysis.used_namespaces.count("io")) {
    // 'using io;' — insert qualified form io::name
    for (const auto &[name, qualified, detail] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"out", "io::out", "io::out — stdout output"},
             {"err", "io::err", "io::err — stderr output"},
             {"in", "io::in", "io::in — stdin input"}}) {
      if (!prefix.empty() && name.find(prefix) == std::string::npos) continue;
      json::Object item;
      item["label"] = json::Value::string(name);
      item["kind"] = json::Value::number(3);
      item["detail"] = json::Value::string(detail);
      item["insertText"] = json::Value::string(qualified);
      item["filterText"] = json::Value::string(name);
      items.push_back(json::Value(item));
    }
  }

  // Snippet completions for control flow / declarations
  struct Snippet { const char *label; const char *body; const char *detail; bool is_decl; };
  Snippet snippets[] = {
    {"if", "if (${1:condition}) {\n\t$0\n}", "if statement", false},
    {"if else", "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}", "if-else statement", false},
    {"for", "for (${1:int i = 0}; ${2:i < n}; ${3:i += 1}) {\n\t$0\n}", "for loop", false},
    {"while", "while (${1:condition}) {\n\t$0\n}", "while loop", false},
    {"inspect", "inspect (${1:expr}) {\n\t${2:_} => ${0:result}\n};", "pattern match", false},
    {"fun", "${1:int} ${2:name}(${3:params}) {\n\t$0\n}", "function declaration", true},
    {"struct", "struct ${1:Name} {\n\t$0\n}", "struct definition", true},
    {"enum", "enum ${1:Name} {\n\t$0\n}", "enum definition", true},
    {"using", "using ${1:io};$0", "using declaration", true},
    {"main", "int main() {\n\t$0\n\treturn 0;\n}", "main function", true},
    {"return", "return ${0:expr};", "return statement", false},
  };

  // Find the start of the current statement on this line (skip leading whitespace)
  int line_start = 0;
  for (int i = 0; i < static_cast<int>(line_text.size()); ++i) {
    if (!std::isspace(static_cast<unsigned char>(line_text[static_cast<std::size_t>(i)]))) {
      line_start = i;
      break;
    }
  }

  for (const auto &s : snippets) {
    if (!prefix.empty() && std::string(s.label).find(prefix) == std::string::npos) continue;
    if (s.is_decl) {
      items.push_back(protocol::snippet_item_with_edit(
          s.label, 15, s.detail, s.body, line, line_start, character));
    } else {
      items.push_back(protocol::completion_item(s.label, 15, s.detail, s.body, 2));
    }
  }

  // Type keywords
  for (const char *kw : {"int", "float", "double", "bool", "string", "void", "byte", "auto"}) {
    if (!prefix.empty() && std::string(kw).find(prefix) == std::string::npos) continue;
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14);
    item["insertText"] = json::Value::string(std::string(kw) + " ");
    items.push_back(json::Value(item));
  }

  // Control flow keywords
  const char *kw_with_space[] = {"if", "else", "for", "while", "return",
                                  "inspect", "const", "export", "using",
                                  "struct", "enum", "trait", "spawn", "select"};
  const char *kw_standalone[] = {"break", "continue", "true", "false", "null"};
  for (const char *kw : kw_with_space) {
    if (!prefix.empty() && std::string(kw).find(prefix) == std::string::npos) continue;
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14);
    item["insertText"] = json::Value::string(std::string(kw) + " ");
    items.push_back(json::Value(item));
  }
  for (const char *kw : kw_standalone) {
    if (!prefix.empty() && std::string(kw).find(prefix) == std::string::npos) continue;
    items.push_back(protocol::completion_item(kw, 14));
  }

  // Namespace completions — only when 'using io;' is present
  // But if we're in a 'using' statement context, insert plain name (not io::)
  bool in_using_context = false;
  {
    std::size_t first_non_space = line_text.find_first_not_of(" \t");
    if (first_non_space != std::string::npos && line_text.substr(first_non_space, 6) == "using ") {
      in_using_context = true;
    }
  }
  if (doc->analysis.used_namespaces.count("io") || doc->analysis.opened_namespaces.count("io")) {
    for (const char *ns : {"io"}) {
      if (!prefix.empty() && std::string(ns).find(prefix) == std::string::npos) continue;
      if (in_using_context) {
        items.push_back(protocol::completion_item(ns, 9, "namespace"));
      } else {
        json::Object item;
        item["label"] = json::Value::string(ns);
        item["kind"] = json::Value::number(9);
        item["detail"] = json::Value::string("namespace");
        item["insertText"] = json::Value::string(std::string(ns) + "::");
        json::Object cmd;
        cmd["title"] = json::Value::string("");
        cmd["command"] = json::Value::string("editor.action.triggerSuggest");
        item["command"] = json::Value(cmd);
        items.push_back(json::Value(item));
      }
    }
  }

  std::cerr << "[LSP] completion: " << items.size() << " items, prefix='" << prefix << "'" << std::endl;
  return json::Value(items);
}

json::Value Server::handle_document_symbol(const json::Value &params) {
  json::Array symbols;
  std::string uri = uri_from_params(params);
  auto *doc = store_.get(uri);
  if (!doc) return json::Value(symbols);

  ensure_analyzed(*doc);

  for (const auto &sym : doc->analysis.symbols.symbols) {
    json::Object item;
    item["name"] = json::Value::string(sym.name);

    int kind = 13; // Variable
    switch (sym.kind) {
    case SymbolKind::Function:
      kind = 12;
      break;
    case SymbolKind::Struct:
      kind = 23;
      break;
    case SymbolKind::Enum:
      kind = 10;
      break;
    case SymbolKind::Variable:
    case SymbolKind::Parameter:
      kind = 13;
      break;
    case SymbolKind::Namespace:
      kind = 3;
      break;
    }
    item["kind"] = json::Value::number(kind);

    int line = sym.location.line > 0 ? sym.location.line - 1 : 0;
    int col = sym.location.column > 0 ? sym.location.column - 1 : 0;
    json::Object range;
    range["start"] = protocol::position(line, col);
    range["end"] = protocol::position(line, col + static_cast<int>(sym.name.size()));
    item["range"] = json::Value(range);
    item["selectionRange"] = json::Value(range);

    // Only include top-level symbols (functions, structs, enums)
    if (sym.kind == SymbolKind::Function || sym.kind == SymbolKind::Struct ||
        sym.kind == SymbolKind::Enum) {
      symbols.push_back(json::Value(item));
    }
  }

  return json::Value(symbols);
}

json::Value Server::handle_signature_help(const json::Value &params) {
  std::string uri = uri_from_params(params);
  auto [line, character] = position_from_params(params);
  auto *doc = store_.get(uri);
  if (!doc) return json::Value(json::Object{});

  ensure_analyzed(*doc);

  // Split text into lines
  std::vector<std::string> lines;
  std::string current;
  for (char c : doc->text) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (c != '\r') {
      current += c;
    }
  }
  lines.push_back(current);

  if (line < 0 || line >= static_cast<int>(lines.size())) return json::Value(json::Object{});
  const std::string &line_text = lines[static_cast<std::size_t>(line)];

  // Walk backwards from cursor to find the function name and count commas for active parameter
  int paren_depth = 0;
  int active_param = 0;
  int func_end = -1;
  for (int i = character - 1; i >= 0; --i) {
    char c = line_text[static_cast<std::size_t>(i)];
    if (c == ')') ++paren_depth;
    else if (c == '(') {
      if (paren_depth == 0) {
        func_end = i;
        break;
      }
      --paren_depth;
    } else if (c == ',' && paren_depth == 0) {
      ++active_param;
    }
  }
  if (func_end < 0) return json::Value(json::Object{});

  // Extract function name (walk back past whitespace and identifier chars)
  int name_end = func_end;
  int name_start = name_end;
  while (name_start > 0 && (std::isalnum(static_cast<unsigned char>(line_text[static_cast<std::size_t>(name_start - 1)])) ||
         line_text[static_cast<std::size_t>(name_start - 1)] == '_'))
    --name_start;
  std::string func_name = line_text.substr(static_cast<std::size_t>(name_start), static_cast<std::size_t>(name_end - name_start));
  if (func_name.empty()) return json::Value(json::Object{});

  // Look up the function in the symbol table
  auto visible = doc->analysis.symbols.visible_at(line + 1);
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Function && sym->name == func_name) {
      // Build signature
      std::string sig = sym->return_type + " " + sym->name + "(";
      json::Array param_infos;
      for (std::size_t i = 0; i < sym->params.size(); ++i) {
        std::string param_str = sym->params[i].type.to_string() + " " + sym->params[i].name;
        if (i > 0) sig += ", ";
        sig += param_str;
        json::Object pi;
        pi["label"] = json::Value::string(param_str);
        param_infos.push_back(json::Value(pi));
      }
      sig += ")";

      json::Object sig_info;
      sig_info["label"] = json::Value::string(sig);
      sig_info["parameters"] = json::Value(param_infos);

      json::Array signatures;
      signatures.push_back(json::Value(sig_info));

      json::Object result;
      result["signatures"] = json::Value(signatures);
      result["activeSignature"] = json::Value::number(0);
      result["activeParameter"] = json::Value::number(active_param);
      return json::Value(result);
    }
  }

  return json::Value(json::Object{});
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
      content += sym->params[i].type.to_string() + " " + sym->params[i].name;
    }
    content += ")";
  } else if (sym->kind == SymbolKind::Struct) {
    content = "struct " + sym->name + " {\n";
    for (const auto &field : sym->fields) {
      content += "  " + field.type_name + " " + field.name + ";\n";
    }
    content += "}";
  } else if (sym->kind == SymbolKind::Enum) {
    content = "enum " + sym->name + " {\n";
    for (std::size_t i = 0; i < sym->variants.size(); ++i) {
      content += "  " + sym->variants[i];
      if (i + 1 < sym->variants.size()) content += ",";
      content += "\n";
    }
    content += "}";
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
