#include "lsp/completion_resolver.h"

#include "lsp/protocol.h"
#include "module/module_loader.h"

#include <filesystem>
#include <set>
#include <sstream>

namespace kinglet::lsp {

CompletionResolver::CompletionResolver(const AnalysisResult &analysis,
                                       const std::string &prefix, int line,
                                       int character, const std::string &uri,
                                       const std::string &source)
    : analysis_(analysis), prefix_(prefix), line_(line),
      character_(character), uri_(uri), source_(source) {}

bool CompletionResolver::matches_prefix(const std::string &name) const {
  if (prefix_.empty()) return true;
  return name.find(prefix_) != std::string::npos;
}

json::Array CompletionResolver::resolve(const CompletionInfo &info) {
  switch (info.position) {
  case CompletionPosition::TopLevelDecl:
    return resolve_top_level();
  case CompletionPosition::Statement:
    return resolve_statement();
  case CompletionPosition::ExpressionStart:
    return resolve_expression();
  case CompletionPosition::TypeExpr:
  case CompletionPosition::ParameterType:
  case CompletionPosition::StructFieldDecl:
    return resolve_type_expr();
  case CompletionPosition::FieldAccess:
    return resolve_field_access(info.receiver_type);
  case CompletionPosition::NamespaceAccess:
    return resolve_namespace_access(info.ns_name);
  case CompletionPosition::ImportPath:
    return resolve_import_path();
  case CompletionPosition::ImportSymbol:
    return resolve_import_symbol(info.import_path);
  case CompletionPosition::ImplMethodDecl:
    return resolve_impl_method(info.enclosing_type, info.trait_name);
  case CompletionPosition::TraitMethodDecl:
    return resolve_type_expr();
  case CompletionPosition::TraitName:
    return resolve_trait_name();
  case CompletionPosition::MatchArm:
    return resolve_expression();
  case CompletionPosition::StructLiteral:
    return resolve_struct_literal(info.struct_name);
  case CompletionPosition::EnumVariant:
    return resolve_enum_variant();
  }
  return json::Array{};
}

void CompletionResolver::add_scope_symbols(json::Array &items) {
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (!matches_prefix(sym->name)) continue;
    bool sym_is_qualified = sym->name.find("::") != std::string::npos;
    if (sym_is_qualified) continue;
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
}

void CompletionResolver::add_io_members(json::Array &items) {
  if (analysis_.opened_namespaces.count("io")) {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"out", "io::out — stdout output"},
             {"err", "io::err — stderr output"},
             {"in", "io::in — stdin input"}}) {
      if (!matches_prefix(name)) continue;
      items.push_back(protocol::completion_item(name, 3, detail));
    }
  } else if (analysis_.used_namespaces.count("io")) {
    for (const auto &[name, qualified, detail] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"out", "io::out", "io::out — stdout output"},
             {"err", "io::err", "io::err — stderr output"},
             {"in", "io::in", "io::in — stdin input"}}) {
      if (!matches_prefix(name)) continue;
      json::Object item;
      item["label"] = json::Value::string(name);
      item["kind"] = json::Value::number(3);
      item["detail"] = json::Value::string(detail);
      item["insertText"] = json::Value::string(qualified);
      item["filterText"] = json::Value::string(name);
      items.push_back(json::Value(item));
    }
  }
}

void CompletionResolver::add_type_keywords(json::Array &items) {
  for (const char *kw : {"int", "float", "double", "bool", "string", "void", "byte", "auto"}) {
    if (!matches_prefix(kw)) continue;
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14);
    item["insertText"] = json::Value::string(std::string(kw) + " ");
    items.push_back(json::Value(item));
  }
}

