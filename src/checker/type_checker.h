#pragma once

#include "ast/ast.h"
#include "types/types.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

struct TypeError {
  ast::SourceLocation location;
  std::string message;
};

struct TypeCheckResult {
  std::vector<TypeError> errors;
};

class TypeChecker {
public:
  TypeCheckResult check(const ast::Program &program);

private:
  void check_function(const ast::FunctionDecl &function);
  void check_stmt(const ast::Stmt &stmt, const Type &expected_return);
  Type check_expr(const ast::Expr &expr);

  void push_scope();
  void pop_scope();
  void declare_var(const std::string &name, const Type &type, bool is_mutable);
  std::optional<Type> lookup_var(const std::string &name) const;
  std::optional<Type> lookup_type(const std::string &name) const;
  Type resolve_type_name(const std::string &name) const;
  void error_at(ast::SourceLocation location, std::string message);

  struct VarInfo {
    Type type;
    bool is_mutable;
  };

  std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
  std::unordered_map<std::string, Type> type_registry_;
  std::vector<TypeError> errors_;
  std::unordered_set<std::string> used_;    // using io;
  std::unordered_set<std::string> opened_;  // using namespace io;
  int loop_depth_ = 0;
};

} // namespace kinglet
