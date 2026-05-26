#include "lsp/analysis.h"

#include "checker/type_checker.h"
#include "lexer/scanner.h"
#include "module/module_loader.h"

#include <filesystem>

namespace kinglet::lsp {

namespace {

class SymbolCollector {
public:
  void collect(const ast::Program &program) {
    for (const auto &decl : program.declarations) {
      visit_decl(*decl);
    }
  }

  SymbolTable take() { return std::move(table_); }

private:
  void visit_decl(const ast::Decl &decl) {
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(&decl)) {
      Symbol sym;
      sym.name = func->name;
      sym.kind = SymbolKind::Function;
      sym.type_name = func->return_type.to_string();
      sym.location = func->location;
      sym.return_type = func->return_type.to_string();
      sym.params = func->params;
      sym.scope_start_line = 0;
      sym.scope_end_line = 999999;
      table_.symbols.push_back(std::move(sym));

      for (const auto &param : func->params) {
        Symbol psym;
        psym.name = param.name;
        psym.kind = SymbolKind::Parameter;
        psym.type_name = param.type.to_string();
        psym.location = func->location;
        psym.scope_start_line = func->location.line;
        psym.scope_end_line = 999999;
        table_.symbols.push_back(std::move(psym));
      }

      if (func->body) {
        visit_stmt(*func->body, func->location.line);
      }
    } else if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(&decl)) {
      Symbol sym;
      sym.name = struct_decl->name;
      sym.kind = SymbolKind::Struct;
      sym.type_name = "struct";
      sym.location = struct_decl->location;
      sym.scope_start_line = 0;
      sym.scope_end_line = 999999;
      for (const auto &field : struct_decl->fields) {
        sym.fields.push_back(FieldSymbol{field.name, field.type.to_string()});
      }
      table_.symbols.push_back(std::move(sym));
    } else if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(&decl)) {
      Symbol sym;
      sym.name = enum_decl->name;
      sym.kind = SymbolKind::Enum;
      sym.type_name = "enum";
      sym.location = enum_decl->location;
      sym.scope_start_line = 0;
      sym.scope_end_line = 999999;
      sym.variants.clear();
      for (const auto &v : enum_decl->variants) {
        sym.variants.push_back(v.name);
        sym.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
      }
      table_.symbols.push_back(std::move(sym));
    } else if (const auto *top = dynamic_cast<const ast::TopLevelStmtDecl *>(&decl)) {
      visit_stmt(*top->stmt, 0);
    }
  }
// PLACEHOLDER_CONTINUE

  void visit_stmt(const ast::Stmt &stmt, int scope_start) {
    if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
      for (const auto &s : block->statements) {
        visit_stmt(*s, scope_start);
      }
    } else if (const auto *var = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
      Symbol sym;
      sym.name = var->name;
      sym.kind = SymbolKind::Variable;
      sym.type_name = var->type.to_string();
      sym.location = var->location;
      sym.scope_start_line = var->location.line;
      sym.scope_end_line = 999999;
      table_.symbols.push_back(std::move(sym));
    } else if (const auto *if_s = dynamic_cast<const ast::IfStmt *>(&stmt)) {
      if (if_s->then_branch) visit_stmt(*if_s->then_branch, scope_start);
      if (if_s->else_branch) visit_stmt(*if_s->else_branch, scope_start);
    } else if (const auto *while_s = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
      if (while_s->body) visit_stmt(*while_s->body, scope_start);
    } else if (const auto *for_s = dynamic_cast<const ast::ForStmt *>(&stmt)) {
      if (for_s->init) visit_stmt(*for_s->init, scope_start);
      if (for_s->body) visit_stmt(*for_s->body, scope_start);
    }
  }

  SymbolTable table_;
};

} // namespace

std::vector<const Symbol *> SymbolTable::visible_at(int line) const {
  std::vector<const Symbol *> result;
  std::set<std::string> seen;
  for (auto it = symbols.rbegin(); it != symbols.rend(); ++it) {
    if (it->scope_start_line <= line && line <= it->scope_end_line) {
      if (seen.insert(it->name).second) {
        result.push_back(&*it);
      }
    }
  }
  return result;
}

const Symbol *SymbolTable::find_definition(const std::string &name, int line) const {
  const Symbol *best = nullptr;
  for (const auto &sym : symbols) {
    if (sym.name == name && sym.scope_start_line <= line) {
      if (!best || sym.location.line > best->location.line) {
        best = &sym;
      }
    }
  }
  return best;
}

