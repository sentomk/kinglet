#include "compiler/compiler.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace kinglet {

namespace {

} // namespace

CompileResult Compiler::compile(const ast::Program &program) {
  chunk_ = Chunk();
  locals_.clear();
  errors_.clear();
  warnings_.clear();
  used_.clear();
  opened_.clear();
  function_indices_.clear();
  struct_indices_.clear();
  enum_indices_.clear();

  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *using_decl = dynamic_cast<const ast::UsingDecl *>(declaration.get())) {
      used_.insert(using_decl->namespace_name);
      if (using_decl->is_namespace) {
        opened_.insert(using_decl->namespace_name);
      }
    }
  }

  // Pass 1: register all structs, enums, and functions
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *struct_decl = dynamic_cast<const ast::StructDecl *>(declaration.get())) {
      StructMeta meta;
      meta.name = struct_decl->name;
      for (const auto &field : struct_decl->fields) {
        meta.field_names.push_back(field.name);
      }
      int idx = chunk_.add_struct_meta(std::move(meta));
      struct_indices_[struct_decl->name] = idx;
    }
    if (const auto *enum_decl = dynamic_cast<const ast::EnumDecl *>(declaration.get())) {
      EnumMeta meta;
      meta.name = enum_decl->name;
      meta.variants = enum_decl->variants;
      int idx = chunk_.add_enum_meta(std::move(meta));
      enum_indices_[enum_decl->name] = idx;
    }
  }

  std::vector<const ast::FunctionDecl *> functions;
  int main_index = -1;
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *function = dynamic_cast<const ast::FunctionDecl *>(declaration.get())) {
      int idx = chunk_.add_function(FunctionInfo{
          .name = function->name,
          .entry = 0,
          .param_count = static_cast<int>(function->params.size()),
      });
      uint32_t const_idx = chunk_.add_constant(Value::function_value(idx));
      function_indices_[function->name] = static_cast<int>(const_idx);
      functions.push_back(function);
      if (function->name == "main") {
        main_index = idx;
      }
    }
  }

  if (main_index < 0) {
    error_at(program.location, "Expected a main function.");
    return CompileResult{.chunk = std::move(chunk_), .errors = std::move(errors_), .warnings = std::move(warnings_)};
  }

  // Emit preamble: call main, then return its result
  ast::SourceLocation preamble_loc{1, 0};
  emit_constant(Value::function_value(main_index), preamble_loc);
  emit_operand(OpCode::Call, 0, preamble_loc);
  emit(OpCode::Return, preamble_loc);

  // Pass 2: compile each function body
  for (const auto *function : functions) {
    compile_function(*function);
    if (!errors_.empty()) break;
  }

  return CompileResult{.chunk = std::move(chunk_), .errors = std::move(errors_), .warnings = std::move(warnings_)};
}

void Compiler::push_scope() {
  scope_stack_.push_back(locals_.size());
}

void Compiler::pop_scope() {
  if (scope_stack_.empty()) return;
  const std::size_t target = scope_stack_.back();
  scope_stack_.pop_back();
  while (locals_.size() > target) {
    locals_.pop_back();
  }
}

void Compiler::compile_function(const ast::FunctionDecl &function) {
  locals_.clear();
  scope_stack_.clear();

  // Look up this function's index and patch its entry point
  auto it = function_indices_.find(function.name);
  int const_idx = it->second;
  int func_idx = chunk_.constants()[static_cast<std::size_t>(const_idx)].function_index_storage;
  const_cast<FunctionInfo &>(chunk_.functions()[static_cast<std::size_t>(func_idx)]).entry =
      chunk_.instructions().size();

  // Parameters become locals at slots 0..N-1
  for (const auto &param : function.params) {
    locals_.push_back(Local{.name = param.name, .is_mutable = true});
  }

  compile_stmt(*function.body);

  // Fallthrough safety: implicit null return
  if (errors_.empty()) {
    emit(OpCode::Null, function.location);
    emit(OpCode::Return, function.location);
  }
}

