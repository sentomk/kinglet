#include "checker/type_checker.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace kinglet {

namespace {

std::string type_to_string(const Type &type) {
  if (type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) {
    return type.name;
  }
  std::ostringstream oss;
  oss << type.kind;
  return oss.str();
}

} // namespace

Type TypeChecker::resolve_type_name(const std::string &name) const {
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
  auto it = type_registry_.find(name);
  if (it != type_registry_.end()) {
    return it->second;
  }
  Type err(TypeKind::Void);
  err.name = "<unknown:" + name + ">";
  return err;
}

Type TypeChecker::resolve_type_expr(const ast::TypeExpr &expr, ast::SourceLocation loc) {
  if (expr.type_args.empty()) {
    Type t = resolve_type_name(expr.name);
    if (t.name.find("<unknown:") == 0 && loc.line > 0) {
      error_at(loc, "Unknown type '" + expr.name + "'.");
    }
    return t;
  }
  std::string mangled = mangle_name(expr.name, expr.type_args);
  auto it = type_registry_.find(mangled);
  if (it != type_registry_.end()) {
    return it->second;
  }
  auto gen_it = generic_structs_.find(expr.name);
  if (gen_it != generic_structs_.end()) {
    instantiate_generic_struct(gen_it->second, expr.type_args);
    auto inst_it = type_registry_.find(mangled);
    if (inst_it != type_registry_.end()) {
      return inst_it->second;
    }
  }
  if (loc.line > 0) {
    error_at(loc, "Unknown type '" + expr.to_string() + "'.");
  }
  Type err(TypeKind::Void);
  err.name = "<unknown:" + expr.to_string() + ">";
  return err;
}

std::string TypeChecker::mangle_name(const std::string &base, const std::vector<ast::TypeExpr> &args) const {
  std::string result = base;
  for (const auto &arg : args) {
    result += "__" + arg.to_string();
  }
  return result;
}

void TypeChecker::instantiate_generic_struct(const ast::StructDecl *decl, const std::vector<ast::TypeExpr> &args) {
  std::string mangled = mangle_name(decl->name, args);
  if (instantiated_.count(mangled)) return;
  instantiated_.insert(mangled);

  if (args.size() != decl->type_params.size()) {
    errors_.push_back(TypeError{.location = decl->location,
                                .message = "Wrong number of type arguments for '" + decl->name + "'."});
    return;
  }

  std::unordered_map<std::string, ast::TypeExpr> subst;
  for (size_t i = 0; i < decl->type_params.size(); ++i) {
    subst[decl->type_params[i]] = args[i];
  }

  Type struct_type(TypeKind::Struct);
  struct_type.name = mangled;
  for (const auto &field : decl->fields) {
    ast::TypeExpr resolved_field_type = field.type;
    auto sub_it = subst.find(field.type.name);
    if (sub_it != subst.end() && field.type.type_args.empty()) {
      resolved_field_type = sub_it->second;
    }
    Type ft = resolve_type_expr(resolved_field_type);
    struct_type.fields.push_back(FieldInfo{field.name, ft.kind, ft.name});
  }
  type_registry_.insert_or_assign(mangled, struct_type);
}

TypeCheckResult TypeChecker::check(const ast::Program &program) {
  errors_.clear();
  scopes_.clear();
  used_.clear();
  opened_.clear();
  type_registry_.clear();

  push_scope();

  // First pass: register types, using declarations, and function signatures
  for (const ast::DeclPtr &decl : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(decl.get())) {
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
      }
      continue;
    }
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(decl.get())) {
      if (!struct_decl->type_params.empty()) {
        generic_structs_[struct_decl->name] = struct_decl;
      } else {
        Type struct_type(TypeKind::Struct);
        struct_type.name = struct_decl->name;
        for (const auto &field : struct_decl->fields) {
          Type ft = resolve_type_expr(field.type);
          struct_type.fields.push_back(FieldInfo{field.name, ft.kind, ft.name});
        }
        type_registry_.insert_or_assign(struct_decl->name, struct_type);
      }
      continue;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      Type enum_type(TypeKind::Enum);
      enum_type.name = enum_decl->name;
      enum_type.variants = enum_decl->variants;
      type_registry_.insert_or_assign(enum_decl->name, enum_type);
      continue;
    }
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      if (!func->type_params.empty()) {
        generic_functions_[func->name] = func;
        continue;
      }
      Type return_type = resolve_type_expr(func->return_type);
      std::vector<Type> param_types;
      for (const auto &param : func->params) {
        param_types.push_back(resolve_type_expr(param.type));
      }
      Type func_type(TypeKind::Function);
      func_type.param_types = std::move(param_types);
      func_type.return_type = std::make_unique<Type>(return_type);
      declare_var(func->name, func_type, false);
    }
  }

  // Second pass: check function bodies
  for (const ast::DeclPtr &decl : program.declarations) {
    if (dynamic_cast<const ast::UsingDecl *>(decl.get())) {
      continue;
    }
    if (dynamic_cast<const ast::StructDecl *>(decl.get())) {
      continue;
    }
    if (dynamic_cast<const ast::EnumDecl *>(decl.get())) {
      continue;
    }
    if (const auto *func = dynamic_cast<const ast::FunctionDecl *>(decl.get())) {
      if (func->type_params.empty()) {
        check_function(*func);
      }
    }
  }

  pop_scope();

  return TypeCheckResult{.errors = std::move(errors_)};
}

