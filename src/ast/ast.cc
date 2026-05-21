#include "ast/ast.h"

#include <ostream>
#include <utility>

namespace kinglet::ast {

const char *binary_op_name(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Sub:
    return "-";
  case BinaryOp::Mul:
    return "*";
  case BinaryOp::Div:
    return "/";
  case BinaryOp::Mod:
    return "%";
  case BinaryOp::Eq:
    return "==";
  case BinaryOp::Neq:
    return "!=";
  case BinaryOp::Lt:
    return "<";
  case BinaryOp::Gt:
    return ">";
  case BinaryOp::Le:
    return "<=";
  case BinaryOp::Ge:
    return ">=";
  case BinaryOp::And:
    return "&&";
  case BinaryOp::Or:
    return "||";
  }
  return "?";
}

const char *unary_op_name(UnaryOp op) {
  switch (op) {
  case UnaryOp::Neg:
    return "-";
  case UnaryOp::Not:
    return "!";
  case UnaryOp::BitNot:
    return "~";
  }
  return "?";
}

const char *assign_op_name(AssignOp op) {
  switch (op) {
  case AssignOp::Assign:
    return "=";
  case AssignOp::AddAssign:
    return "+=";
  case AssignOp::SubAssign:
    return "-=";
  case AssignOp::MulAssign:
    return "*=";
  case AssignOp::DivAssign:
    return "/=";
  }
  return "?";
}

namespace {

void write_indent(std::ostream &out, int indent) {
  for (int i = 0; i < indent; ++i) {
    out << "  ";
  }
}

void print_child(std::ostream &out, const Node &node, int indent) {
  out << '\n';
  node.print(out, indent + 1);
}

} // namespace

Node::Node(SourceLocation location) : location(location) {}

IntLiteralExpr::IntLiteralExpr(SourceLocation location, int64_t value)
    : Expr(location), value(value) {}

void IntLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(int-literal " << value << ")";
}

FloatLiteralExpr::FloatLiteralExpr(SourceLocation location, double value)
    : Expr(location), value(value) {}

void FloatLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(float-literal " << value << ")";
}

StringLiteralExpr::StringLiteralExpr(SourceLocation location, std::string value)
    : Expr(location), value(std::move(value)) {}

void StringLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(string-literal " << value << ")";
}

BoolLiteralExpr::BoolLiteralExpr(SourceLocation location, bool value)
    : Expr(location), value(value) {}

void BoolLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(bool-literal " << (value ? "true" : "false") << ")";
}

NullLiteralExpr::NullLiteralExpr(SourceLocation location)
    : Expr(location) {}

void NullLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(null-literal)";
}

IdentifierExpr::IdentifierExpr(SourceLocation location, std::string name)
    : Expr(location), name(std::move(name)) {}

void IdentifierExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(identifier " << name << ")";
}

UnaryExpr::UnaryExpr(SourceLocation location, UnaryOp op, ExprPtr right)
    : Expr(location), op(op), right(std::move(right)) {}

void UnaryExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(unary " << unary_op_name(op);
  print_child(out, *right, indent);
  out << ")";
}

BinaryExpr::BinaryExpr(SourceLocation location, ExprPtr left, BinaryOp op, ExprPtr right)
    : Expr(location), left(std::move(left)), op(op), right(std::move(right)) {}

void BinaryExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(binary " << binary_op_name(op);
  print_child(out, *left, indent);
  print_child(out, *right, indent);
  out << ")";
}

AssignExpr::AssignExpr(SourceLocation location, std::string name, AssignOp op, ExprPtr value)
    : Expr(location), name(std::move(name)), op(op), value(std::move(value)) {}

void AssignExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(assign " << assign_op_name(op) << '\n';
  write_indent(out, indent + 1);
  out << "(identifier " << name << ")";
  print_child(out, *value, indent);
  out << ")";
}

CallExpr::CallExpr(SourceLocation location, ExprPtr callee, std::vector<ExprPtr> args)
    : Expr(location), callee(std::move(callee)), args(std::move(args)) {}

void CallExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(call";
  print_child(out, *callee, indent);
  for (const ExprPtr &arg : args) {
    print_child(out, *arg, indent);
  }
  out << ")";
}

InspectExpr::InspectExpr(SourceLocation location, ExprPtr value, std::vector<InspectArm> arms)
    : Expr(location), value(std::move(value)), arms(std::move(arms)) {}

void InspectExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(inspect";
  print_child(out, *value, indent);
  for (const InspectArm &arm : arms) {
    write_indent(out, indent + 1);
    out << "(arm";
    print_child(out, *arm.pattern, indent + 1);
    print_child(out, *arm.body, indent + 1);
    out << ")";
  }
  out << ")";
}

ExprStmt::ExprStmt(SourceLocation location, ExprPtr expr)
    : Stmt(location), expr(std::move(expr)) {}

void ExprStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(expr-stmt";
  print_child(out, *expr, indent);
  out << ")";
}

ReturnStmt::ReturnStmt(SourceLocation location, ExprPtr value)
    : Stmt(location), value(std::move(value)) {}

void ReturnStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(return";
  if (value) {
    print_child(out, *value, indent);
  }
  out << ")";
}

VarDeclStmt::VarDeclStmt(SourceLocation location, std::string storage, std::string type,
                         std::string name, ExprPtr init)
    : Stmt(location), storage(std::move(storage)), type(std::move(type)),
      name(std::move(name)), init(std::move(init)) {}

void VarDeclStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(var";
  if (!storage.empty()) {
    out << " storage=" << storage;
  }
  if (!type.empty()) {
    out << " type=" << type;
  }
  out << " name=" << name;
  if (init) {
    print_child(out, *init, indent);
  }
  out << ")";
}

BlockStmt::BlockStmt(SourceLocation location, std::vector<StmtPtr> statements)
    : Stmt(location), statements(std::move(statements)) {}

void BlockStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(block";
  for (const StmtPtr &statement : statements) {
    print_child(out, *statement, indent);
  }
  out << ")";
}

IfStmt::IfStmt(SourceLocation location, ExprPtr condition, StmtPtr then_branch,
               StmtPtr else_branch)
    : Stmt(location), condition(std::move(condition)), then_branch(std::move(then_branch)),
      else_branch(std::move(else_branch)) {}

void IfStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(if";
  print_child(out, *condition, indent);
  print_child(out, *then_branch, indent);
  if (else_branch) {
    print_child(out, *else_branch, indent);
  }
  out << ")";
}

WhileStmt::WhileStmt(SourceLocation location, ExprPtr condition, StmtPtr body)
    : Stmt(location), condition(std::move(condition)), body(std::move(body)) {}

void WhileStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(while";
  print_child(out, *condition, indent);
  print_child(out, *body, indent);
  out << ")";
}

FunctionDecl::FunctionDecl(SourceLocation location, std::string return_type, std::string name,
                           std::vector<Parameter> params, StmtPtr body)
    : Decl(location), return_type(std::move(return_type)), name(std::move(name)),
      params(std::move(params)), body(std::move(body)) {}

void FunctionDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(function " << return_type << ' ' << name << " (params";
  for (const Parameter &param : params) {
    out << " (" << param.type << ' ' << param.name << ')';
  }
  out << ")";
  print_child(out, *body, indent);
  out << ")";
}

ImportDecl::ImportDecl(SourceLocation location, std::string module)
    : Decl(location), module(std::move(module)) {}

void ImportDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(import " << module << ")";
}

UsingDecl::UsingDecl(SourceLocation location, std::string namespace_name)
    : Decl(location), namespace_name(std::move(namespace_name)) {}

void UsingDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(using " << namespace_name << ")";
}

NamespaceAccessExpr::NamespaceAccessExpr(SourceLocation location, std::string namespace_name,
                                         std::string member_name)
    : Expr(location), namespace_name(std::move(namespace_name)),
      member_name(std::move(member_name)) {}

void NamespaceAccessExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(namespace-access " << namespace_name << "::" << member_name << ")";
}

TopLevelStmtDecl::TopLevelStmtDecl(SourceLocation location, StmtPtr stmt)
    : Decl(location), stmt(std::move(stmt)) {}

void TopLevelStmtDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(top-level";
  print_child(out, *stmt, indent);
  out << ")";
}

Program::Program(std::vector<DeclPtr> declarations)
    : Node(SourceLocation{}), declarations(std::move(declarations)) {}

void Program::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(program";
  for (const DeclPtr &declaration : declarations) {
    print_child(out, *declaration, indent);
  }
  out << ")\n";
}

} // namespace kinglet::ast