void CompletionResolver::add_control_keywords(json::Array &items) {
  const char *kw_with_space[] = {"if", "else", "for", "while", "return",
                                  "match", "let", "const", "import", "pub",
                                  "export", "using",
                                  "struct", "enum", "trait", "impl", "spawn", "select"};
  const char *kw_standalone[] = {"break", "continue", "true", "false", "null"};
  for (const char *kw : kw_with_space) {
    if (!matches_prefix(kw)) continue;
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14);
    item["insertText"] = json::Value::string(std::string(kw) + " ");
    items.push_back(json::Value(item));
  }
  for (const char *kw : kw_standalone) {
    if (!matches_prefix(kw)) continue;
    items.push_back(protocol::completion_item(kw, 14));
  }
}

void CompletionResolver::add_snippets(json::Array &items) {
  struct Snippet { const char *label; const char *body; const char *detail; };
  Snippet snippets[] = {
    {"if", "if (${1:condition}) {\n\t$0\n}", "if statement"},
    {"if else", "if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}", "if-else statement"},
    {"for", "for (${1:int i = 0}; ${2:i < n}; ${3:i += 1}) {\n\t$0\n}", "for loop"},
    {"while", "while (${1:condition}) {\n\t$0\n}", "while loop"},
    {"match", "${1:expr} match {\n\t${2:_} => ${0:result}\n};", "pattern match"},
    {"fun", "${1:int} ${2:name}(${3:params}) {\n\t$0\n}", "function declaration"},
    {"struct", "struct ${1:Name} {\n\t$0\n}", "struct definition"},
    {"enum", "enum ${1:Name} {\n\t$0\n}", "enum definition"},
    {"trait", "trait ${1:Name} {\n\t$0\n}", "trait definition"},
    {"impl", "impl ${1:Type} {\n\t$0\n}", "impl block"},
    {"impl trait", "impl ${1:Type} : ${2:Trait} {\n\t$0\n}", "trait implementation"},
    {"using", "using ${1:io};$0", "using declaration"},
    {"import", "import \"${1:file.kl}\"$0", "import module"},
    {"main", "int main() {\n\t$0\n\treturn 0;\n}", "main function"},
    {"return", "return ${0:expr};", "return statement"},
  };
  for (const auto &s : snippets) {
    if (!matches_prefix(s.label)) continue;
    items.push_back(protocol::completion_item(s.label, 15, s.detail, s.body, 2));
  }
}