void Compiler::compile_stmt(const ast::Stmt &stmt) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    push_scope();
    bool returned = false;
    for (const ast::StmtPtr &statement : block->statements) {
      if (returned) {
        warnings_.push_back(CompileWarning{
            .location = statement->location,
            .message = "Unreachable code after return statement.",
        });
        break;
      }
      compile_stmt(*statement);
      if (!errors_.empty()) {
        return;
      }
      if (dynamic_cast<const ast::ReturnStmt *>(statement.get())) {
        returned = true;
      }
    }
    pop_scope();
    return;
  }

  if (const auto *return_stmt = dynamic_cast<const ast::ReturnStmt *>(&stmt)) {
    if (return_stmt->value) {
      compile_expr(*return_stmt->value);
    } else {
      emit(OpCode::Null, return_stmt->location);
    }
    emit(OpCode::Return, return_stmt->location);
    return;
  }

  if (const auto *var_decl = dynamic_cast<const ast::VarDeclStmt *>(&stmt)) {
    uint32_t slot = 0;
    if (!declare_local(*var_decl, &slot)) {
      return;
    }
    if (var_decl->init) {
      compile_expr(*var_decl->init);
    } else {
      emit(OpCode::Null, var_decl->location);
    }
    emit_operand(OpCode::StoreLocal, slot, var_decl->location);
    emit(OpCode::Pop, var_decl->location);
    return;
  }

  if (const auto *expr_stmt = dynamic_cast<const ast::ExprStmt *>(&stmt)) {
    compile_expr(*expr_stmt->expr);
    emit(OpCode::Pop, expr_stmt->location);
    return;
  }

  if (const auto *if_stmt = dynamic_cast<const ast::IfStmt *>(&stmt)) {
    compile_expr(*if_stmt->condition);
    const std::size_t then_jump = emit_jump(OpCode::JmpFalse, if_stmt->location);
    compile_stmt(*if_stmt->then_branch);
    if (if_stmt->else_branch) {
      const std::size_t else_jump = emit_jump(OpCode::Jmp, if_stmt->location);
      patch_jump(then_jump);
      compile_stmt(*if_stmt->else_branch);
      patch_jump(else_jump);
    } else {
      patch_jump(then_jump);
    }
    return;
  }

  if (const auto *while_stmt = dynamic_cast<const ast::WhileStmt *>(&stmt)) {
    loop_stack_.emplace_back();

    const std::size_t loop_start = chunk_.instructions().size();
    compile_expr(*while_stmt->condition);
    loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::JmpFalse, while_stmt->location));
    compile_stmt(*while_stmt->body);
    const std::size_t loop_jump = emit_jump(OpCode::Jmp, while_stmt->location);
    patch_jump_to(loop_jump, loop_start);

    for (std::size_t jump : loop_stack_.back().break_jumps) {
      patch_jump(jump);
    }
    for (std::size_t jump : loop_stack_.back().continue_jumps) {
      patch_jump_to(jump, loop_start);
    }
    loop_stack_.pop_back();
    return;
  }

  if (const auto *for_stmt = dynamic_cast<const ast::ForStmt *>(&stmt)) {
    push_scope();
    loop_stack_.emplace_back();

    if (for_stmt->init) {
      compile_stmt(*for_stmt->init);
    }

    const std::size_t loop_start = chunk_.instructions().size();
    if (for_stmt->condition) {
      compile_expr(*for_stmt->condition);
      loop_stack_.back().break_jumps.push_back(emit_jump(OpCode::JmpFalse, for_stmt->location));
    }
    compile_stmt(*for_stmt->body);

    const std::size_t step_start = chunk_.instructions().size();
    for (std::size_t jump : loop_stack_.back().continue_jumps) {
      patch_jump_to(jump, step_start);
    }

    if (for_stmt->step) {
      compile_stmt(*for_stmt->step);
    }

    const std::size_t loop_jump = emit_jump(OpCode::Jmp, for_stmt->location);
    patch_jump_to(loop_jump, loop_start);

    for (std::size_t jump : loop_stack_.back().break_jumps) {
      patch_jump(jump);
    }
    loop_stack_.pop_back();
    pop_scope();
    return;
  }

  if (dynamic_cast<const ast::BreakStmt *>(&stmt)) {
    if (loop_stack_.empty()) {
      error_at(stmt.location, "break must be inside a loop.");
      return;
    }
    const std::size_t jump = emit_jump(OpCode::Jmp, stmt.location);
    loop_stack_.back().break_jumps.push_back(jump);
    return;
  }

  if (dynamic_cast<const ast::ContinueStmt *>(&stmt)) {
    if (loop_stack_.empty()) {
      error_at(stmt.location, "continue must be inside a loop.");
      return;
    }
    const std::size_t jump = emit_jump(OpCode::Jmp, stmt.location);
    loop_stack_.back().continue_jumps.push_back(jump);
    return;
  }

  error_at(stmt.location, "Unsupported statement in VM compiler.");
}

