#pragma once

#include "ast/ast.h"
#include "types/types.h"

namespace kinglet {
class ModuleLoader;
}

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

enum class DiagnosticSeverity { Error = 1, Warning = 2, Info = 3, Hint = 4 };

struct TypeError {
  ast::SourceLocation location;
  std::string message;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
};

struct TypeCheckResult {
  std::vector<TypeError> errors;
};

class TypeChecker {
public:
  TypeCheckResult check(const ast::Program &program);
  void set_module_loader(ModuleLoader *loader) { module_loader_ = loader; }

private:
  void check_function(const ast::FunctionDecl &function);
  void check_stmt(const ast::Stmt &stmt, const Type &expected_return);
  Type check_expr(const ast::Expr &expr);

  void push_scope();
  void pop_scope();
  void declare_var(const std::string &name, const Type &type, bool is_mutable, ast::SourceLocation loc = {});
  std::optional<Type> lookup_var(const std::string &name);
  std::optional<Type> lookup_type(const std::string &name) const;
  Type resolve_type_name(const std::string &name) const;
  Type resolve_type_expr(const ast::TypeExpr &expr, ast::SourceLocation loc = {});
  std::string mangle_name(const std::string &base, const std::vector<ast::TypeExpr> &args) const;
  void instantiate_generic_struct(const ast::StructDecl *decl, const std::vector<ast::TypeExpr> &args);
  void error_at(ast::SourceLocation location, std::string message);
  void warn_at(ast::SourceLocation location, std::string message);
  void check_fmt_args(const std::vector<ast::ExprPtr> &args, ast::SourceLocation location);

  struct VarInfo {
    Type type;
    bool is_mutable;
    bool used = false;
    ast::SourceLocation location;
  };

  std::vector<std::unordered_map<std::string, VarInfo>> scopes_;
  std::unordered_map<std::string, Type> type_registry_;
  std::unordered_map<std::string, const ast::StructDecl *> generic_structs_;
  std::unordered_map<std::string, const ast::FunctionDecl *> generic_functions_;
  std::unordered_set<std::string> instantiated_;

  struct MethodInfo {
    const ast::FunctionDecl *decl;
    std::string target_type;
  };
  std::unordered_map<std::string, MethodInfo> method_registry_;

  std::vector<TypeError> errors_;
  std::unordered_set<std::string> used_;    // using io;
  std::unordered_set<std::string> opened_;  // using namespace io;
  std::unordered_set<std::string> imported_namespaces_;
  std::unordered_set<std::string> imported_bare_names_;  // for selective imports
  int loop_depth_ = 0;
  const ast::ExprStmt *implicit_return_stmt_ = nullptr;
  ModuleLoader *module_loader_ = nullptr;
};

} // namespace kinglet
