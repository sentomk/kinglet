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
  int length = 1;
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

struct TypeExpr {
  std::string name;
  std::vector<TypeExpr> type_args;
  std::string to_string() const;
};

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

struct ArrayLiteralExpr final : Expr {
  ArrayLiteralExpr(SourceLocation location, std::vector<ExprPtr> elements);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<ExprPtr> elements;
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
  CallExpr(SourceLocation location, ExprPtr callee, std::vector<TypeExpr> type_args,
           std::vector<ExprPtr> args);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr callee;
  std::vector<TypeExpr> type_args;
  std::vector<ExprPtr> args;
};

struct MatchArm {
  ExprPtr pattern;
  ExprPtr body;
};

struct MatchExpr final : Expr {
  MatchExpr(SourceLocation location, ExprPtr value, std::vector<MatchArm> arms);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr value;
  std::vector<MatchArm> arms;
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
  VarDeclStmt(SourceLocation location, std::string storage, TypeExpr type, std::string name,
              ExprPtr init);
  void print(std::ostream &out, int indent = 0) const override;

  std::string storage;
  TypeExpr type;
  std::string name;
  ExprPtr init;
};

struct UnpackDeclStmt final : Stmt {
  UnpackDeclStmt(SourceLocation location, std::vector<std::string> names,
                 std::string rest_name, ExprPtr init);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<std::string> names;
  std::string rest_name;
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

struct GuardStmt final : Stmt {
  GuardStmt(SourceLocation location, ExprPtr condition, StmtPtr else_body);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr condition;
  StmtPtr else_body;
};

struct WhileStmt final : Stmt {
  WhileStmt(SourceLocation location, ExprPtr condition, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr condition;
  StmtPtr body;
};

struct ForStmt final : Stmt {
  ForStmt(SourceLocation location, StmtPtr init, ExprPtr condition, StmtPtr step, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;

  StmtPtr init;
  ExprPtr condition;
  StmtPtr step;
  StmtPtr body;
};

struct BreakStmt final : Stmt {
  explicit BreakStmt(SourceLocation location);
  void print(std::ostream &out, int indent = 0) const override;
};

struct ContinueStmt final : Stmt {
  explicit ContinueStmt(SourceLocation location);
  void print(std::ostream &out, int indent = 0) const override;
};

struct Parameter {
  TypeExpr type;
  std::string name;
};

struct FunctionDecl final : Decl {
  FunctionDecl(SourceLocation location, TypeExpr return_type, std::string name,
               std::vector<std::string> type_params, std::vector<Parameter> params, StmtPtr body);
  void print(std::ostream &out, int indent = 0) const override;

  TypeExpr return_type;
  std::string name;
  std::vector<std::string> type_params;
  std::vector<Parameter> params;
  StmtPtr body;
};

struct ImportDecl final : Decl {
  ImportDecl(SourceLocation location, std::string module);
  void print(std::ostream &out, int indent = 0) const override;

  std::string module;
};

struct UsingDecl final : Decl {
  UsingDecl(SourceLocation location, std::string namespace_name, bool is_namespace);
  void print(std::ostream &out, int indent = 0) const override;

  std::string namespace_name;
  bool is_namespace;
};

struct NamespaceAccessExpr final : Expr {
  NamespaceAccessExpr(SourceLocation location, std::string namespace_name,
                      std::string member_name);
  void print(std::ostream &out, int indent = 0) const override;

  std::string namespace_name;
  std::string member_name;
};

struct FieldAccessExpr final : Expr {
  FieldAccessExpr(SourceLocation location, ExprPtr object, std::string field_name);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr object;
  std::string field_name;
};

struct FieldAssignExpr final : Expr {
  FieldAssignExpr(SourceLocation location, ExprPtr object, std::string field_name, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr object;
  std::string field_name;
  ExprPtr value;
};

struct IndexExpr final : Expr {
  IndexExpr(SourceLocation location, ExprPtr object, ExprPtr index);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr object;
  ExprPtr index;
};

struct IndexAssignExpr final : Expr {
  IndexAssignExpr(SourceLocation location, ExprPtr object, ExprPtr index, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr object;
  ExprPtr index;
  ExprPtr value;
};

struct FieldDef {
  TypeExpr type;
  std::string name;
};

struct StructLiteralExpr final : Expr {
  struct FieldInit {
    std::string name;
    ExprPtr value;
  };
  StructLiteralExpr(SourceLocation location, TypeExpr struct_type,
                    std::vector<FieldInit> fields);
  void print(std::ostream &out, int indent = 0) const override;

  TypeExpr struct_type;
  std::vector<FieldInit> fields;
};

struct StructDecl final : Decl {
  StructDecl(SourceLocation location, std::string name, std::vector<std::string> type_params,
             std::vector<FieldDef> fields);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
  std::vector<std::string> type_params;
  std::vector<FieldDef> fields;
};

struct EnumDecl final : Decl {
  EnumDecl(SourceLocation location, std::string name, std::vector<std::string> variants);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
  std::vector<std::string> variants;
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
