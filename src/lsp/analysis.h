#pragma once

#include "ast/ast.h"
#include "parser/parser.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace kinglet::lsp {

enum class SymbolKind { Variable, Function, Parameter, Namespace, Struct, Enum };

struct FieldSymbol {
  std::string name;
  std::string type_name;
};

struct Symbol {
  std::string name;
  SymbolKind kind;
  std::string type_name;
  ast::SourceLocation location;
  int scope_start_line = 0;
  int scope_end_line = 999999;
  std::vector<ast::Parameter> params;
  std::string return_type;
  std::vector<FieldSymbol> fields;
  std::vector<std::string> variants;
  std::vector<int> variant_param_counts;
};

struct SymbolTable {
  std::vector<Symbol> symbols;

  std::vector<const Symbol *> visible_at(int line) const;
  const Symbol *find_definition(const std::string &name, int line) const;
};

struct Diagnostic {
  int line;
  int col;
  int length = 1;
  std::string message;
  int severity; // 1=Error, 2=Warning, 3=Info, 4=Hint
};

struct AnalysisResult {
  std::unique_ptr<ast::Program> program;
  std::vector<ParseError> errors;
  std::vector<std::string> type_errors;
  std::vector<Diagnostic> diagnostics;
  SymbolTable symbols;
  std::set<std::string> used_namespaces;
  std::set<std::string> opened_namespaces;
  std::set<std::string> imported_namespaces;
  std::unordered_map<std::string, std::vector<Symbol>> imported_symbols;
  bool valid = false;
};

AnalysisResult analyze(const std::string &source, const std::string &file_path = "");

} // namespace kinglet::lsp
