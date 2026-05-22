#include "checker/type_checker.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace kinglet {

namespace {

Type resolve_type_name(const std::string &name) {
  if (name == "int" || name == "auto") {
    return int_type();
  }
  if (name == "float" || name == "double") {
    return float_type();
  }
  if (name == "bool") {
    return bool_type();
  }
  if (name == "string") {
    return string_type();
  }
  if (name == "void") {
    return void_type();
  }
  return int_type();
}

std::string type_to_string(const Type &type) {
  std::ostringstream oss;
  oss << type.kind;
  return oss.str();
}

} // namespace

TypeCheckResult TypeChecker::check(const ast::Program &program) {
  errors_.clear();
  scopes_.clear();
  using_.clear();

  push_scope();

  for (const ast::DeclPtr &decl : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(decl.get())) {
      using_.insert(using_decl->namespace_name);
      continue;
    }
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      Type return_type = resolve_type_name(func->return_type);
      std::vector<Type> param_types;
      for (const auto &param : func->params) {
        param_types.push_back(resolve_type_name(param.type));
      }
      Type func_type(TypeKind::Function);
      func_type.param_types = std::move(param_types);
      func_type.return_type = std::make_unique<Type>(return_type);
      declare_var(func->name, func_type, false);
    }
  }

  for (const ast::DeclPtr &decl : program.declarations) {
    if (dynamic_cast<const ast::UsingDecl *>(decl.get())) {
      continue;
    }
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      check_function(*func);
    }
  }

  pop_scope();

  return TypeCheckResult{.errors = std::move(errors_)};
}

void TypeChecker::check_function(const ast::FunctionDecl &function) {
  Type return_type = resolve_type_name(function.return_type);

  push_scope();

  for (const auto &param : function.params) {
    Type param_type = resolve_type_name(param.type);
    declare_var(param.name, param_type, true);
  }

  if (function.body) {
    check_stmt(*function.body, return_type);
  }

  pop_scope();
}

void TypeChecker::check_stmt(const ast::Stmt &stmt, const Type &expected_return) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    push_scope();
    for (const ast::StmtPtr &statement : block->statements) {
      check_stmt(*statement, expected_return);
    }
    pop_scope();
    return;
  }

  if (const auto *return_stmt = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (return_stmt->value) {
      Type value_type = check_expr(*return_stmt->value);
      if (!value_type.is_compatible_with(expected_return)) {
        error_at(return_stmt->location,
                 "Cannot return " + type_to_string(value_type) + " from function returning " +
                     type_to_string(expected_return) + ".");
      }
    } else if (expected_return.kind != TypeKind::Void) {
      error_at(return_stmt->location, "Non-void function must return a value.");
    }
    return;
  }

  if (const auto *var_decl = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    Type var_type = resolve_type_name(var_decl->type);
    if (var_decl->init) {
      Type init_type = check_expr(*var_decl->init);
      if (var_decl->type == "auto") {
        var_type = init_type;
      } else if (!init_type.is_compatible_with(var_type)) {
        error_at(var_decl->location,
                 "Cannot assign " + type_to_string(init_type) + " to variable of type " +
                     type_to_string(var_type) + ".");
      }
    }
    bool is_mutable = var_decl->storage == "mut";
    declare_var(var_decl->name, var_type, is_mutable);
    return;
  }

  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    check_expr(*expr_stmt->expr);
    return;
  }

  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    Type cond_type = check_expr(*if_stmt->condition);
    if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
      error_at(if_stmt->location, "Condition must be Bool or Int.");
    }
    check_stmt(*if_stmt->then_branch, expected_return);
    if (if_stmt->else_branch) {
      check_stmt(*if_stmt->else_branch, expected_return);
    }
    return;
  }

  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    Type cond_type = check_expr(*while_stmt->condition);
    if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
      error_at(while_stmt->location, "Condition must be Bool or Int.");
    }
    ++loop_depth_;
    check_stmt(*while_stmt->body, expected_return);
    --loop_depth_;
    return;
  }

  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    push_scope();
    if (for_stmt->init) {
      check_stmt(*for_stmt->init, expected_return);
    }
    if (for_stmt->condition) {
      Type cond_type = check_expr(*for_stmt->condition);
      if (cond_type.kind != TypeKind::Bool && cond_type.kind != TypeKind::Int) {
        error_at(for_stmt->location, "Condition must be Bool or Int.");
      }
    }
    if (for_stmt->step) {
      check_stmt(*for_stmt->step, expected_return);
    }
    ++loop_depth_;
    check_stmt(*for_stmt->body, expected_return);
    --loop_depth_;
    pop_scope();
    return;
  }

  if (dynamic_cast<const ast::BreakStmt *>(&stmt)) {
    if (loop_depth_ <= 0) {
      error_at(stmt.location, "break must be inside a loop.");
    }
    return;
  }

  if (dynamic_cast<const ast::ContinueStmt *>(&stmt)) {
    if (loop_depth_ <= 0) {
      error_at(stmt.location, "continue must be inside a loop.");
    }
    return;
  }
}

