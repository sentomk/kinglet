#include "lsp/completion_resolver.h"

#include "lsp/protocol.h"
#include "module/module_loader.h"

#include <cctype>
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
  case CompletionPosition::None:
    return json::Array{};
  case CompletionPosition::UsingNamespace:
    return resolve_using_namespace();
  case CompletionPosition::TopLevelDecl:
    return resolve_top_level();
  case CompletionPosition::Statement:
    return resolve_statement();
  case CompletionPosition::ExpressionStart:
    return resolve_expression();
  case CompletionPosition::TypeExpr:
  case CompletionPosition::StructFieldDecl:
    return resolve_type_expr();
  case CompletionPosition::ParameterType:
    return resolve_param_type();
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
  case CompletionPosition::ImplTarget:
    return resolve_impl_target();
  case CompletionPosition::MatchArm:
    return resolve_enum_variant(info.receiver_type);
  case CompletionPosition::StructLiteral:
    return resolve_struct_literal(info.struct_name);
  case CompletionPosition::EnumVariant:
    return resolve_enum_variant({});
  }
  return json::Array{};
}

void CompletionResolver::add_scope_symbols(json::Array &items) {
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (!matches_prefix(sym->name)) continue;
    // Skip names that are not valid identifiers (e.g. stray punctuation a
    // recovering parser may have registered from an incomplete expression).
    if (sym->name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(sym->name[0])) || sym->name[0] == '_'))
      continue;
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
  add_type_keywords(items, /*include_void_auto=*/true);
}

void CompletionResolver::add_type_keywords(json::Array &items, bool include_void_auto) {
  for (const char *kw : {"int", "float", "double", "bool", "string", "void", "byte", "auto"}) {
    if (!include_void_auto && (std::string(kw) == "void" || std::string(kw) == "auto"))
      continue;
    if (!matches_prefix(kw)) continue;
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14);
    item["insertText"] = json::Value::string(std::string(kw) + " ");
    items.push_back(json::Value(item));
  }
}