void TypeChecker::check_function(const ast::FunctionDecl &function) {
  Type return_type = resolve_type_expr(function.return_type, function.location);

  push_scope();

  for (const auto &param : function.params) {
    Type param_type = resolve_type_expr(param.type, function.location);
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
    bool returned = false;
    for (const ast::StmtPtr &statement : block->statements) {
      if (returned) {
        warn_at(statement->location, "Unreachable code.");
        break;
      }
      check_stmt(*statement, expected_return);
      if (dynamic_cast<const ast::ReturnStmt *>(statement.get()) ||
          dynamic_cast<const ast::BreakStmt *>(statement.get()) ||
          dynamic_cast<const ast::ContinueStmt *>(statement.get())) {
        returned = true;
      }
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
    Type var_type = resolve_type_expr(var_decl->type, var_decl->location);
    if (var_decl->init) {
      Type init_type = check_expr(*var_decl->init);
      if (var_decl->type.name == "auto") {
        var_type = init_type;
      } else if (!init_type.is_compatible_with(var_type)) {
        error_at(var_decl->location,
                 "Cannot assign " + type_to_string(init_type) + " to variable of type " +
                     type_to_string(var_type) + ".");
      }
    }
    bool is_mutable = var_decl->storage != "const";
    declare_var(var_decl->name, var_type, is_mutable, var_decl->location);
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
    auto enum_type = lookup_type(ns_access->namespace_name);
    if (enum_type.has_value() && enum_type->kind == TypeKind::Enum) {
      bool found = false;
      for (const auto &v : enum_type->variants) {
        if (v == ns_access->member_name) { found = true; break; }
      }
      if (!found) {
        error_at(ns_access->location, "Enum '" + ns_access->namespace_name +
                                          "' has no variant '" + ns_access->member_name + "'.");
      }
      return *enum_type;
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
    case ast::UnaryOp::BitNot:
      if (right_type.kind != TypeKind::Int) {
        error_at(unary->location, "Bitwise NOT requires an integer operand.");
      }
      return int_type();
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
    // Handle bare io:: members when 'using namespace io;' is in effect
    if (callee_id && opened_.count("io") != 0) {
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
    if (ns_callee && used_.count(ns_callee->namespace_name) != 0 &&
        ns_callee->namespace_name == "io") {
      if (ns_callee->member_name == "out" || ns_callee->member_name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return void_type();
      }
      if (ns_callee->member_name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          check_expr(*arg);
        }
        return string_type();
      }
    }

    if (!call_expr->type_args.empty()) {
      const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
      if (callee_id) {
        auto gen_it = generic_functions_.find(callee_id->name);
        if (gen_it != generic_functions_.end()) {
          const ast::FunctionDecl *decl = gen_it->second;
          if (call_expr->type_args.size() != decl->type_params.size()) {
            error_at(call_expr->location, "Wrong number of type arguments for '" + callee_id->name + "'.");
            return int_type();
          }
          std::unordered_map<std::string, ast::TypeExpr> subst;
          for (size_t i = 0; i < decl->type_params.size(); ++i) {
            subst[decl->type_params[i]] = call_expr->type_args[i];
          }
          auto substitute = [&](const ast::TypeExpr &te) -> ast::TypeExpr {
            if (te.type_args.empty()) {
              auto s_it = subst.find(te.name);
              if (s_it != subst.end()) return s_it->second;
            }
            return te;
          };
          Type ret = resolve_type_expr(substitute(decl->return_type));
          std::vector<Type> param_types;
          for (const auto &param : decl->params) {
            param_types.push_back(resolve_type_expr(substitute(param.type)));
          }
          if (call_expr->args.size() != param_types.size()) {
            error_at(call_expr->location,
                     "Expected " + std::to_string(param_types.size()) + " arguments, got " +
                         std::to_string(call_expr->args.size()) + ".");
            return ret;
          }
          for (size_t i = 0; i < call_expr->args.size(); ++i) {
            Type arg_type = check_expr(*call_expr->args[i]);
            if (!arg_type.is_compatible_with(param_types[i])) {
              error_at(call_expr->args[i]->location,
                       "Expected " + type_to_string(param_types[i]) + ", got " +
                           type_to_string(arg_type) + ".");
            }
          }
          return ret;
        }
      }
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

  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    Type resolved = resolve_type_expr(struct_lit->struct_type, struct_lit->location);
    std::string type_name = struct_lit->struct_type.to_string();
    auto type_opt = lookup_type(resolved.name);
    if (!type_opt.has_value()) {
      error_at(struct_lit->location, "Unknown struct type '" + type_name + "'.");
      return int_type();
    }
    if (type_opt->kind != TypeKind::Struct) {
      error_at(struct_lit->location, "'" + type_name + "' is not a struct type.");
      return int_type();
    }
    const auto &fields_def = type_opt->fields;
    if (struct_lit->fields.size() != fields_def.size()) {
      error_at(struct_lit->location,
               "Struct '" + type_name + "' expects " + std::to_string(fields_def.size()) +
                   " field(s), got " + std::to_string(struct_lit->fields.size()) + ".");
    }
    for (size_t i = 0; i < struct_lit->fields.size() && i < fields_def.size(); ++i) {
      Type val_type = check_expr(*struct_lit->fields[i].value);
      Type expected(fields_def[i].type_kind);
      if (fields_def[i].type_kind == TypeKind::Struct && !fields_def[i].type_name.empty()) {
        auto reg_it = type_registry_.find(fields_def[i].type_name);
        if (reg_it != type_registry_.end()) expected = reg_it->second;
      }
      if (!val_type.is_compatible_with(expected)) {
        error_at(struct_lit->fields[i].value->location,
                 "Field '" + fields_def[i].name + "' expects " + type_to_string(expected) +
                     ", got " + type_to_string(val_type) + ".");
      }
    }
    for (size_t i = fields_def.size(); i < struct_lit->fields.size(); ++i) {
      check_expr(*struct_lit->fields[i].value);
    }
    return *type_opt;
  }

  if (const auto *field_access = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    Type obj_type = check_expr(*field_access->object);
    if (obj_type.kind != TypeKind::Struct) {
      error_at(field_access->location, "Cannot access field on non-struct type.");
      return int_type();
    }
    for (const auto &f : obj_type.fields) {
      if (f.name == field_access->field_name) {
        if (f.type_kind == TypeKind::Struct && !f.type_name.empty()) {
          auto reg_it = type_registry_.find(f.type_name);
          if (reg_it != type_registry_.end()) return reg_it->second;
        }
        return Type(f.type_kind);
      }
    }
    error_at(field_access->location, "Struct '" + obj_type.name + "' has no field '" +
                                         field_access->field_name + "'.");
    return int_type();
  }

  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    Type obj_type = check_expr(*field_assign->object);
    Type value_type = check_expr(*field_assign->value);
    if (obj_type.kind != TypeKind::Struct) {
      error_at(field_assign->location, "Cannot assign field on non-struct type.");
      return int_type();
    }
    for (const auto &f : obj_type.fields) {
      if (f.name == field_assign->field_name) {
        Type field_type(f.type_kind);
        if (f.type_kind == TypeKind::Struct && !f.type_name.empty()) {
          auto reg_it = type_registry_.find(f.type_name);
          if (reg_it != type_registry_.end()) field_type = reg_it->second;
        }
        if (!value_type.is_compatible_with(field_type)) {
          error_at(field_assign->location,
                   "Cannot assign " + type_to_string(value_type) + " to field '" +
                       f.name + "' of type " + type_to_string(field_type) + ".");
        }
        return field_type;
      }
    }
    error_at(field_assign->location, "Struct '" + obj_type.name + "' has no field '" +
                                         field_assign->field_name + "'.");
    return int_type();
  }

  return int_type();
}

void TypeChecker::push_scope() {
  scopes_.emplace_back();
}

void TypeChecker::pop_scope() {
  if (!scopes_.empty()) {
    for (const auto &[name, info] : scopes_.back()) {
      if (!info.used && name != "_" && info.location.line > 0) {
        warn_at(info.location, "Unused variable '" + name + "'.");
      }
    }
    scopes_.pop_back();
  }
}

void TypeChecker::declare_var(const std::string &name, const Type &type, bool is_mutable, ast::SourceLocation loc) {
  if (scopes_.empty()) {
    return;
  }
  auto &scope = scopes_.back();
  if (scope.find(name) != scope.end()) {
    error_at(loc, "Variable '" + name + "' already declared.");
    return;
  }
  scope.insert_or_assign(name, VarInfo{.type = type, .is_mutable = is_mutable, .used = false, .location = loc});
}

std::optional<Type> TypeChecker::lookup_var(const std::string &name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      found->second.used = true;
      return found->second.type;
    }
  }
  return std::nullopt;
}

std::optional<Type> TypeChecker::lookup_type(const std::string &name) const {
  auto it = type_registry_.find(name);
  if (it != type_registry_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void TypeChecker::error_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(TypeError{.location = location, .message = std::move(message), .severity = DiagnosticSeverity::Error});
}

void TypeChecker::warn_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(TypeError{.location = location, .message = std::move(message), .severity = DiagnosticSeverity::Warning});
}

} // namespace kinglet
