#pragma once

#include "ast/ast.h"
#include "vm/chunk.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kinglet {

struct CompileError {
  ast::SourceLocation location;
  std::string message;
};

struct CompileWarning {
  ast::SourceLocation location;
  std::string message;
};

struct CompileResult {
  Chunk chunk;
  std::vector<CompileError> errors;
  std::vector<CompileWarning> warnings;
};

class Compiler {
public:
  CompileResult compile(const ast::Program &program);

private:
  struct Local {
    std::string name;
    bool is_mutable = true;
  };

  struct LoopInfo {
    std::vector<std::size_t> break_jumps;
    std::vector<std::size_t> continue_jumps;
  };

  void push_scope();
  void pop_scope();

  void compile_function(const ast::FunctionDecl &function);
  void compile_stmt(const ast::Stmt &stmt);
  void compile_expr(const ast::Expr &expr);
  void compile_assignment(const ast::AssignExpr &assign);

  void emit(OpCode op, ast::SourceLocation location);
  void emit_operand(OpCode op, uint32_t operand, ast::SourceLocation location);
  void emit_constant(Value value, ast::SourceLocation location);
  std::size_t emit_jump(OpCode op, ast::SourceLocation location);
  void patch_jump(std::size_t offset);
  void patch_jump_to(std::size_t offset, std::size_t target);
  int resolve_local(const std::string &name) const;
  bool declare_local(const ast::VarDeclStmt &var_decl, uint32_t *slot);
  void error_at(ast::SourceLocation location, std::string message);

  Chunk chunk_;
  std::vector<Local> locals_;
  std::vector<std::size_t> scope_stack_;
  std::vector<CompileError> errors_;
  std::vector<CompileWarning> warnings_;
  std::vector<LoopInfo> loop_stack_;
  std::unordered_set<std::string> used_;
  std::unordered_set<std::string> opened_;
  std::unordered_map<std::string, int> function_indices_;
  std::unordered_map<std::string, int> struct_indices_;
  std::unordered_map<std::string, int> enum_indices_;
};

} // namespace kinglet