void Compiler::compile_expr(const ast::Expr &expr) {
  if (const auto *int_lit = dynamic_cast<const ast::IntLiteralExpr *>(&expr)) {
    emit_constant(Value::int_value(int_lit->value), int_lit->location);
    return;
  }

  if (const auto *float_lit = dynamic_cast<const ast::FloatLiteralExpr *>(&expr)) {
    emit_constant(Value::double_value(float_lit->value), float_lit->location);
    return;
  }

  if (const auto *string_lit = dynamic_cast<const ast::StringLiteralExpr *>(&expr)) {
    // Unescape: \n -> newline, \t -> tab, etc.
    std::string unescaped;
    unescaped.reserve(string_lit->value.size());
    for (std::size_t i = 0; i < string_lit->value.size(); ++i) {
      if (string_lit->value[i] == '\\' && i + 1 < string_lit->value.size()) {
        switch (string_lit->value[i + 1]) {
        case 'n':
          unescaped += '\n';
          ++i;
          break;
        case 't':
          unescaped += '\t';
          ++i;
          break;
        case 'r':
          unescaped += '\r';
          ++i;
          break;
        case '\\':
          unescaped += '\\';
          ++i;
          break;
        case '"':
          unescaped += '"';
          ++i;
          break;
        default:
          unescaped += string_lit->value[i];
          break;
        }
      } else {
        unescaped += string_lit->value[i];
      }
    }
    emit_constant(Value::string_value(unescaped), string_lit->location);
    return;
  }

  if (const auto *bool_lit = dynamic_cast<const ast::BoolLiteralExpr *>(&expr)) {
    emit(bool_lit->value ? OpCode::True : OpCode::False, bool_lit->location);
    return;
  }

  if (dynamic_cast<const ast::NullLiteralExpr *>(&expr)) {
    emit(OpCode::Null, expr.location);
    return;
  }

  if (const auto *unary = dynamic_cast<const ast::UnaryExpr *>(&expr)) {
    compile_expr(*unary->right);
    switch (unary->op) {
    case ast::UnaryOp::Neg:
      emit(OpCode::Negate, unary->location);
      break;
    case ast::UnaryOp::Not:
      emit(OpCode::Not, unary->location);
      break;
    default:
      error_at(unary->location, "Unsupported unary operator.");
      break;
    }
    return;
  }

  if (const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(&expr)) {
    const int slot = resolve_local(identifier->name);
    if (slot < 0) {
      error_at(identifier->location, "Use of undeclared variable '" + identifier->name + "'.");
      return;
    }
    emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(slot), identifier->location);
    return;
  }

  if (const auto *assign = dynamic_cast<const ast::AssignExpr *>(&expr)) {
    compile_assignment(*assign);
    return;
  }

  if (const auto *binary = dynamic_cast<const ast::BinaryExpr *>(&expr)) {
    compile_expr(*binary->left);
    compile_expr(*binary->right);
    switch (binary->op) {
    case ast::BinaryOp::Add:
      emit(OpCode::Add, binary->location);
      break;
    case ast::BinaryOp::Sub:
      emit(OpCode::Subtract, binary->location);
      break;
    case ast::BinaryOp::Mul:
      emit(OpCode::Multiply, binary->location);
      break;
    case ast::BinaryOp::Div:
      emit(OpCode::Divide, binary->location);
      break;
    case ast::BinaryOp::Eq:
      emit(OpCode::Eq, binary->location);
      break;
    case ast::BinaryOp::Neq:
      emit(OpCode::Neq, binary->location);
      break;
    case ast::BinaryOp::Lt:
      emit(OpCode::Lt, binary->location);
      break;
    case ast::BinaryOp::Gt:
      emit(OpCode::Gt, binary->location);
      break;
    case ast::BinaryOp::Le:
      emit(OpCode::Le, binary->location);
      break;
    case ast::BinaryOp::Ge:
      emit(OpCode::Ge, binary->location);
      break;
    default:
      error_at(binary->location, "Unsupported binary operator.");
      break;
    }
    return;
  }

  if (const auto *call_expr = dynamic_cast<const ast::CallExpr *>(&expr)) {
    const auto *callee_id = dynamic_cast<const ast::IdentifierExpr *>(call_expr->callee.get());
    // Handle bare io:: members when 'using namespace io;' is in effect
    if (callee_id && opened_.count("io") != 0) {
      if (callee_id->name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeOut, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (callee_id->name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeErr, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
      if (callee_id->name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeIn, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    const auto *ns_callee =
        dynamic_cast<const ast::NamespaceAccessExpr *>(call_expr->callee.get());
    if (ns_callee && used_.count(ns_callee->namespace_name) != 0 &&
        ns_callee->namespace_name == "io") {
      if (ns_callee->member_name == "out") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeOut, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }

      if (ns_callee->member_name == "err") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeErr, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }

      if (ns_callee->member_name == "in") {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_operand(OpCode::NativeIn, static_cast<uint32_t>(call_expr->args.size()),
                     call_expr->location);
        return;
      }
    }

    // User-defined function call
    if (callee_id) {
      auto func_it = function_indices_.find(callee_id->name);
      if (func_it != function_indices_.end()) {
        for (const ast::ExprPtr &arg : call_expr->args) {
          compile_expr(*arg);
        }
        emit_constant(Value::function_value(
            chunk_.constants()[static_cast<std::size_t>(func_it->second)].function_index_storage),
            call_expr->location);
        emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
        return;
      }
    }

    compile_expr(*call_expr->callee);
    for (const ast::ExprPtr &arg : call_expr->args) {
      compile_expr(*arg);
    }
    emit_operand(OpCode::Call, static_cast<uint32_t>(call_expr->args.size()), call_expr->location);
    return;
  }

  if (const auto *inspect_expr = dynamic_cast<const ast::InspectExpr *>(&expr)) {
    // Compile the inspect value and store it in a temporary local
    compile_expr(*inspect_expr->value);
    const uint32_t temp_slot = static_cast<uint32_t>(locals_.size());
    locals_.push_back(Local{.name = "<inspect_value>", .is_mutable = false});
    emit_operand(OpCode::StoreLocal, temp_slot, inspect_expr->location);

    // Compile each arm as an if-else chain
    std::vector<std::size_t> end_jumps;
    for (const ast::InspectArm &arm : inspect_expr->arms) {
      // Check if this is a wildcard pattern (identifier "_")
      const auto *identifier = dynamic_cast<const ast::IdentifierExpr *>(arm.pattern.get());
      if (identifier && identifier->name == "_") {
        // Wildcard pattern - always matches
        emit(OpCode::Pop, inspect_expr->location);
        compile_expr(*arm.body);
      } else {
        // Load the inspect value and compare with the pattern
        emit_operand(OpCode::LoadLocal, temp_slot, inspect_expr->location);
        compile_expr(*arm.pattern);
        emit(OpCode::Eq, inspect_expr->location);
        const std::size_t next_arm = emit_jump(OpCode::JmpFalse, inspect_expr->location);
        emit(OpCode::Pop, inspect_expr->location);
        compile_expr(*arm.body);
        end_jumps.push_back(emit_jump(OpCode::Jmp, inspect_expr->location));
        patch_jump(next_arm);
      }
    }

    // Patch all end jumps to point here
    for (std::size_t jump : end_jumps) {
      patch_jump(jump);
    }

    // Remove the temporary local
    locals_.pop_back();
    return;
  }

  if (const auto *ns_access = dynamic_cast<const ast::NamespaceAccessExpr *>(&expr)) {
    auto enum_it = enum_indices_.find(ns_access->namespace_name);
    if (enum_it != enum_indices_.end()) {
      int type_idx = enum_it->second;
      const auto &meta = chunk_.enum_metas()[static_cast<std::size_t>(type_idx)];
      int variant_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.variants.size()); ++i) {
        if (meta.variants[static_cast<std::size_t>(i)] == ns_access->member_name) {
          variant_idx = i;
          break;
        }
      }
      if (variant_idx < 0) {
        error_at(ns_access->location, "Unknown enum variant '" + ns_access->member_name + "'.");
        return;
      }
      uint32_t operand = (static_cast<uint32_t>(type_idx) << 16) |
                         static_cast<uint32_t>(variant_idx);
      emit_operand(OpCode::EnumVariant, operand, ns_access->location);
      return;
    }
    if (used_.count(ns_access->namespace_name) != 0) {
      return;
    }
    error_at(ns_access->location, "Unknown namespace '" + ns_access->namespace_name + "'.");
    return;
  }

  if (const auto *struct_lit = dynamic_cast<const ast::StructLiteralExpr *>(&expr)) {
    auto struct_it = struct_indices_.find(struct_lit->struct_name);
    if (struct_it == struct_indices_.end()) {
      error_at(struct_lit->location, "Unknown struct type '" + struct_lit->struct_name + "'.");
      return;
    }
    int type_idx = struct_it->second;
    const auto &meta = chunk_.struct_metas()[static_cast<std::size_t>(type_idx)];
    for (std::size_t i = 0; i < meta.field_names.size(); ++i) {
      if (i < struct_lit->fields.size()) {
        compile_expr(*struct_lit->fields[i].value);
      } else {
        emit(OpCode::Null, struct_lit->location);
      }
    }
    emit_operand(OpCode::StructNew, static_cast<uint32_t>((type_idx << 16) |
                 static_cast<int>(meta.field_names.size())), struct_lit->location);
    return;
  }

  if (const auto *field_access = dynamic_cast<const ast::FieldAccessExpr *>(&expr)) {
    compile_expr(*field_access->object);
    uint32_t field_const = chunk_.add_constant(Value::string_value(field_access->field_name));
    emit_operand(OpCode::FieldGet, field_const, field_access->location);
    return;
  }

  if (const auto *field_assign = dynamic_cast<const ast::FieldAssignExpr *>(&expr)) {
    compile_expr(*field_assign->object);
    compile_expr(*field_assign->value);
    uint32_t field_const = chunk_.add_constant(Value::string_value(field_assign->field_name));
    emit_operand(OpCode::FieldSet, field_const, field_assign->location);
    return;
  }

  error_at(expr.location, "Unsupported expression in VM compiler.");
}

