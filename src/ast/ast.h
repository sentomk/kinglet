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
  BitAnd,
  BitOr,
  BitXor,
  Shl,
  Shr,
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

// A map literal: `{}` (empty) or `{k1: v1, k2: v2}`. Parallel key/value lists.
struct MapLiteralExpr final : Expr {
  MapLiteralExpr(SourceLocation location, std::vector<ExprPtr> keys,
                 std::vector<ExprPtr> values);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<ExprPtr> keys;
  std::vector<ExprPtr> values;
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

// `lhs ?? rhs` — if lhs is a Result<T,E>::Ok(v), evaluates to v; otherwise
// evaluates rhs (which must produce a T or diverge). The optional
// err_binding form is `lhs ?? |e| rhs`, where the Err's payload is bound
// to `e` for use inside rhs.
struct NullCoalesceExpr final : Expr {
  NullCoalesceExpr(SourceLocation location, ExprPtr left, std::string err_binding, ExprPtr right);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr left;
  std::string err_binding;  // empty when the |e| form was not used
  ExprPtr right;
};

// `try expr` — if expr is a Result<T,E>::Ok(v), evaluates to v. If it is
// Err(e), returns Err(e) from the enclosing function (whose return type
// must be Result<_, E> or compatible).
struct TryExpr final : Expr {
  TryExpr(SourceLocation location, ExprPtr value);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr value;
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

struct BindingPattern final : Expr {
  BindingPattern(SourceLocation location, std::string name);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
};

struct ArrayPattern final : Expr {
  ArrayPattern(SourceLocation location, std::vector<ExprPtr> elements);
  void print(std::ostream &out, int indent = 0) const override;

  std::vector<ExprPtr> elements;
};

struct EnumPattern final : Expr {
  EnumPattern(SourceLocation location, std::string enum_name, std::string variant_name,
              std::vector<ExprPtr> fields);
  void print(std::ostream &out, int indent = 0) const override;

  std::string enum_name;
  std::string variant_name;
  std::vector<ExprPtr> fields;
};

struct MatchArm {
  ExprPtr pattern;
  ExprPtr guard;
  ExprPtr body;
};

struct MatchExpr final : Expr {
  MatchExpr(SourceLocation location, ExprPtr value, std::vector<MatchArm> arms);
  void print(std::ostream &out, int indent = 0) const override;

  ExprPtr value;
  std::vector<MatchArm> arms;
};

// Cast expression: `Type(value)` or `Type(value) else fallback`. The fallback
// is required for fallible source/target pairs (e.g. string→int) and forbidden
// for infallible ones; the type checker enforces this. The fallback statement
// is either an ExprStmt (for `else expr`) or a BlockStmt that must terminate.
struct CastExpr final : Expr {
  CastExpr(SourceLocation location, TypeExpr target, ExprPtr value, StmtPtr fallback);
  void print(std::ostream &out, int indent = 0) const override;

  TypeExpr target;
  ExprPtr value;
  StmtPtr fallback;
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
  bool is_public = false;
};

struct ImportDecl final : Decl {
  ImportDecl(SourceLocation location, std::string path, std::string alias,
             std::vector<std::string> selected_symbols);
  void print(std::ostream &out, int indent = 0) const override;

  std::string path;
  std::string alias;
  std::vector<std::string> selected_symbols;
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
  bool is_public = false;
};

struct EnumVariantDecl {
  std::string name;
  std::vector<TypeExpr> param_types;
};

struct EnumDecl final : Decl {
  EnumDecl(SourceLocation location, std::string name, std::vector<EnumVariantDecl> variants);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
  std::vector<EnumVariantDecl> variants;
  bool is_public = false;
};

struct TraitMethodDecl {
  TypeExpr return_type;
  std::string name;
  std::vector<Parameter> params;
  StmtPtr default_body;
};

struct TraitDecl final : Decl {
  TraitDecl(SourceLocation location, std::string name,
            std::vector<TraitMethodDecl> methods);
  void print(std::ostream &out, int indent = 0) const override;

  std::string name;
  std::vector<TraitMethodDecl> methods;
  bool is_public = false;
};

struct ImplDecl final : Decl {
  ImplDecl(SourceLocation location, std::string target_type, std::string trait_name,
           std::vector<std::unique_ptr<FunctionDecl>> methods);
  void print(std::ostream &out, int indent = 0) const override;

  std::string target_type;
  std::string trait_name;
  std::vector<std::unique_ptr<FunctionDecl>> methods;
  bool is_public = false;
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