Type TypeChecker::check_expr(const ast::Expr &expr) {
  if (dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    return int_type();
  }

  if (dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    return float_type();
  }

  if (dynamic_cast<const ast::StringLiteralExpr *>(&expr)) {
    return string_type();
  }

  if (dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) {
    return bool_type();
  }

  if (dynamic_cast<const ast::NullLiteralExpr *>(&expr)) {
    return null_type();
  }

  if (const auto *ns_access = dynamic_cast<const ast::NamespaceAccessExpr *>(&expr)) {
    if (ns_access->namespace_name == "io") {
      if (ns_access->member_name == "out" || ns_access->member_name == "err") {
        return void_type();
      }
      if (ns_access->member_name == "in") {
        return string_type();
      }
    }
    return void_type();
  }

  if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    if (identifier->name == "_") {
      return null_type();
    }
    auto var_type = lookup_var(identifier->name);
    if (!var_type.has_value()) {
      error_at(identifier->location, "Undeclared variable '" + identifier->name + "'.");
      return int_type();
    }
    return var_type.value();
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    Type right_type = check_expr(*unary->right);
    switch (unary->op) {
    case ast::UnaryOp::Neg:
      if (!right_type.is_numeric()) {
        error_at(unary->location, "Cannot negate non-numeric type.");
      }
      return right_type;
    case ast::UnaryOp::Not:
      return bool_type();
    default:
      error_at(unary->location, "Unsupported unary operator.");
      return int_type();
    }
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    Type left_type = check_expr(*binary->left);
    Type right_type = check_expr(*binary->right);

    switch (binary->op) {
    case ast::BinaryOp::Add:
    case ast::BinaryOp::Sub:
    case ast::BinaryOp::Mul:
    case ast::BinaryOp::Div:
    case ast::BinaryOp::Mod:
      if (!left_type.is_numeric() || !right_type.is_numeric()) {
        error_at(binary->location, "Arithmetic operands must be numeric.");
        return int_type();
      }
      return Type::promote(left_type, right_type);

    case ast::BinaryOp::Eq:
    case ast::BinaryOp::Neq:
    case ast::BinaryOp::Lt:
    case ast::BinaryOp::Gt:
    case ast::BinaryOp::Le:
    case ast::BinaryOp::Ge:
      if (!left_type.is_compatible_with(right_type)) {
        error_at(binary->location, "Cannot compare " + type_to_string(left_type) + " and " +
                                       type_to_string(right_type) + ".");
      }
      return bool_type();

    case ast::BinaryOp::And:
    case ast::BinaryOp::Or:
      return bool_type();
    }
  }

  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    auto var_type = lookup_var(assign->name);
    if (!var_type.has_value()) {
      error_at(assign->location, "Assignment to undeclared variable '" + assign->name + "'.");
      return int_type();
    }
    Type value_type = check_expr(*assign->value);
    if (!value_type.is_compatible_with(var_type.value())) {
      error_at(assign->location, "Cannot assign " + type_to_string(value_type) + " to " +
                                     type_to_string(var_type.value()) + ".");
    }
    return var_type.value();
  }

  if (const auto *call_expr = dynamic_cast<const ast::CallExpr *>(&expr)) {
    // Intercept built-in functions
    const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
    if (callee_id && callee_id->name == "print") {
      for (const ast::ExprPtr &arg : call_expr->args) {
        check_expr(*arg);
      }
      return void_type();
    }

    // Handle bare io:: members when 'using io;' is in effect
    if (callee_id && using_.count("io") != 0) {
      if (callee_id->name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return void_type();
      }
      if (callee_id->name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return void_type();
      }
      if (callee_id->name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return string_type();
      }
    }

    const auto *ns_callee =
        dynamic_cast<const ast::NamespaceAccessExpr *>(call_expr->callee.get());
    if (ns_callee && ns_callee->namespace_name == "io" && ns_callee->member_name == "out") {
      for (const ast::ExprPtr &arg : call_expr->args) {
        check_expr(*arg);
      }
      return void_type();
    }

    if (ns_callee && ns_callee->namespace_name == "io" && ns_callee->member_name == "err") {
      for (const ast::ExprPtr &arg : call_expr->args) {
        check_expr(*arg);
      }
      return void_type();
    }

    if (ns_callee && ns_callee->namespace_name == "io" && ns_callee->member_name == "in") {
      for (const ast::ExprPtr &arg : call_expr->args) {
        check_expr(*arg);
      }
      return string_type();
    }

    Type callee_type = check_expr(*call_expr->callee);
    if (callee_type.kind != TypeKind::Function) {
      error_at(call_expr->location, "Cannot call non-function type.");
      return int_type();
    }
    if (call_expr->args.size() != callee_type.param_types.size()) {
      error_at(call_expr->location,
               "Expected " + std::to_string(callee_type.param_types.size()) + " arguments, got " +
                   std::to_string(call_expr->args.size()) + ".");
      return callee_type.return_type ? *callee_type.return_type : int_type();
    }
    for (std::size_t i = 0; i < call_expr->args.size(); ++i) {
      Type arg_type = check_expr(*call_expr->args[i]);
      if (!arg_type.is_compatible_with(callee_type.param_types[i])) {
        error_at(call_expr->args[i]->location,
                 "Expected " + type_to_string(callee_type.param_types[i]) + ", got " +
                     type_to_string(arg_type) + ".");
      }
    }
    return callee_type.return_type ? *callee_type.return_type : int_type();
  }

  if (const auto *inspect_expr = dynamic_cast<const ast::InspectExpr *>(&expr)) {
    Type value_type = check_expr(*inspect_expr->value);
    Type result_type = null_type();
    for (const ast::InspectArm &arm : inspect_expr->arms) {
      Type pattern_type = check_expr(*arm.pattern);
      if (pattern_type.kind != TypeKind::Null && !pattern_type.is_compatible_with(value_type)) {
        error_at(arm.pattern->location, "Pattern type " + type_to_string(pattern_type) +
                                            " does not match value type " +
                                            type_to_string(value_type) + ".");
      }
      Type body_type = check_expr(*arm.body);
      if (result_type.kind == TypeKind::Null) {
        result_type = body_type;
      }
    }
    return result_type;
  }

  return int_type();
}

void TypeChecker::push_scope() {
  scopes_.emplace_back();
}

void TypeChecker::pop_scope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void TypeChecker::declare_var(const std::string &name, const Type &type, bool is_mutable) {
  if (scopes_.empty()) {
    return;
  }
  auto &scope = scopes_.back();
  if (scope.find(name) != scope.end()) {
    error_at(ast::SourceLocation{}, "Variable '" + name + "' already declared.");
    return;
  }
  scope.insert_or_assign(name, VarInfo{.type = type, .is_mutable = is_mutable});
}

std::optional<Type> TypeChecker::lookup_var(const std::string &name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return found->second.type;
    }
  }
  return std::nullopt;
}

void TypeChecker::error_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(TypeError{.location = location, .message = std::move(message)});
}

} // namespace kinglet