void Compiler::compile_assignment(const ast::AssignExpr &assign) {
  const int slot = resolve_local(assign.name);
  if (slot < 0) {
    error_at(assign.location, "Assignment to undeclared variable '" + assign.name + "'.");
    return;
  }
  if (!locals_[static_cast<std::size_t>(slot)].is_mutable) {
    error_at(assign.location, "Cannot assign to const variable '" + assign.name + "'.");
    return;
  }

  if (assign.op == ast::AssignOp::Assign) {
    compile_expr(*assign.value);
  } else {
    emit_operand(OpCode::LoadLocal, static_cast<uint32_t>(slot), assign.location);
    compile_expr(*assign.value);
    switch (assign.op) {
    case ast::AssignOp::AddAssign:
      emit(OpCode::Add, assign.location);
      break;
    case ast::AssignOp::SubAssign:
      emit(OpCode::Subtract, assign.location);
      break;
    case ast::AssignOp::MulAssign:
      emit(OpCode::Multiply, assign.location);
      break;
    case ast::AssignOp::DivAssign:
      emit(OpCode::Divide, assign.location);
      break;
    default:
      error_at(assign.location, "Unsupported assignment operator.");
      return;
    }
  }

  emit_operand(OpCode::StoreLocal, static_cast<uint32_t>(slot), assign.location);
}

