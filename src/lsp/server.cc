#include "lsp/server.h"

#include "lexer/scanner.h"
#include "lsp/protocol.h"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <set>
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
  } else if (method == "textDocument/semanticTokens/full") {
    send_response(id, handle_semantic_tokens(params));
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
  trigger_chars.push_back(json::Value::string("\""));
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

  // Semantic tokens
  json::Object sem_tokens;
  json::Object sem_legend;
  json::Array token_types;
  for (const char *t : {"keyword", "function", "type", "enum", "enumMember",
                         "variable", "parameter", "string", "number", "comment",
                         "operator", "namespace"}) {
    token_types.push_back(json::Value::string(t));
  }
  sem_legend["tokenTypes"] = json::Value(token_types);
  sem_legend["tokenModifiers"] = json::Value(json::Array{});
  sem_tokens["legend"] = json::Value(sem_legend);
  sem_tokens["full"] = json::Value(true);
  capabilities["semanticTokensProvider"] = json::Value(sem_tokens);

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

static std::string uri_to_path(const std::string &uri) {
  const std::string prefix = "file://";
  if (uri.compare(0, prefix.size(), prefix) == 0) {
    return uri.substr(prefix.size());
  }
  return uri;
}

void Server::ensure_analyzed(Document &doc) {
  if (!doc.dirty) return;
  doc.analysis = analyze(doc.text, uri_to_path(doc.uri));
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

  // File name completion inside import "..."
  if (uri.compare(0, 7, "file://") == 0) {
    std::string before_cursor = line_text.substr(0, static_cast<std::size_t>(character));
    auto import_pos = before_cursor.rfind("import");
    if (import_pos != std::string::npos) {
      auto quote_pos = before_cursor.find('"', import_pos);
      if (quote_pos != std::string::npos && static_cast<int>(quote_pos) < character) {
        auto close_pos = before_cursor.find('"', quote_pos + 1);
        if (close_pos == std::string::npos || static_cast<int>(close_pos) >= character) {
          std::string file_prefix = before_cursor.substr(quote_pos + 1);
          std::filesystem::path current_path = uri_to_path(uri);
          std::string current_name = current_path.filename().string();
          std::string base_dir = current_path.parent_path().string();
          std::error_code ec;
          for (const auto &entry : std::filesystem::directory_iterator(base_dir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string filename = entry.path().filename().string();
            if (filename.size() < 3 || filename.substr(filename.size() - 3) != ".kl") continue;
            if (filename == current_name) continue;  // skip self
            if (!file_prefix.empty() && filename.find(file_prefix) == std::string::npos) continue;
            items.push_back(protocol::completion_item_with_edit(
                filename, 17, entry.path().string(), line,
                static_cast<int>(quote_pos) + 1, character));
          }
          if (!items.empty()) return json::Value(items);
        }
      }
    }
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
    items.push_back(protocol::completion_item_with_edit("out", 3, "stdout output", line, character, character));
    items.push_back(protocol::completion_item_with_edit("err", 3, "stderr output", line, character, character));
    items.push_back(protocol::completion_item_with_edit("in", 3, "stdin input", line, character, character));
    return json::Value(items);
  }

  if (doc->analysis.imported_namespaces.count(ns_name)) {
    auto it = doc->analysis.imported_symbols.find(ns_name);
    if (it != doc->analysis.imported_symbols.end()) {
      for (const auto &sym : it->second) {
        int kind = 6;
        if (sym.kind == SymbolKind::Function) kind = 3;
        else if (sym.kind == SymbolKind::Struct) kind = 22;
        else if (sym.kind == SymbolKind::Enum) kind = 13;
        std::string detail = sym.type_name;
        if (sym.kind == SymbolKind::Function) {
          detail = sym.return_type + " " + sym.name + "(";
          for (std::size_t i = 0; i < sym.params.size(); ++i) {
            if (i > 0) detail += ", ";
            detail += sym.params[i].type.to_string() + " " + sym.params[i].name;
          }
          detail += ")";
          std::string snippet = sym.name + "(";
          for (std::size_t i = 0; i < sym.params.size(); ++i) {
            if (i > 0) snippet += ", ";
            snippet += "${" + std::to_string(i + 1) + ":" + sym.params[i].name + "}";
          }
          snippet += ")";
          items.push_back(protocol::completion_item(sym.name, kind, detail, snippet, 2));
        } else {
          items.push_back(protocol::completion_item(sym.name, kind, detail));
        }
      }
      return json::Value(items);
    }
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

  // Match arm context: suggest uncovered enum variants
  if (!ns_name.empty()) {
    // Already handled above
  } else {
    // Detect if cursor is inside a match { ... } body
    // Scan backwards through lines to find "EXPR match {"
    std::string full_text_before;
    {
      std::istringstream s2(doc->text);
      std::string ln;
      int cur = 0;
      while (std::getline(s2, ln)) {
        if (cur < line) {
          full_text_before += ln + "\n";
        } else if (cur == line) {
          full_text_before += ln.substr(0, static_cast<std::size_t>(character));
        }
        ++cur;
      }
    }

    // Find the innermost unmatched "match {"
    int brace_depth = 0;
    std::size_t match_pos = std::string::npos;
    for (std::size_t i = full_text_before.size(); i > 0; --i) {
      char c = full_text_before[i - 1];
      if (c == '}') ++brace_depth;
      else if (c == '{') {
        if (brace_depth > 0) --brace_depth;
        else {
          // Check if "match" precedes this brace
          std::size_t brace_pos = i - 1;
          std::size_t j = brace_pos;
          while (j > 0 && std::isspace(static_cast<unsigned char>(full_text_before[j - 1]))) --j;
          if (j >= 5 && full_text_before.substr(j - 5, 5) == "match") {
            match_pos = j - 5;
            break;
          }
          // Not a match brace, stop searching
          break;
        }
      }
    }

    if (match_pos != std::string::npos) {
      // Find the expression before "match" to determine the type
      std::size_t expr_end = match_pos;
      while (expr_end > 0 && std::isspace(static_cast<unsigned char>(full_text_before[expr_end - 1]))) --expr_end;
      // Extract identifier before "match"
      std::size_t expr_start = expr_end;
      while (expr_start > 0 && (std::isalnum(static_cast<unsigned char>(full_text_before[expr_start - 1])) ||
             full_text_before[expr_start - 1] == '_')) --expr_start;
      std::string match_var = full_text_before.substr(expr_start, expr_end - expr_start);

      // Look up the variable's type in the symbol table
      const Symbol *var_sym = nullptr;
      auto visible_syms = doc->analysis.symbols.visible_at(line + 1);
      for (const auto *s : visible_syms) {
        if (s->name == match_var && (s->kind == SymbolKind::Variable || s->kind == SymbolKind::Parameter)) {
          var_sym = s;
          break;
        }
      }

      if (var_sym) {
        // Find the enum type
        const Symbol *enum_sym = nullptr;
        for (const auto *s : visible_syms) {
          if (s->kind == SymbolKind::Enum && s->name == var_sym->type_name) {
            enum_sym = s;
            break;
          }
        }

        if (enum_sym && !enum_sym->variants.empty()) {
          // Collect already-covered variants from text
          std::set<std::string> covered;
          std::string match_body = full_text_before.substr(match_pos);
          for (const auto &v : enum_sym->variants) {
            if (match_body.find(enum_sym->name + "::" + v) != std::string::npos) {
              covered.insert(v);
            }
          }

          // Suggest uncovered variants
          for (std::size_t vi = 0; vi < enum_sym->variants.size(); ++vi) {
            const auto &v = enum_sym->variants[vi];
            if (covered.count(v)) continue;
            if (!prefix.empty() && v.find(prefix) == std::string::npos &&
                enum_sym->name.find(prefix) == std::string::npos) continue;

            std::string label = enum_sym->name + "::" + v;
            std::string snippet = label;
            int param_count = vi < enum_sym->variant_param_counts.size()
                ? enum_sym->variant_param_counts[vi] : 0;
            if (param_count > 0) {
              snippet += "(";
              for (int pi = 0; pi < param_count; ++pi) {
                if (pi > 0) snippet += ", ";
                snippet += "let ${" + std::to_string(pi + 1) + ":v" + std::to_string(pi) + "}";
              }
              snippet += ")";
            }
            snippet += " => ${0:expr},";
            items.push_back(protocol::completion_item(label, 20, "enum variant", snippet, 2));
          }

          if (!items.empty()) {
            return json::Value(items);
          }
        }
      }
    }
  }

  // Struct literal field completion: StructName { field: ... }
  {
    // Scan backwards from cursor to find an unmatched '{'
    std::string before_cursor = line_text.substr(0, static_cast<std::size_t>(character));
    auto brace_pos = before_cursor.rfind('{');
    if (brace_pos != std::string::npos) {
      // Check if there's a struct name before the '{'
      std::size_t j = brace_pos;
      while (j > 0 && std::isspace(static_cast<unsigned char>(before_cursor[j - 1]))) --j;
      std::size_t name_end = j;
      while (j > 0 && (std::isalnum(static_cast<unsigned char>(before_cursor[j - 1])) || before_cursor[j - 1] == '_')) --j;
      std::string struct_name = before_cursor.substr(j, name_end - j);

      // Make sure it's not "match {" or control flow
      if (!struct_name.empty() && struct_name != "match" && struct_name != "if" &&
          struct_name != "else" && struct_name != "for" && struct_name != "while") {
        auto visible_syms = doc->analysis.symbols.visible_at(line + 1);
        const Symbol *struct_sym = nullptr;
        for (const auto *s : visible_syms) {
          if (s->kind == SymbolKind::Struct && s->name == struct_name) {
            struct_sym = s;
            break;
          }
        }

        if (struct_sym && !struct_sym->fields.empty()) {
          // Collect already-assigned fields
          std::string after_brace = before_cursor.substr(brace_pos + 1);
          std::set<std::string> assigned;
          std::size_t pos = 0;
          while (pos < after_brace.size()) {
            while (pos < after_brace.size() && !std::isalnum(static_cast<unsigned char>(after_brace[pos])) && after_brace[pos] != '_') ++pos;
            std::size_t start = pos;
            while (pos < after_brace.size() && (std::isalnum(static_cast<unsigned char>(after_brace[pos])) || after_brace[pos] == '_')) ++pos;
            std::string word = after_brace.substr(start, pos - start);
            if (pos < after_brace.size() && after_brace[pos] == ':') {
              assigned.insert(word);
            }
          }

          for (const auto &field : struct_sym->fields) {
            if (assigned.count(field.name)) continue;
            if (!prefix.empty() && field.name.find(prefix) == std::string::npos) continue;
            std::string snippet = field.name + ": ${1:" + field.type_name + "}";
            items.push_back(protocol::completion_item(field.name, 5, field.type_name, snippet, 2));
          }

          if (!items.empty()) {
            return json::Value(items);
          }
        }
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
            if (sym->type_name == "string") {
              items.push_back(protocol::snippet_item_with_edit("len", 2, "int — string length",
                  "len()", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("contains", 2, "bool — check substring",
                  "contains($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("starts_with", 2, "bool — check prefix",
                  "starts_with($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("ends_with", 2, "bool — check suffix",
                  "ends_with($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("index_of", 2, "int — find substring (-1 if not found)",
                  "index_of($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("slice", 2, "string — substring from start to end",
                  "slice($1, $2)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("replace", 2, "string — replace all occurrences",
                  "replace($1, $2)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("split", 2, "string[] — split by delimiter",
                  "split($1)", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("trim", 2, "string — remove leading/trailing whitespace",
                  "trim()", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("to_upper", 2, "string — convert to uppercase",
                  "to_upper()", line, character, character));
              items.push_back(protocol::snippet_item_with_edit("to_lower", 2, "string — convert to lowercase",
                  "to_lower()", line, character, character));
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
    {"match", "match {\n\t${1:_} => ${0:result},\n}", "pattern match", false},
    {"fun", "${1:int} ${2:name}(${3:params}) {\n\t$0\n}", "function declaration", true},
    {"struct", "struct ${1:Name} {\n\t$0\n}", "struct definition", true},
    {"enum", "enum ${1:Name} {\n\t$0\n}", "enum definition", true},
    {"using", "using ${1:io};$0", "using declaration", true},
    {"import", "import \"${1:file.kl}\"$0", "import module", true},
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

  bool has_expr_before_cursor = false;
  {
    std::size_t cursor_col = static_cast<std::size_t>(character);
    std::size_t prefix_start = cursor_col > prefix.size() ? cursor_col - prefix.size() : 0;
    for (std::size_t i = 0; i < prefix_start; ++i) {
      if (!std::isspace(static_cast<unsigned char>(line_text[i]))) {
        has_expr_before_cursor = true;
        break;
      }
    }
  }

  for (const auto &s : snippets) {
    if (!prefix.empty() && std::string(s.label).find(prefix) == std::string::npos) continue;
    if (std::string(s.label) == "match") {
      const char *body = has_expr_before_cursor
          ? "match {\n\t${1:_} => ${0:result}\n};"
          : "${1:expr} match {\n\t${2:_} => ${0:result}\n};";
      items.push_back(protocol::completion_item(s.label, 15, s.detail, body, 2));
      continue;
    }
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
                                  "match", "let", "const", "import", "pub",
                                  "export", "using",
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
    if (sym.name.empty()) continue;
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

json::Value Server::handle_semantic_tokens(const json::Value &params) {
  std::string uri = uri_from_params(params);
  auto *doc = store_.get(uri);
  if (!doc) {
    json::Object result;
    result["data"] = json::Value(json::Array{});
    return json::Value(result);
  }

  ensure_analyzed(*doc);

  // Token type indices (must match legend order)
  enum TT { Keyword=0, Function=1, Type=2, Enum=3, EnumMember=4,
            Variable=5, Parameter=6, String=7, Number=8, Comment=9,
            Operator=10, Namespace=11 };

  // Build a set of known type/enum/function names for classification
  std::set<std::string> type_names;
  std::set<std::string> enum_names;
  std::set<std::string> func_names;
  for (const auto &sym : doc->analysis.symbols.symbols) {
    if (sym.kind == SymbolKind::Struct) type_names.insert(sym.name);
    else if (sym.kind == SymbolKind::Enum) enum_names.insert(sym.name);
    else if (sym.kind == SymbolKind::Function) func_names.insert(sym.name);
  }

  // Scan tokens
  kinglet::Scanner scanner(doc->text);
  auto tokens = scanner.scan_tokens();

  json::Array data;
  int prev_line = 0;
  int prev_start = 0;

  for (const auto &tok : tokens) {
    if (tok.type == TokenType::END_OF_FILE || tok.type == TokenType::ERROR) continue;
    if (tok.lexeme.empty()) continue;

    int token_type = -1;

    switch (tok.type) {
    case TokenType::AUTO: case TokenType::INT: case TokenType::FLOAT:
    case TokenType::DOUBLE: case TokenType::BOOL: case TokenType::STRING:
    case TokenType::VOID: case TokenType::BYTE: case TokenType::CONST:
    case TokenType::RETURN: case TokenType::IF: case TokenType::ELSE:
    case TokenType::FOR: case TokenType::WHILE: case TokenType::BREAK:
    case TokenType::CONTINUE: case TokenType::GUARD: case TokenType::MATCH:
    case TokenType::PUB: case TokenType::LET: case TokenType::WHEN:
    case TokenType::IMPORT: case TokenType::EXPORT: case TokenType::NAMESPACE:
    case TokenType::USING:
    case TokenType::STRUCT: case TokenType::ENUM: case TokenType::TRAIT:
    case TokenType::SPAWN: case TokenType::SELECT: case TokenType::TRUE:
    case TokenType::FALSE: case TokenType::NULL_:
      token_type = TT::Keyword;
      break;
    case TokenType::STRING_LIT:
      token_type = TT::String;
      break;
    case TokenType::INTEGER: case TokenType::FLOAT_LIT: case TokenType::CHAR_LIT:
      token_type = TT::Number;
      break;
    case TokenType::PLUS: case TokenType::MINUS: case TokenType::STAR:
    case TokenType::SLASH: case TokenType::PERCENT: case TokenType::EQUAL:
    case TokenType::EQUAL_EQUAL: case TokenType::BANG_EQUAL:
    case TokenType::LESS: case TokenType::GREATER:
    case TokenType::LESS_EQUAL: case TokenType::GREATER_EQUAL:
    case TokenType::AMP_AMP: case TokenType::PIPE_PIPE: case TokenType::BANG:
    case TokenType::AMP: case TokenType::PIPE: case TokenType::CARET:
    case TokenType::TILDE: case TokenType::PIPE_GREATER:
    case TokenType::FAT_ARROW: case TokenType::ARROW:
      token_type = TT::Operator;
      break;
    case TokenType::IDENTIFIER: {
      std::string name(tok.lexeme);
      if (type_names.count(name)) token_type = TT::Type;
      else if (enum_names.count(name)) token_type = TT::Enum;
      else if (func_names.count(name)) token_type = TT::Function;
      else if (name == "io") token_type = TT::Namespace;
      else token_type = TT::Variable;
      break;
    }
    default:
      break;
    }

    if (token_type < 0) continue;

    int tok_line = tok.line - 1;
    int tok_col = tok.column - 1;
    int length = static_cast<int>(tok.lexeme.size());

    int delta_line = tok_line - prev_line;
    int delta_start = (delta_line == 0) ? (tok_col - prev_start) : tok_col;

    data.push_back(json::Value::number(delta_line));
    data.push_back(json::Value::number(delta_start));
    data.push_back(json::Value::number(length));
    data.push_back(json::Value::number(token_type));
    data.push_back(json::Value::number(0));

    prev_line = tok_line;
    prev_start = tok_col;
  }

  json::Object result;
  result["data"] = json::Value(data);
  return json::Value(result);
}

} // namespace kinglet::lsp
