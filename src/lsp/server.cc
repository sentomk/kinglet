#include "lsp/server.h"

#include "lexer/scanner.h"
#include "lsp/completion_resolver.h"
#include "lsp/completion_token.h"
#include "lsp/protocol.h"
#include "parser/parser.h"

#include <cctype>
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
    auto result = handle_completion(params);
    json::Object list;
    list["isIncomplete"] = json::Value(false);
    list["items"] = result;
    std::cerr << "[LSP] completion: " << (result.is_array() ? result.as_array().size() : 0) << " items" << std::endl;
    send_response(id, json::Value(list));
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

  // Send pending diagnostics after handling the current request
  if (!pending_diagnostics_uri_.empty()) {
    auto *d = store_.get(pending_diagnostics_uri_);
    if (d) publish_diagnostics(*d);
    pending_diagnostics_uri_.clear();
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
  trigger_chars.push_back(json::Value::string("{"));
  trigger_chars.push_back(json::Value::string(","));
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
  pending_diagnostics_uri_ = uri;
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
  try {
    auto new_analysis = analyze(doc.text, uri_to_path(doc.uri));
    if (new_analysis.imported_namespaces.empty() && !doc.analysis.imported_namespaces.empty()) {
      new_analysis.imported_namespaces = std::move(doc.analysis.imported_namespaces);
      new_analysis.imported_symbols = std::move(doc.analysis.imported_symbols);
    }
    doc.analysis = std::move(new_analysis);
  } catch (const std::exception &e) {
    std::cerr << "[LSP] analysis error: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "[LSP] analysis error: unknown exception" << std::endl;
  }
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

  // Suppress completion inside string literals
  {
    std::string before_cursor = line_text.substr(0, static_cast<std::size_t>(character));
    int quote_count = 0;
    for (std::size_t i = 0; i < before_cursor.size(); ++i) {
      if (before_cursor[i] == '"' && (i == 0 || before_cursor[i - 1] != '\\')) {
        ++quote_count;
      }
    }
    if (quote_count % 2 == 1) {
      return json::Value(items);
    }
  }

  // Suppress completion inside comments
  {
    std::string before_cursor = line_text.substr(0, static_cast<std::size_t>(character));
    if (before_cursor.find("//") != std::string::npos) {
      return json::Value(items);
    }
  }

  // Parser-driven completion: inject COMPLETION token and let parser report context
  {
    auto token_result = inject_completion_token(doc->text, line, character);
    Parser parser(token_result.tokens, token_result.completion_index);
    parser.parse();
    if (parser.has_completion()) {
      const auto &info = parser.completion_result();

      // Suppress block-body completion when the cursor sits on the same line as
      // the opening brace (e.g. `struct Test {│`). Body completions are offered
      // only after a newline, once the brace is on a previous line. Applies to
      // every declaration body (struct/enum/trait/impl/function); struct literals
      // use a different position and keep their same-line field-value completion.
      switch (info.position) {
      case lsp::CompletionPosition::StructFieldDecl:
      case lsp::CompletionPosition::EnumVariant:
      case lsp::CompletionPosition::TraitMethodDecl:
      case lsp::CompletionPosition::ImplMethodDecl:
      case lsp::CompletionPosition::Statement: {
        std::size_t cut = static_cast<std::size_t>(character) < line_text.size()
                              ? static_cast<std::size_t>(character)
                              : line_text.size();
        std::string before = line_text.substr(0, cut);
        std::size_t last = before.find_last_not_of(" \t");
        if (last != std::string::npos && before[last] == '{') {
          return json::Value(items); // empty: same line as opening brace
        }
        break;
      }
      default:
        break;
      }

      CompletionResolver resolver(doc->analysis, token_result.prefix.empty() ? prefix : token_result.prefix,
                                  line, character, uri, doc->text);
      return json::Value(resolver.resolve(info));
    }
  }

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
    case SymbolKind::Trait:
      kind = 11;
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

    // Only include top-level symbols (functions, structs, enums, traits)
    if (sym.kind == SymbolKind::Function || sym.kind == SymbolKind::Struct ||
        sym.kind == SymbolKind::Enum || sym.kind == SymbolKind::Trait) {
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
  if (!sym) {
    for (const auto &s : doc->analysis.symbols.symbols) {
      auto sep = s.name.find("::");
      if (sep != std::string::npos && s.name.substr(sep + 2) == word) {
        sym = &s;
        break;
      }
    }
  }
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
  if (!sym) {
    for (const auto &s : doc->analysis.symbols.symbols) {
      auto sep = s.name.find("::");
      if (sep != std::string::npos && s.name.substr(sep + 2) == word) {
        sym = &s;
        break;
      }
    }
  }
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
    else if (sym.kind == SymbolKind::Trait) type_names.insert(sym.name);
    else if (sym.kind == SymbolKind::Enum) enum_names.insert(sym.name);
    else if (sym.kind == SymbolKind::Function) {
      func_names.insert(sym.name);
      auto colons = sym.name.find("::");
      if (colons != std::string::npos) {
        func_names.insert(sym.name.substr(colons + 2));
      }
    }
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
    case TokenType::IMPL:
    case TokenType::SPAWN: case TokenType::SELECT: case TokenType::TRUE:
    case TokenType::FALSE: case TokenType::NULL_:
      token_type = TT::Keyword;
      break;
    case TokenType::SELF:
      token_type = TT::Parameter;
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
      if (name == "self") token_type = TT::Parameter;
      else if (type_names.count(name)) token_type = TT::Type;
      else if (enum_names.count(name)) token_type = TT::Enum;
      else if (func_names.count(name)) token_type = TT::Function;
      else if (name == "io" || name == "fs" || name == "sys") token_type = TT::Namespace;
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