void Compiler::emit(OpCode op, ast::SourceLocation location) {
  chunk_.write(op, location.line, location.column);
}

void Compiler::emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location) {
  chunk_.write_operand(op, operand, location.line, location.column);
}

void Compiler::emit_constant(Value value, ast::SourceLocation location) {
  chunk_.write_constant(value, location.line, location.column);
}

std::size_t Compiler::emit_jump(OpCode op, ast::SourceLocation location) {
  emit_operand(op, 0, location);
  return chunk_.instructions().size() - 1;
}

void Compiler::patch_jump(std::size_t offset) {
  patch_jump_to(offset, chunk_.instructions().size());
}

void Compiler::patch_jump_to(std::size_t offset, std::size_t target) {
  const int32_t jump_offset = static_cast<int32_t>(target - offset - 1);
  Instruction &instruction = const_cast<Instruction &>(chunk_.instructions()[offset]);
  instruction.operand = jump_offset;
}

int Compiler::resolve_local(const std::string &name) const {
  for (std::size_t i = locals_.size(); i > 0; --i) {
    if (locals_[i - 1].name == name) {
      return static_cast<int>(i - 1);
    }
  }
  return -1;
}

bool Compiler::declare_local(const ast::VarDeclStmt &var_decl, uint32_t *slot) {
  if (resolve_local(var_decl.name) >= 0) {
    error_at(var_decl.location, "Duplicate variable declaration '" + var_decl.name + "'.");
    return false;
  }
  const bool is_mutable = var_decl.storage != "const";
  locals_.push_back(Local{
      .name = var_decl.name,
      .is_mutable = is_mutable,
  });
  *slot = static_cast<uint32_t>(locals_.size() - 1);
  return true;
}

void Compiler::error_at(ast::SourceLocation location, std::string message) {
  errors_.push_back(CompileError{
      .location = location,
      .message = std::move(message),
  });
}

} // namespace kinglet