void CompletionResolver::add_namespace_completions(json::Array &items) {
  if (analysis_.used_namespaces.count("io") || analysis_.opened_namespaces.count("io")) {
    for (const char *ns : {"io"}) {
      if (!matches_prefix(ns)) continue;
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

json::Array CompletionResolver::resolve_top_level() {
  json::Array items;
  add_scope_symbols(items);
  add_io_members(items);
  add_snippets(items);
  add_type_keywords(items);
  add_control_keywords(items);
  add_namespace_completions(items);
  return items;
}

json::Array CompletionResolver::resolve_statement() {
  json::Array items;
  add_scope_symbols(items);
  add_io_members(items);
  add_snippets(items);
  add_type_keywords(items);
  add_control_keywords(items);
  add_namespace_completions(items);
  return items;
}

json::Array CompletionResolver::resolve_expression() {
  json::Array items;
  add_scope_symbols(items);
  add_io_members(items);
  add_namespace_completions(items);
  return items;
}

json::Array CompletionResolver::resolve_type_expr() {
  json::Array items;
  add_type_keywords(items);
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (sym->kind != SymbolKind::Struct && sym->kind != SymbolKind::Enum &&
        sym->kind != SymbolKind::Trait)
      continue;
    if (!matches_prefix(sym->name)) continue;
    int kind = (sym->kind == SymbolKind::Struct) ? 22 :
               (sym->kind == SymbolKind::Enum) ? 13 : 8;
    items.push_back(protocol::completion_item(sym->name, kind, sym->type_name));
  }
  return items;
}

json::Array CompletionResolver::resolve_field_access(const std::string &receiver_type) {
  json::Array items;
  if (receiver_type.empty()) return items;
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Struct && sym->name == receiver_type) {
      for (const auto &field : sym->fields) {
        if (!matches_prefix(field.name)) continue;
        items.push_back(protocol::completion_item(field.name, 5, field.type_name));
      }
    }
    if (sym->kind == SymbolKind::Function) {
      std::string method_prefix = receiver_type + "::";
      if (sym->name.size() > method_prefix.size() &&
          sym->name.compare(0, method_prefix.size(), method_prefix) == 0) {
        std::string method_name = sym->name.substr(method_prefix.size());
        if (!matches_prefix(method_name)) continue;
        std::string detail = sym->return_type + " " + method_name + "(";
        std::string snippet = method_name + "(";
        int snippet_idx = 1;
        bool first = true;
        for (std::size_t i = 0; i < sym->params.size(); ++i) {
          if (sym->params[i].name == "self") continue;
          if (!first) { detail += ", "; snippet += ", "; }
          first = false;
          detail += sym->params[i].type.to_string() + " " + sym->params[i].name;
          snippet += "${" + std::to_string(snippet_idx++) + ":" + sym->params[i].name + "}";
        }
        detail += ")";
        snippet += ")";
        items.push_back(protocol::completion_item(method_name, 2, detail, snippet, 2));
      }
    }
    if (sym->kind == SymbolKind::Trait && sym->name == receiver_type) {
      for (const auto *fn : visible) {
        if (fn->kind != SymbolKind::Function) continue;
        std::string method_prefix = receiver_type + "::";
        if (fn->name.size() > method_prefix.size() &&
            fn->name.compare(0, method_prefix.size(), method_prefix) == 0) {
          std::string method_name = fn->name.substr(method_prefix.size());
          if (!matches_prefix(method_name)) continue;
          std::string detail = fn->return_type + " " + method_name + "(";
          std::string snippet = method_name + "(";
          int snippet_idx = 1;
          bool first = true;
          for (std::size_t i = 0; i < fn->params.size(); ++i) {
            if (fn->params[i].name == "self") continue;
            if (!first) { detail += ", "; snippet += ", "; }
            first = false;
            detail += fn->params[i].type.to_string() + " " + fn->params[i].name;
            snippet += "${" + std::to_string(snippet_idx++) + ":" + fn->params[i].name + "}";
          }
          detail += ")";
          snippet += ")";
          items.push_back(protocol::completion_item(method_name, 2, detail, snippet, 2));
        }
      }
    }
  }
  return items;
}

json::Array CompletionResolver::resolve_namespace_access(const std::string &ns_name) {
  json::Array items;
  if (ns_name == "io") {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"out", "io::out — stdout output"},
             {"err", "io::err — stderr output"},
             {"in", "io::in — stdin input"}}) {
      if (!matches_prefix(name)) continue;
      items.push_back(protocol::completion_item(name, 3, detail));
    }
  }
  auto it = analysis_.imported_symbols.find(ns_name);
  if (it != analysis_.imported_symbols.end()) {
    for (const auto &sym : it->second) {
      if (!matches_prefix(sym.name)) continue;
      int kind = (sym.kind == SymbolKind::Function) ? 3 : 6;
      items.push_back(protocol::completion_item(sym.name, kind, sym.type_name));
    }
  }
  return items;
}

json::Array CompletionResolver::resolve_import_path() {
  json::Array items;
  if (uri_.compare(0, 7, "file://") != 0) return items;
  std::string file_path = uri_.substr(7);
  std::filesystem::path current_path(file_path);
  std::string base_dir = current_path.parent_path().string();
  std::string current_name = current_path.filename().string();
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(base_dir, ec)) {
    if (!entry.is_regular_file()) continue;
    std::string filename = entry.path().filename().string();
    if (filename.size() < 3 || filename.substr(filename.size() - 3) != ".kl") continue;
    if (filename == current_name) continue;
    if (!matches_prefix(filename)) continue;
    items.push_back(protocol::completion_item(filename, 17, entry.path().string()));
  }
  return items;
}

