#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet::ast {

enum class BinaryOp {
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Eq,
  Neq,
  Lt,
  Gt,
  Le,
  Ge,
  And,
  Or,
};

enum class UnaryOp {
  Neg,
  Not,
  BitNot,
};

enum class AssignOp {
  Assign,
  AddAssign,
  SubAssign,
  MulAssign,
  DivAssign,
};

const char *binary_op_name(BinaryOp op);
const char *unary_op_name(UnaryOp op);
const char *assign_op_name(AssignOp op);

struct SourceLocation {
  int line = 0;
  int column = 0;
};

struct Node {
  explicit Node(SourceLocation location);
  virtual ~Node() = default;
  virtual void print(std::ostream &out, int indent = 0) const = 0;

  SourceLocation location;
};

struct Expr : Node {
  using Node::Node;
};
struct Stmt : Node {
  using Node::Node;
};
struct Decl : Node {
  using Node::Node;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using DeclPtr = std::unique_ptr<Decl>;

struct IntLiteralExpr final : Expr {
  IntLiteralExpr(SourceLocation location, int64_t value);
  void print(std::ostream &out, int indent = 0) const override;

  int64_t value;
};

struct FloatLiteralExpr final : Expr {
  FloatLiteralExpr(SourceLocation location, double value);
  void print(std::ostream &out, int indent = 0) const override;

  double value;
};

struct StringLiteralExpr final : Expr {
  StringLiteralExpr(SourceLocation location, std::string value);
  void print(std::ostream &out, int indent = 0) const override;

  std::string value;
};

struct BoolLiteralExpr final : Expr {
  BoolLiteralExpr(SourceLocation location, bool value);
  void print(std::ostream &out, int indent = 0) const override;

  bool value;
};

struct NullLiteralExpr final : Expr {
  explicit NullLiteralExpr(SourceLocation location);
  void print(std::ostream &out, int indent = 0) const override;
};

struct IdentifierExpr final : Expr {
  IdentifierExpr(SourceLocation location, std::string name);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
};

struct UnaryExpr final : Expr {
  UnaryExpr(SourceLocation location, UnaryOp op, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;

  UnaryOp op;
  ExprPtr right;
};

struct BinaryExpr final : Expr {
  BinaryExpr(SourceLocation location, ExprPtr left, BinaryOp op, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr left;
  BinaryOp op;
  ExprPtr right;
};

struct AssignExpr final : Expr {
  AssignExpr(SourceLocation location, std::string name, AssignOp op, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
  AssignOp op;
  ExprPtr value;
};

struct CallExpr final : Expr {
  CallExpr(SourceLocation location, ExprPtr callee, std::vector<ExprPtr> args);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr callee;
  std::vector<ExprPtr> args;
};

struct InspectArm {
  ExprPtr pattern;
  ExprPtr body;
};

struct InspectExpr final : Expr {
  InspectExpr(SourceLocation location, ExprPtr value, std::vector<InspectArm> arms);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr value;
  std::vector<InspectArm> arms;
};

struct ExprStmt final : Stmt {
  ExprStmt(SourceLocation location, ExprPtr expr);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr expr;
};

struct ReturnStmt final : Stmt {
  ReturnStmt(SourceLocation location, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr value;
};

struct VarDeclStmt final : Stmt {
  VarDeclStmt(SourceLocation location, std::string storage, std::string type, std::string name,
              ExprPtr init);
  void print(std::ostream &out, int indent = 0) const override;

  std::string storage;
  std::string type;
  std::string name;
  ExprPtr init;
};

struct BlockStmt final : Stmt {
  BlockStmt(SourceLocation location, std::vector<StmtPtr> statements);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<StmtPtr> statements;
};

struct IfStmt final : Stmt {
  IfStmt(SourceLocation location, ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr condition;
  StmtPtr then_branch;
  StmtPtr else_branch;
};

struct WhileStmt final : Stmt {
  WhileStmt(SourceLocation location, ExprPtr condition, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr condition;
  StmtPtr body;
};

struct Parameter {
  std::string type;
  std::string name;
};

struct FunctionDecl final : Decl {
  FunctionDecl(SourceLocation location, std::string return_type, std::string name,
               std::vector<Parameter> params, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;

  std::string return_type;
  std::string name;
  std::vector<Parameter> params;
  StmtPtr body;
};

struct ImportDecl final : Decl {
  ImportDecl(SourceLocation location, std::string module);
  void print(std::ostream &out, int indent = 0) const override;

  std::string module;
};

struct TopLevelStmtDecl final : Decl {
  TopLevelStmtDecl(SourceLocation location, StmtPtr stmt);
  void print(std::ostream &out, int indent = 0) const override;

  StmtPtr stmt;
};

struct Program final : Node {
  explicit Program(std::vector<DeclPtr> declarations);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<DeclPtr> declarations;
};

} // namespace kinglet::ast