void CompletionResolver::add_statement_keywords(json::Array &items) {
  // Keywords valid inside a function body (statement position).
  const char *kw_with_space[] = {"if",    "else", "for",   "while",
                                  "return", "match", "let",  "const"};
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

void CompletionResolver::add_decl_keywords(json::Array &items) {
  // Top-level declaration keywords (not valid inside a function body).
  for (const char *kw : {"import", "pub", "export", "using", "struct", "enum",
                         "trait", "impl"}) {
    if (!matches_prefix(kw)) continue;
    json::Object item;
    item["label"] = json::Value::string(kw);
    item["kind"] = json::Value::number(14);
    item["insertText"] = json::Value::string(std::string(kw) + " ");
    items.push_back(json::Value(item));
  }
}

void CompletionResolver::add_namespace_completions(json::Array &items) {
  for (const char *ns : {"io", "fs", "sys"}) {
    if (!analysis_.used_namespaces.count(ns) && !analysis_.opened_namespaces.count(ns))
      continue;
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
  // Imported namespaces (`import "..." as foo;`) — insert "foo::" and re-trigger.
  for (const auto &ns : analysis_.imported_namespaces) {
    if (!matches_prefix(ns)) continue;
    json::Object item;
    item["label"] = json::Value::string(ns);
    item["kind"] = json::Value::number(9);
    item["detail"] = json::Value::string("module " + ns);
    item["insertText"] = json::Value::string(ns + "::");
    json::Object cmd;
    cmd["title"] = json::Value::string("");
    cmd["command"] = json::Value::string("editor.action.triggerSuggest");
    item["command"] = json::Value(cmd);
    items.push_back(json::Value(item));
  }
}

json::Array CompletionResolver::resolve_top_level() {
  json::Array items;
  add_scope_symbols(items);
  add_decl_keywords(items);
  add_namespace_completions(items);
  // Declaration-only snippets (no control-flow statement snippets at file scope).
  struct Snippet { const char *label; const char *body; const char *detail; };
  Snippet snippets[] = {
    {"fun", "${1:int} ${2:name}(${3:params}) {\n\t$0\n}", "function declaration"},
    {"struct", "struct ${1:Name} {\n\t$0\n}", "struct definition"},
    {"enum", "enum ${1:Name} {\n\t$0\n}", "enum definition"},
    {"trait", "trait ${1:Name} {\n\t$0\n}", "trait definition"},
    {"impl", "impl ${1:Type} {\n\t$0\n}", "impl block"},
    {"impl trait", "impl ${1:Type} : ${2:Trait} {\n\t$0\n}", "trait implementation"},
    {"using", "using ${1:io};$0", "using declaration"},
    {"import", "import \"${1:file.kl}\"$0", "import module"},
    {"main", "int main() {\n\t$0\n\treturn 0;\n}", "main function"},
  };
  for (const auto &s : snippets) {
    if (!matches_prefix(s.label)) continue;
    items.push_back(protocol::completion_item(s.label, 15, s.detail, s.body, 2));
  }
  return items;
}

json::Array CompletionResolver::resolve_statement() {
  json::Array items;
  add_scope_symbols(items);
  add_io_members(items);
  add_statement_keywords(items);
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

json::Array CompletionResolver::resolve_param_type() {
  // Parameter types: same as a type expression, but `void` and `auto` are
  // never valid as a parameter type, so they are excluded.
  json::Array items;
  add_type_keywords(items, /*include_void_auto=*/false);
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

// Resolve a single member (field or method) of `type_name` to the type it
// yields. `is_call` selects a method's return type; otherwise a struct field's
// type. Returns empty if unresolved.
std::string CompletionResolver::member_type(const std::string &type_name,
                                            const std::string &member,
                                            bool is_call) {
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  if (is_call) {
    std::string qualified = type_name + "::" + member;
    for (const auto *sym : visible) {
      if (sym->kind == SymbolKind::Function && sym->name == qualified)
        return sym->return_type;
    }
    return {};
  }
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Struct && sym->name == type_name) {
      for (const auto &field : sym->fields) {
        if (field.name == member) return field.type_name;
      }
    }
  }
  return {};
}

// Walk a parser-encoded access chain ("base\x1fseg\x1fseg()") to a concrete
// type name. The base resolves through a variable's declared type; each later
// segment resolves through a field type or method return type.
std::string CompletionResolver::walk_access_chain(const std::string &chain) {
  std::vector<std::string> segments;
  std::size_t start = 0;
  while (true) {
    std::size_t sep = chain.find('\x1f', start);
    segments.push_back(chain.substr(start, sep - start));
    if (sep == std::string::npos) break;
    start = sep + 1;
  }
  if (segments.empty()) return {};

  // Resolve the base segment (a variable or type name) to a type.
  std::string base = segments[0];
  std::string cur_type = base;
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  bool is_type = false;
  for (const auto *sym : visible) {
    if ((sym->kind == SymbolKind::Struct || sym->kind == SymbolKind::Trait) &&
        sym->name == base) { is_type = true; break; }
  }
  if (!is_type) {
    for (const auto *sym : visible) {
      if (sym->name == base && !sym->type_name.empty()) {
        cur_type = sym->type_name;
        break;
      }
    }
  }

  // Walk the remaining segments.
  for (std::size_t i = 1; i < segments.size(); ++i) {
    std::string seg = segments[i];
    bool is_call = false;
    if (seg.size() >= 2 && seg.compare(seg.size() - 2, 2, "()") == 0) {
      is_call = true;
      seg = seg.substr(0, seg.size() - 2);
    }
    cur_type = member_type(cur_type, seg, is_call);
    if (cur_type.empty()) return {};
  }
  return cur_type;
}

json::Array CompletionResolver::resolve_field_access(const std::string &receiver_type) {
  json::Array items;
  if (receiver_type.empty()) return items;

  // A receiver may be an access chain (e.g. "r\x1fscale()") encoded by the
  // parser; walk it to a concrete type before resolving members.
  std::string rt = receiver_type;
  if (receiver_type.find('\x1f') != std::string::npos) {
    rt = walk_access_chain(receiver_type);
    if (rt.empty()) return items;
  }

  // Built-in io stream objects (io::out / io::err / io::in).
  if (rt == "$io_ostream") {
    if (matches_prefix("line"))
      items.push_back(protocol::completion_item("line", 3, "print with newline", "line($1)", 2));
    return items;
  }
  if (rt == "$io_istream") {
    if (matches_prefix("secret"))
      items.push_back(protocol::completion_item("secret", 3, "read without echo", "secret($1)", 2));
    return items;
  }

  auto visible = analysis_.symbols.visible_at(line_ + 1);

  // rt may be a type name directly (e.g. "Rect" from self) or a variable name.
  // Resolve variable name to its type if needed.
  std::string type_name = rt;
  bool found_as_type = false;
  for (const auto *sym : visible) {
    if ((sym->kind == SymbolKind::Struct || sym->kind == SymbolKind::Trait) &&
        sym->name == rt) {
      found_as_type = true;
      break;
    }
  }
  if (!found_as_type) {
    for (const auto *sym : visible) {
      if (sym->name == rt && !sym->type_name.empty()) {
        type_name = sym->type_name;
        break;
      }
    }
  }

  if (type_name == "string") {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"len", "int — string length"},
             {"contains", "bool — check substring"},
             {"starts_with", "bool — check prefix"},
             {"ends_with", "bool — check suffix"},
             {"index_of", "int — find substring"},
             {"slice", "string — substring"},
             {"replace", "string — replace all"},
             {"split", "string[] — split by delimiter"},
             {"trim", "string — trim whitespace"},
             {"to_upper", "string — uppercase"},
             {"to_lower", "string — lowercase"}}) {
      if (!matches_prefix(name)) continue;
      items.push_back(protocol::completion_item(name, 2, detail, name + "($1)", 2));
    }
    return items;
  }

  if (type_name.size() >= 2 && type_name.substr(type_name.size() - 2) == "[]") {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"len", "int — array length"},
             {"push", "void — append element"},
             {"pop", "T — remove last"},
             {"remove", "T — remove at index"},
             {"contains", "bool — check membership"},
             {"clear", "void — remove all"},
             {"insert", "void — insert at index"},
             {"index_of", "int — find element"},
             {"slice", "T[] — sub-array"},
             {"reverse", "void — reverse in place"},
             {"resize", "void — grow/shrink to n, filling with default"}}) {
      if (!matches_prefix(name)) continue;
      items.push_back(protocol::completion_item(name, 2, detail, name + "($1)", 2));
    }
    return items;
  }

  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Struct && sym->name == type_name) {
      for (const auto &field : sym->fields) {
        if (!matches_prefix(field.name)) continue;
        items.push_back(protocol::completion_item(field.name, 5, field.type_name));
      }
    }
    if (sym->kind == SymbolKind::Function) {
      std::string method_prefix = type_name + "::";
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
  if (ns_name == "fs") {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"__read", "fs::__read(path) — read whole file as string, null on failure"},
             {"__write", "fs::__write(path, content) — write string to file"}}) {
      if (!matches_prefix(name)) continue;
      items.push_back(protocol::completion_item(name, 3, detail));
    }
  }
  if (ns_name == "sys") {
    for (const auto &[name, detail] : std::vector<std::pair<std::string, std::string>>{
             {"args", "sys::args() — command-line arguments as string[]"}}) {
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

  // EnumName:: — suggest the enum's variants.
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Enum && sym->name == ns_name) {
      for (const auto &variant : sym->variants) {
        if (!matches_prefix(variant)) continue;
        items.push_back(protocol::completion_item(variant, 20, ns_name + " variant"));
      }
      break;
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
  // A method's return type may be a user-defined type, so offer struct / enum /
  // trait names alongside the built-in type keywords.
  auto type_scope = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : type_scope) {
    if (sym->kind != SymbolKind::Struct && sym->kind != SymbolKind::Enum &&
        sym->kind != SymbolKind::Trait)
      continue;
    if (!matches_prefix(sym->name)) continue;
    int kind = (sym->kind == SymbolKind::Struct) ? 22 :
               (sym->kind == SymbolKind::Enum) ? 13 : 8;
    items.push_back(protocol::completion_item(sym->name, kind, sym->name));
  }
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

json::Array CompletionResolver::resolve_impl_target() {
  // `impl <Type>` — only structs and enums can be the target of an impl block.
  json::Array items;
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  for (const auto *sym : visible) {
    if (sym->kind != SymbolKind::Struct && sym->kind != SymbolKind::Enum) continue;
    if (!matches_prefix(sym->name)) continue;
    int kind = (sym->kind == SymbolKind::Struct) ? 22 : 13;
    items.push_back(protocol::completion_item(sym->name, kind, sym->name));
  }
  return items;
}

json::Array CompletionResolver::resolve_using_namespace() {
  // `using <namespace>` — offer the built-in io namespace plus any namespaces
  // brought in via imports.
  json::Array items;
  std::set<std::string> names;
  names.insert("io");
  names.insert("fs");
  names.insert("sys");
  // Namespaces made available through imports. used_namespaces is intentionally
  // excluded: it is populated by `using` statements themselves (including the
  // partially-typed one being completed), which would echo back as noise.
  for (const auto &ns : analysis_.imported_namespaces) names.insert(ns);
  for (const auto &name : names) {
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
      continue;
    if (!matches_prefix(name)) continue;
    items.push_back(protocol::completion_item(name, 9, "namespace"));
  }
  return items;
}

json::Array CompletionResolver::resolve_struct_literal(const std::string &struct_name) {
  json::Array items;
  auto visible = analysis_.symbols.visible_at(line_ + 1);
  // Field names act as hints for what each positional slot expects.
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Struct && sym->name == struct_name) {
      for (const auto &field : sym->fields) {
        if (!matches_prefix(field.name)) continue;
        items.push_back(protocol::completion_item(field.name, 5,
                                                  "field — " + field.type_name));
      }
      break;
    }
  }
  // Slots are positional value expressions, so offer self / locals / types too.
  add_scope_symbols(items);
  return items;
}