json::Array CompletionResolver::resolve_import_symbol(const std::string &import_path) {
  json::Array items;
  if (uri_.compare(0, 7, "file://") != 0) return items;
  std::string file_path = uri_.substr(7);
  std::filesystem::path current_path(file_path);
  std::string base_dir = current_path.parent_path().string();
  ModuleLoader loader(base_dir);
  auto load_result = loader.load(import_path);
  if (!load_result.module) return items;
  for (const auto *fn : load_result.module->public_functions) {
    if (!matches_prefix(fn->name)) continue;
    items.push_back(protocol::completion_item(fn->name, 3, "function"));
  }
  for (const auto *st : load_result.module->public_structs) {
    if (!matches_prefix(st->name)) continue;
    items.push_back(protocol::completion_item(st->name, 22, "struct"));
  }
  for (const auto *en : load_result.module->public_enums) {
    if (!matches_prefix(en->name)) continue;
    items.push_back(protocol::completion_item(en->name, 13, "enum"));
  }
  return items;
}

json::Array CompletionResolver::resolve_impl_method(const std::string &target_type,
                                                    const std::string &trait_name) {
  json::Array items;
  add_type_keywords(items);
  if (trait_name.empty()) return items;

  auto visible = analysis_.symbols.visible_at(line_ + 1);
  const ast::TraitDecl *trait_decl = nullptr;
  if (analysis_.program) {
    for (const auto &decl : analysis_.program->declarations) {
      if (const auto *td = dynamic_cast<const ast::TraitDecl *>(decl.get())) {
        if (td->name == trait_name) { trait_decl = td; break; }
      }
    }
  }
  if (!trait_decl) return items;

  std::set<std::string> implemented;
  for (const auto *s : visible) {
    if (s->kind == SymbolKind::Function) {
      std::string method_prefix = target_type + "::";
      if (s->name.size() > method_prefix.size() &&
          s->name.compare(0, method_prefix.size(), method_prefix) == 0) {
        implemented.insert(s->name.substr(method_prefix.size()));
      }
    }
  }

  for (const auto &tm : trait_decl->methods) {
    if (implemented.count(tm.name)) continue;
    if (!matches_prefix(tm.name)) continue;
    std::string sig = tm.return_type.to_string() + " " + tm.name + "(";
    std::string snippet = tm.return_type.to_string() + " " + tm.name + "(";
    bool first = true;
    for (const auto &p : tm.params) {
      if (!first) { sig += ", "; snippet += ", "; }
      first = false;
      if (p.name == "self") {
        sig += "self";
        snippet += "self";
      } else {
        sig += p.type.to_string() + " " + p.name;
        snippet += p.type.to_string() + " " + p.name;
      }
    }
    sig += ")";
    snippet += ") {\n\t$0\n}";
    items.push_back(protocol::completion_item(tm.name, 2, sig, snippet, 2));
  }
  return items;
}

json::Array CompletionResolver::resolve_trait_name() {
  json::Array items;
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (sym->kind != SymbolKind::Trait) continue;
    if (!matches_prefix(sym->name)) continue;
    items.push_back(protocol::completion_item(sym->name, 8, "trait " + sym->name));
  }
  return items;
}

json::Array CompletionResolver::resolve_struct_literal(const std::string &struct_name) {
  json::Array items;
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Struct && sym->name == struct_name) {
      for (const auto &field : sym->fields) {
        if (!matches_prefix(field.name)) continue;
        std::string snippet = field.name + ": ${1:" + field.type_name + "}";
        items.push_back(protocol::completion_item(field.name, 5, field.type_name, snippet, 2));
      }
      break;
    }
  }
  return items;
}

json::Array CompletionResolver::resolve_enum_variant() {
  return json::Array{};
}

} // namespace kinglet::lsp