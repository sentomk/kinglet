#include "ast/ast.h"

#include <ostream>
#include <utility>

namespace kinglet::ast {

std::string TypeExpr::to_string() const {
  if (name == "Array" && type_args.size() == 1) {
    return type_args[0].to_string() + "[]";
  }
  if (type_args.empty()) return name;
  std::string result = name + "<";
  for (size_t i = 0; i < type_args.size(); ++i) {
    if (i > 0) result += ", ";
    result += type_args[i].to_string();
  }
  result += ">";
  return result;
}

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

ArrayLiteralExpr::ArrayLiteralExpr(SourceLocation location, std::vector<ExprPtr> elements)
    : Expr(location), elements(std::move(elements)) {}

void ArrayLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(array-literal";
  for (const ExprPtr &element : elements) {
    print_child(out, *element, indent);
  }
  out << ")";
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

CallExpr::CallExpr(SourceLocation location, ExprPtr callee, std::vector<TypeExpr> type_args,
                   std::vector<ExprPtr> args)
    : Expr(location), callee(std::move(callee)), type_args(std::move(type_args)),
      args(std::move(args)) {}

void CallExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(call";
  print_child(out, *callee, indent);
  for (const ExprPtr &arg : args) {
    print_child(out, *arg, indent);
  }
  out << ")";
}

BindingPattern::BindingPattern(SourceLocation location, std::string name)
    : Expr(location), name(std::move(name)) {}

void BindingPattern::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(let " << name << ")";
}

ArrayPattern::ArrayPattern(SourceLocation location, std::vector<ExprPtr> elements)
    : Expr(location), elements(std::move(elements)) {}

void ArrayPattern::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "[";
  for (std::size_t i = 0; i < elements.size(); ++i) {
    if (i > 0) out << ", ";
    elements[i]->print(out, 0);
  }
  out << "]";
}

EnumPattern::EnumPattern(SourceLocation location, std::string enum_name, std::string variant_name,
                         std::vector<ExprPtr> fields)
    : Expr(location), enum_name(std::move(enum_name)), variant_name(std::move(variant_name)),
      fields(std::move(fields)) {}

void EnumPattern::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(enum-pattern " << enum_name << "::" << variant_name;
  for (const ExprPtr &f : fields) {
    out << " ";
    f->print(out, 0);
  }
  out << ")";
}

MatchExpr::MatchExpr(SourceLocation location, ExprPtr value, std::vector<MatchArm> arms)
    : Expr(location), value(std::move(value)), arms(std::move(arms)) {}

void MatchExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(match";
  print_child(out, *value, indent);
  for (const MatchArm &arm : arms) {
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

VarDeclStmt::VarDeclStmt(SourceLocation location, std::string storage, TypeExpr type,
                         std::string name, ExprPtr init)
    : Stmt(location), storage(std::move(storage)), type(std::move(type)),
      name(std::move(name)), init(std::move(init)) {}

void VarDeclStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(var";
  if (!storage.empty()) {
    out << " storage=" << storage;
  }
  if (!type.name.empty()) {
    out << " type=" << type.to_string();
  }
  out << " name=" << name;
  if (init) {
    print_child(out, *init, indent);
  }
  out << ")";
}

UnpackDeclStmt::UnpackDeclStmt(SourceLocation location, std::vector<std::string> names,
                               std::string rest_name, ExprPtr init)
    : Stmt(location), names(std::move(names)), rest_name(std::move(rest_name)),
      init(std::move(init)) {}

void UnpackDeclStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(unpack [";
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (i > 0) out << ", ";
    out << names[i];
  }
  if (!rest_name.empty()) {
    if (!names.empty()) out << ", ";
    out << "..." << rest_name;
  }
  out << "]";
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

GuardStmt::GuardStmt(SourceLocation location, ExprPtr condition, StmtPtr else_body)
    : Stmt(location), condition(std::move(condition)), else_body(std::move(else_body)) {}

void GuardStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(guard";
  print_child(out, *condition, indent);
  out << " else";
  print_child(out, *else_body, indent);
  out << ")";
}

void WhileStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(while";
  print_child(out, *condition, indent);
  print_child(out, *body, indent);
  out << ")";
}

ForStmt::ForStmt(SourceLocation location, StmtPtr init, ExprPtr condition, StmtPtr step,
                 StmtPtr body)
    : Stmt(location), init(std::move(init)), condition(std::move(condition)),
      step(std::move(step)), body(std::move(body)) {}

void ForStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(for";
  if (init) {
    print_child(out, *init, indent);
  } else {
    write_indent(out, indent + 1);
    out << "(null-init)";
  }
  if (condition) {
    print_child(out, *condition, indent);
  } else {
    write_indent(out, indent + 1);
    out << "(null-cond)";
  }
  if (step) {
    print_child(out, *step, indent);
  } else {
    write_indent(out, indent + 1);
    out << "(null-step)";
  }
  print_child(out, *body, indent);
  out << ")";
}

BreakStmt::BreakStmt(SourceLocation location) : Stmt(location) {}

void BreakStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(break)";
}

ContinueStmt::ContinueStmt(SourceLocation location) : Stmt(location) {}

void ContinueStmt::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(continue)";
}

FunctionDecl::FunctionDecl(SourceLocation location, TypeExpr return_type, std::string name,
                           std::vector<std::string> type_params, std::vector<Parameter> params,
                           StmtPtr body)
    : Decl(location), return_type(std::move(return_type)), name(std::move(name)),
      type_params(std::move(type_params)), params(std::move(params)), body(std::move(body)) {}

void FunctionDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(function " << return_type.to_string() << ' ' << name << " (params";
  for (const Parameter &param : params) {
    out << " (" << param.type.to_string() << ' ' << param.name << ')';
  }
  out << ")";
  print_child(out, *body, indent);
  out << ")";
}

ImportDecl::ImportDecl(SourceLocation location, std::string path,
                       std::string alias, std::vector<std::string> selected_symbols)
    : Decl(location), path(std::move(path)), alias(std::move(alias)),
      selected_symbols(std::move(selected_symbols)) {}

void ImportDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(import \"" << path << "\"";
  if (!alias.empty()) out << " as " << alias;
  if (!selected_symbols.empty()) {
    out << " {";
    for (std::size_t i = 0; i < selected_symbols.size(); ++i) {
      if (i > 0) out << ", ";
      out << selected_symbols[i];
    }
    out << "}";
  }
  out << ")";
}

UsingDecl::UsingDecl(SourceLocation location, std::string namespace_name, bool is_namespace)
    : Decl(location), namespace_name(std::move(namespace_name)), is_namespace(is_namespace) {}

void UsingDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  if (is_namespace) {
    out << "(using-namespace " << namespace_name << ")";
  } else {
    out << "(using " << namespace_name << ")";
  }
}

NamespaceAccessExpr::NamespaceAccessExpr(SourceLocation location, std::string namespace_name,
                                         std::string member_name)
    : Expr(location), namespace_name(std::move(namespace_name)),
      member_name(std::move(member_name)) {}

void NamespaceAccessExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(namespace-access " << namespace_name << "::" << member_name << ")";
}