json::Array CompletionResolver::resolve_enum_variant(const std::string &subject_name) {
  json::Array items;
  if (subject_name.empty()) return items;

  auto visible = analysis_.symbols.visible_at(line_ + 1);

  // Resolve the match subject to its enum type. The subject may be a variable
  // or parameter (resolve through its declared type), or a type name directly.
  std::string enum_type = subject_name;
  for (const auto *sym : visible) {
    if (sym->name == subject_name &&
        (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Parameter) &&
        !sym->type_name.empty()) {
      enum_type = sym->type_name;
      break;
    }
  }

  const Symbol *enum_sym = nullptr;
  for (const auto *sym : visible) {
    if (sym->kind == SymbolKind::Enum && sym->name == enum_type) {
      enum_sym = sym;
      break;
    }
  }
  if (!enum_sym) return items;

  for (std::size_t vi = 0; vi < enum_sym->variants.size(); ++vi) {
    const auto &v = enum_sym->variants[vi];
    if (!matches_prefix(v) && !matches_prefix(enum_sym->name)) continue;

    std::string label = enum_sym->name + "::" + v;
    std::string snippet = label;
    int param_count = vi < enum_sym->variant_param_counts.size()
                          ? enum_sym->variant_param_counts[vi]
                          : 0;
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
  return items;
}

} // namespace kinglet::lsp