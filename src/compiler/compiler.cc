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

  const ast::FunctionDecl *main_function = nullptr;
  for (const ast::DeclPtr &declaration : program.declarations) {
    if (const auto *function = dynamic_cast<const ast::FunctionDecl *>(declaration.get())) {
      if (function->name == "main") {
        main_function = function;
        break;
      }
    }
  }

  if (main_function == nullptr) {
    error_at(program.location, "Expected a main function.");
  } else {
    compile_function(*main_function);
  }

  if (errors_.empty()) {
    emit(OpCode::Null, program.location);
    emit(OpCode::Return, program.location);
  }

  return CompileResult{.chunk = std::move(chunk_), .errors = std::move(errors_)};
}

void Compiler::compile_function(const ast::FunctionDecl &function) {
  if (!function.params.empty()) {
    error_at(function.location, "main parameters are not supported yet.");
    return;
  }
  compile_stmt(*function.body);
}

void Compiler::compile_stmt(const ast::Stmt &stmt) {
  if (const auto *block = dynamic_cast<const ast::BlockStmt *>(&stmt)) {
    for (const ast::StmtPtr &statement : block->statements) {
      compile_stmt(*statement);
      if (!errors_.empty()) {
        return;
      }
    }
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
    const std::size_t loop_start = chunk_.instructions().size();
    compile_expr(*while_stmt->condition);
    const std::size_t exit_jump = emit_jump(OpCode::JmpFalse, while_stmt->location);
    compile_stmt(*while_stmt->body);
    const std::size_t loop_jump = emit_jump(OpCode::Jmp, while_stmt->location);
    // Calculate offset back to loop_start
    Instruction &jmp = const_cast<Instruction &>(chunk_.instructions()[loop_jump]);
    jmp.operand = static_cast<int32_t>(loop_start - loop_jump - 1);
    patch_jump(exit_jump);
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
    error_at(string_lit->location, "String literals are not supported by the VM compiler yet.");
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
    if (callee_id && callee_id->name == "print") {
      for (const ast::ExprPtr &arg : call_expr->args) {
        compile_expr(*arg);
      }
      emit_operand(OpCode::NativePrint, static_cast<uint32_t>(call_expr->args.size()),
                   call_expr->location);
      return;
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
  const std::size_t target = chunk_.instructions().size();
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
