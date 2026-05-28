#pragma once

#include <string>

namespace kinglet::lsp {

enum class CompletionPosition {
  TopLevelDecl,
  TypeExpr,
  ExpressionStart,
  Statement,
  StructFieldDecl,
  EnumVariant,
  TraitMethodDecl,
  ImplMethodDecl,
  FieldAccess,
  NamespaceAccess,
  ImportPath,
  ImportSymbol,
  MatchArm,
  StructLiteral,
  ParameterType,
  TraitName,
};

struct CompletionInfo {
  CompletionPosition position;
  std::string enclosing_type;
  std::string trait_name;
  std::string receiver_type;
  std::string ns_name;
  std::string import_path;
  std::string struct_name;
};

} // namespace kinglet::lsp
