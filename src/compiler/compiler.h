#pragma once

#include "ast/ast.h"
#include "vm/chunk.h"

#include <string>
#include <vector>

namespace kinglet {

struct CompileError {
  ast::SourceLocation location;
  std::string message;
};

struct CompileResult {
  Chunk chunk;
  std::vector<CompileError> errors;
};

class Compiler {
public:
  CompileResult compile(const ast::Program &program);

private:
  struct Local {
    std::string name;
    bool is_mutable = true;
  };

  void compile_function(const ast::FunctionDecl &function);
  void compile_stmt(const ast::Stmt &stmt);
  void compile_expr(const ast::Expr &expr);
  void compile_assignment(const ast::AssignExpr &assign);

  void emit(OpCode op, ast::SourceLocation location);
  void emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location);
  void emit_constant(Value value, ast::SourceLocation location);
  std::size_t emit_jump(OpCode op, ast::SourceLocation location);
  void patch_jump(std::size_t offset);
  int resolve_local(const std::string &name) const;
  bool declare_local(const ast::VarDeclStmt &var_decl, uint32_t *slot);
  void error_at(ast::SourceLocation location, std::string message);

  Chunk chunk_;
  std::vector<Local> locals_;
  std::vector<CompileError> errors_;
};

} // namespace kinglet
