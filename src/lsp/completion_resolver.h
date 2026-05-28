#pragma once

#include "lsp/analysis.h"
#include "lsp/completion_context.h"
#include "lsp/json.h"

#include <string>

namespace kinglet::lsp {

class CompletionResolver {
public:
  CompletionResolver(const AnalysisResult &analysis,
                     const std::string &prefix, int line, int character,
                     const std::string &uri, const std::string &source);

  json::Array resolve(const CompletionInfo &info);

private:
  json::Array resolve_top_level();
  json::Array resolve_statement();
  json::Array resolve_expression();
  json::Array resolve_type_expr();
  json::Array resolve_field_access(const std::string &receiver_type);
  json::Array resolve_namespace_access(const std::string &ns_name);
  json::Array resolve_import_path();
  json::Array resolve_import_symbol(const std::string &import_path);
  json::Array resolve_impl_method(const std::string &target_type,
                                  const std::string &trait_name);
  json::Array resolve_trait_name();
  json::Array resolve_struct_literal(const std::string &struct_name);
  json::Array resolve_enum_variant();

  void add_scope_symbols(json::Array &items);
  void add_io_members(json::Array &items);
  void add_type_keywords(json::Array &items);
  void add_control_keywords(json::Array &items);
  void add_snippets(json::Array &items);
  void add_namespace_completions(json::Array &items);

  bool matches_prefix(const std::string &name) const;

  const AnalysisResult &analysis_;
  std::string prefix_;
  int line_;
  int character_;
  std::string uri_;
  std::string source_;
};

} // namespace kinglet::lsp