AnalysisResult analyze(const std::string &source, const std::string &file_path) {
  AnalysisResult result;

  Scanner scanner(source);
  auto tokens = scanner.scan_tokens();

  bool has_lexer_error = false;
  for (const auto &token : tokens) {
    if (token.type == TokenType::ERROR) {
      result.diagnostics.push_back({token.line, token.column, static_cast<int>(token.lexeme.size()), std::string(token.lexeme), 1});
      has_lexer_error = true;
    }
  }

  if (has_lexer_error) return result;

  Parser parser(tokens);
  auto parse_result = parser.parse();

  for (const auto &err : parse_result.errors) {
    result.diagnostics.push_back({err.line, err.column, 1, err.message, 1});
  }

  // Collect using and import declarations even with parse errors (for completion)
  if (parse_result.program) {
    for (const auto &decl : parse_result.program->declarations) {
      if (const auto *u = dynamic_cast<const ast::UsingDecl *>(decl.get())) {
        result.used_namespaces.insert(u->namespace_name);
        if (u->is_namespace) {
          result.opened_namespaces.insert(u->namespace_name);
        }
      }
      if (const auto *imp = dynamic_cast<const ast::ImportDecl *>(decl.get())) {
        std::string ns = imp->alias.empty()
            ? std::filesystem::path(imp->path).stem().string()
            : imp->alias;
        result.imported_namespaces.insert(ns);
      }
    }
  }

  if (!parse_result.errors.empty() || !parse_result.program) {
    if (parse_result.program) {
      SymbolCollector collector;
      collector.collect(*parse_result.program);
      result.symbols = collector.take();
    }
    return result;
  }

  // Set up ModuleLoader for import resolution
  std::unique_ptr<ModuleLoader> module_loader;
  if (!file_path.empty()) {
    std::filesystem::path p(file_path);
    std::string base_dir = p.has_parent_path() ? p.parent_path().string() : ".";
    module_loader = std::make_unique<ModuleLoader>(base_dir);
  }

  TypeChecker checker;
  if (module_loader) checker.set_module_loader(module_loader.get());
  auto type_result = checker.check(*parse_result.program);
  for (const auto &err : type_result.errors) {
    result.diagnostics.push_back({err.location.line, err.location.column, err.location.length, err.message, static_cast<int>(err.severity)});
  }

  // Collect symbols from imported modules (cached by ModuleLoader)
  if (module_loader) {
    for (const auto &decl : parse_result.program->declarations) {
      const auto *imp = dynamic_cast<const ast::ImportDecl *>(decl.get());
      if (!imp) continue;
      auto load_result = module_loader->load(imp->path);
      if (!load_result.module) {
        result.diagnostics.push_back({
            imp->location.line, imp->location.column,
            1, load_result.error, 1  // severity=Error
        });
        continue;
      }
      const auto &mod = *load_result.module;
      std::string ns = imp->alias.empty() ? mod.namespace_name : imp->alias;

      auto &syms = result.imported_symbols[ns];

      for (const auto *fn : mod.public_functions) {
        if (!imp->selected_symbols.empty()) {
          bool found = false;
          for (const auto &s : imp->selected_symbols) {
            if (s == fn->name) { found = true; break; }
          }
          if (!found) continue;
        }
        Symbol sym;
        sym.name = fn->name;
        sym.kind = SymbolKind::Function;
        sym.return_type = fn->return_type.to_string();
        sym.params = fn->params;
        sym.location = imp->location;
        syms.push_back(std::move(sym));
      }
      for (const auto *sd : mod.public_structs) {
        if (!imp->selected_symbols.empty()) continue;
        Symbol sym;
        sym.name = sd->name;
        sym.kind = SymbolKind::Struct;
        sym.type_name = "struct";
        sym.location = imp->location;
        for (const auto &field : sd->fields) {
          sym.fields.push_back(FieldSymbol{field.name, field.type.to_string()});
        }
        syms.push_back(std::move(sym));
      }
      for (const auto *ed : mod.public_enums) {
        if (!imp->selected_symbols.empty()) continue;
        Symbol sym;
        sym.name = ed->name;
        sym.kind = SymbolKind::Enum;
        sym.type_name = "enum";
        sym.location = imp->location;
        for (const auto &v : ed->variants) {
          sym.variants.push_back(v.name);
          sym.variant_param_counts.push_back(static_cast<int>(v.param_types.size()));
        }
        syms.push_back(std::move(sym));
      }
    }
  }

  SymbolCollector collector;
  collector.collect(*parse_result.program);
  result.symbols = collector.take();

  // Register imported symbols with qualified names for hover / definition
  for (const auto &[ns, syms] : result.imported_symbols) {
    for (auto sym : syms) {
      sym.name = ns + "::" + sym.name;
      sym.scope_start_line = 0;
      sym.scope_end_line = 999999;
      result.symbols.symbols.push_back(std::move(sym));
    }
  }

  result.program = std::move(parse_result.program);
  result.valid = true;

  return result;
}

} // namespace kinglet::lsp