FieldAccessExpr::FieldAccessExpr(SourceLocation location, ExprPtr object, std::string field_name)
    : Expr(location), object(std::move(object)), field_name(std::move(field_name)) {}

void FieldAccessExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(field-access ." << field_name;
  print_child(out, *object, indent);
  out << ")";
}

FieldAssignExpr::FieldAssignExpr(SourceLocation location, ExprPtr object, std::string field_name,
                                 ExprPtr value)
    : Expr(location), object(std::move(object)), field_name(std::move(field_name)),
      value(std::move(value)) {}

void FieldAssignExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(field-assign ." << field_name;
  print_child(out, *object, indent);
  print_child(out, *value, indent);
  out << ")";
}

IndexExpr::IndexExpr(SourceLocation location, ExprPtr object, ExprPtr index)
    : Expr(location), object(std::move(object)), index(std::move(index)) {}

void IndexExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(index";
  print_child(out, *object, indent);
  print_child(out, *index, indent);
  out << ")";
}

IndexAssignExpr::IndexAssignExpr(SourceLocation location, ExprPtr object, ExprPtr index,
                                 ExprPtr value)
    : Expr(location), object(std::move(object)), index(std::move(index)),
      value(std::move(value)) {}

void IndexAssignExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(index-assign";
  print_child(out, *object, indent);
  print_child(out, *index, indent);
  print_child(out, *value, indent);
  out << ")";
}

StructLiteralExpr::StructLiteralExpr(SourceLocation location, TypeExpr struct_type,
                                     std::vector<FieldInit> fields)
    : Expr(location), struct_type(std::move(struct_type)), fields(std::move(fields)) {}

void StructLiteralExpr::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(struct-literal " << struct_type.to_string();
  for (const FieldInit &f : fields) {
    out << '\n';
    write_indent(out, indent + 1);
    out << "(field " << f.name;
    print_child(out, *f.value, indent + 1);
    out << ")";
  }
  out << ")";
}

StructDecl::StructDecl(SourceLocation location, std::string name,
                       std::vector<std::string> type_params, std::vector<FieldDef> fields)
    : Decl(location), name(std::move(name)), type_params(std::move(type_params)),
      fields(std::move(fields)) {}

void StructDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(struct " << name;
  for (const FieldDef &f : fields) {
    out << '\n';
    write_indent(out, indent + 1);
    out << "(" << f.type.to_string() << ' ' << f.name << ")";
  }
  out << ")";
}

EnumDecl::EnumDecl(SourceLocation location, std::string name, std::vector<EnumVariantDecl> variants)
    : Decl(location), name(std::move(name)), variants(std::move(variants)) {}

void EnumDecl::print(std::ostream &out, int indent) const {
  write_indent(out, indent);
  out << "(enum " << name;
  for (const EnumVariantDecl &v : variants) {
    out << '\n';
    write_indent(out, indent + 1);
    out << v.name;
    if (!v.param_types.empty()) {
      out << "(";
      for (std::size_t i = 0; i < v.param_types.size(); ++i) {
        if (i > 0) out << ", ";
        out << v.param_types[i].name;
      }
      out << ")";
    }
  }
  out << ")";
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
