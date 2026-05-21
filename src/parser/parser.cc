#include "parser/parser.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace kinglet {

namespace {

bool is_assignment_operator(TokenType type) {
  return type == TokenType::EQUAL || type == TokenType::PLUS_EQUAL ||
         type == TokenType::MINUS_EQUAL || type == TokenType::STAR_EQUAL ||
         type == TokenType::SLASH_EQUAL;
}

ast::AssignOp token_to_assign_op(TokenType type) {
  switch (type) {
  case TokenType::EQUAL:
    return ast::AssignOp::Assign;
  case TokenType::PLUS_EQUAL:
    return ast::AssignOp::AddAssign;
  case TokenType::MINUS_EQUAL:
    return ast::AssignOp::SubAssign;
  case TokenType::STAR_EQUAL:
    return ast::AssignOp::MulAssign;
  case TokenType::SLASH_EQUAL:
    return ast::AssignOp::DivAssign;
  default:
    return ast::AssignOp::Assign;
  }
}

ast::BinaryOp token_to_binary_op(TokenType type) {
  switch (type) {
  case TokenType::PLUS:
    return ast::BinaryOp::Add;
  case TokenType::MINUS:
    return ast::BinaryOp::Sub;
  case TokenType::STAR:
    return ast::BinaryOp::Mul;
  case TokenType::SLASH:
    return ast::BinaryOp::Div;
  case TokenType::PERCENT:
    return ast::BinaryOp::Mod;
  case TokenType::EQUAL_EQUAL:
    return ast::BinaryOp::Eq;
  case TokenType::BANG_EQUAL:
    return ast::BinaryOp::Neq;
  case TokenType::LESS:
    return ast::BinaryOp::Lt;
  case TokenType::GREATER:
    return ast::BinaryOp::Gt;
  case TokenType::LESS_EQUAL:
    return ast::BinaryOp::Le;
  case TokenType::GREATER_EQUAL:
    return ast::BinaryOp::Ge;
  case TokenType::AMP_AMP:
    return ast::BinaryOp::And;
  case TokenType::PIPE_PIPE:
    return ast::BinaryOp::Or;
  default:
    return ast::BinaryOp::Add;
  }
}

ast::UnaryOp token_to_unary_op(TokenType type) {
  switch (type) {
  case TokenType::MINUS:
    return ast::UnaryOp::Neg;
  case TokenType::BANG:
    return ast::UnaryOp::Not;
  case TokenType::TILDE:
    return ast::UnaryOp::BitNot;
  default:
    return ast::UnaryOp::Neg;
  }
}

} // namespace

Parser::Parser(const std::vector<Token> &tokens) : tokens_(tokens) {}

ParseResult Parser::parse() {
  std::vector<ast::DeclPtr> declarations;
  while (!is_at_end()) {
    ast::DeclPtr decl = declaration();
    if (decl) {
      declarations.push_back(std::move(decl));
    }
  }

  return ParseResult{
      .program = std::make_unique<ast::Program>(std::move(declarations)),
      .errors = std::move(errors_),
  };
}

ast::DeclPtr Parser::declaration() {
  if (match(TokenType::IMPORT)) {
    return import_declaration();
  }

  if (match(TokenType::USING)) {
    return using_declaration();
  }

  if (is_function_declaration_start()) {
    return function_declaration();
  }

  ast::StmtPtr stmt = statement();
  if (!stmt) {
    synchronize();
    return nullptr;
  }
  const ast::SourceLocation location = stmt->location;
  return std::make_unique<ast::TopLevelStmtDecl>(location, std::move(stmt));
}

ast::DeclPtr Parser::import_declaration() {
  const Token &import_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected module name after import.");
  consume(TokenType::SEMICOLON, "Expected ';' after import.");
  return std::make_unique<ast::ImportDecl>(location_of(import_token), token_text(name));
}

ast::DeclPtr Parser::using_declaration() {
  const Token &using_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected namespace name after 'using'.");
  consume(TokenType::SEMICOLON, "Expected ';' after using declaration.");
  return std::make_unique<ast::UsingDecl>(location_of(using_token), token_text(name));
}

ast::DeclPtr Parser::function_declaration() {
  const Token &return_type_token = peek();
  std::string return_type = parse_type_name();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected function name.");
  consume(TokenType::LEFT_PAREN, "Expected '(' after function name.");
  std::vector<ast::Parameter> params = parameters();
  consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.");
  ast::StmtPtr body = function_body();
  return std::make_unique<ast::FunctionDecl>(location_of(return_type_token), std::move(return_type),
                                             token_text(name), std::move(params), std::move(body));
}

ast::StmtPtr Parser::statement() {
  if (match(TokenType::LEFT_BRACE)) {
    return block_statement();
  }
  if (match(TokenType::RETURN)) {
    return return_statement();
  }
  if (match(TokenType::IF)) {
    return if_statement();
  }
  if (match(TokenType::WHILE)) {
    return while_statement();
  }
  if (is_declaration_start()) {
    return var_declaration();
  }
  return expression_statement();
}

ast::StmtPtr Parser::block_statement() {
  const Token &left_brace = previous();
  std::vector<ast::StmtPtr> statements;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    statements.push_back(statement());
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after block.");
  return std::make_unique<ast::BlockStmt>(location_of(left_brace), std::move(statements));
}

ast::StmtPtr Parser::return_statement() {
  const Token &return_token = previous();
  ast::ExprPtr value;
  if (!check(TokenType::SEMICOLON)) {
    value = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after return value.");
  return std::make_unique<ast::ReturnStmt>(location_of(return_token), std::move(value));
}

ast::StmtPtr Parser::if_statement() {
  const Token &if_token = previous();
  ast::ExprPtr condition = condition_expression();
  ast::StmtPtr then_branch = statement();
  ast::StmtPtr else_branch;
  if (match(TokenType::ELSE)) {
    else_branch = statement();
  }
  return std::make_unique<ast::IfStmt>(location_of(if_token), std::move(condition), std::move(then_branch),
                                       std::move(else_branch));
}

ast::StmtPtr Parser::while_statement() {
  const Token &while_token = previous();
  ast::ExprPtr condition = condition_expression();
  ast::StmtPtr body = statement();
  return std::make_unique<ast::WhileStmt>(location_of(while_token), std::move(condition),
                                          std::move(body));
}

ast::StmtPtr Parser::var_declaration() {
  const Token &start_token = peek();
  std::string storage;
  if (match(TokenType::CONST) || match(TokenType::MUT)) {
    storage = token_text(previous());
  }

  std::string type;
  Token name = peek();
  if (is_type_start(peek().type) && check_next(TokenType::IDENTIFIER)) {
    type = parse_type_name();
    name = consume(TokenType::IDENTIFIER, "Expected variable name.");
  } else {
    name = consume(TokenType::IDENTIFIER, "Expected variable name.");
  }

  ast::ExprPtr init;
  if (match(TokenType::EQUAL)) {
    init = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
  return std::make_unique<ast::VarDeclStmt>(location_of(start_token), std::move(storage), std::move(type),
                                            token_text(name), std::move(init));
}

ast::StmtPtr Parser::expression_statement() {
  ast::ExprPtr expr = expression();
  const ast::SourceLocation location = expr->location;
  consume(TokenType::SEMICOLON, "Expected ';' after expression.");
  return std::make_unique<ast::ExprStmt>(location, std::move(expr));
}

ast::ExprPtr Parser::expression() {
  return assignment();
}

ast::ExprPtr Parser::assignment() {
  ast::ExprPtr expr = logical_or();
  if (is_assignment_operator(peek().type)) {
    const Token &op = advance();
    ast::ExprPtr value = assignment();
    auto *identifier = dynamic_cast<ast::IdentifierExpr *>(expr.get());
    if (identifier == nullptr) {
      error_at(op, "Invalid assignment target.");
      return value;
    }
    const ast::SourceLocation location = expr->location;
    return std::make_unique<ast::AssignExpr>(location, identifier->name,
                                             token_to_assign_op(op.type), std::move(value));
  }
  return expr;
}

ast::ExprPtr Parser::logical_or() {
  ast::ExprPtr expr = logical_and();
  while (match(TokenType::PIPE_PIPE)) {
    const Token &op = previous();
    ast::ExprPtr right = logical_and();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::logical_and() {
  ast::ExprPtr expr = equality();
  while (match(TokenType::AMP_AMP)) {
    const Token &op = previous();
    ast::ExprPtr right = equality();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::equality() {
  ast::ExprPtr expr = comparison();
  while (match_any({TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL})) {
    const Token &op = previous();
    ast::ExprPtr right = comparison();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::comparison() {
  ast::ExprPtr expr = term();
  while (match_any({TokenType::LESS, TokenType::LESS_EQUAL, TokenType::GREATER,
                    TokenType::GREATER_EQUAL})) {
    const Token &op = previous();
    ast::ExprPtr right = term();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::term() {
  ast::ExprPtr expr = factor();
  while (match_any({TokenType::PLUS, TokenType::MINUS})) {
    const Token &op = previous();
    ast::ExprPtr right = factor();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::factor() {
  ast::ExprPtr expr = unary();
  while (match_any({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
    const Token &op = previous();
    ast::ExprPtr right = unary();
    expr = std::make_unique<ast::BinaryExpr>(location_of(op), std::move(expr),
                                             token_to_binary_op(op.type), std::move(right));
  }
  return expr;
}

ast::ExprPtr Parser::unary() {
  if (match_any({TokenType::BANG, TokenType::MINUS, TokenType::TILDE})) {
    const Token &op = previous();
    ast::ExprPtr right = unary();
    return std::make_unique<ast::UnaryExpr>(location_of(op), token_to_unary_op(op.type),
                                            std::move(right));
  }
  return call();
}

ast::ExprPtr Parser::call() {
  ast::ExprPtr expr = primary();
  while (match(TokenType::LEFT_PAREN)) {
    std::vector<ast::ExprPtr> args;
    if (!check(TokenType::RIGHT_PAREN)) {
      do {
        args.push_back(expression());
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");
    const ast::SourceLocation location = expr->location;
    expr = std::make_unique<ast::CallExpr>(location, std::move(expr), std::move(args));
  }
  return expr;
}

ast::ExprPtr Parser::primary() {
  if (match(TokenType::INSPECT)) {
    return match_expression();
  }
  if (match(TokenType::INTEGER)) {
    const Token &literal = previous();
    return std::make_unique<ast::IntLiteralExpr>(location_of(literal), literal.int_value);
  }
  if (match(TokenType::FLOAT_LIT)) {
    const Token &literal = previous();
    return std::make_unique<ast::FloatLiteralExpr>(location_of(literal), literal.float_value);
  }
  if (match(TokenType::STRING_LIT)) {
    const Token &literal = previous();
    return std::make_unique<ast::StringLiteralExpr>(location_of(literal), token_text(literal));
  }
  if (match(TokenType::CHAR_LIT)) {
    const Token &literal = previous();
    return std::make_unique<ast::IntLiteralExpr>(location_of(literal), literal.int_value);
  }
  if (match(TokenType::TRUE)) {
    const Token &literal = previous();
    return std::make_unique<ast::BoolLiteralExpr>(location_of(literal), true);
  }
  if (match(TokenType::FALSE)) {
    const Token &literal = previous();
    return std::make_unique<ast::BoolLiteralExpr>(location_of(literal), false);
  }
  if (match(TokenType::NULL_)) {
    const Token &literal = previous();
    return std::make_unique<ast::NullLiteralExpr>(location_of(literal));
  }
  if (match(TokenType::IDENTIFIER)) {
    const Token &identifier = previous();
    if (match(TokenType::COLON_COLON)) {
      const Token &member =
          consume(TokenType::IDENTIFIER, "Expected member name after '::'.");
      return std::make_unique<ast::NamespaceAccessExpr>(
          location_of(identifier), token_text(identifier), token_text(member));
    }
    return std::make_unique<ast::IdentifierExpr>(location_of(identifier), token_text(identifier));
  }
  if (match(TokenType::LEFT_PAREN)) {
    ast::ExprPtr expr = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after expression.");
    return expr;
  }

  error_at(peek(), "Expected expression.");
  const Token &error_token = advance();
  return std::make_unique<ast::NullLiteralExpr>(location_of(error_token));
}

ast::ExprPtr Parser::condition_expression() {
  if (match(TokenType::LEFT_PAREN)) {
    ast::ExprPtr condition = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after condition.");
    return condition;
  }
  return expression();
}

ast::ExprPtr Parser::match_expression() {
  const Token &inspect_token = previous();
  ast::ExprPtr value = expression();
  consume(TokenType::LEFT_BRACE, "Expected '{' after inspect expression.");
  
  std::vector<ast::InspectArm> arms;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    ast::ExprPtr pattern = expression();
    consume(TokenType::FAT_ARROW, "Expected '=>' after inspect pattern.");
    ast::ExprPtr body = expression();
    arms.push_back(ast::InspectArm{std::move(pattern), std::move(body)});
    if (!check(TokenType::RIGHT_BRACE)) {
      consume(TokenType::COMMA, "Expected ',' after inspect arm.");
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after inspect arms.");
  return std::make_unique<ast::InspectExpr>(location_of(inspect_token), std::move(value),
                                            std::move(arms));
}

std::vector<ast::Parameter> Parser::parameters() {
  std::vector<ast::Parameter> params;
  if (check(TokenType::RIGHT_PAREN)) {
    return params;
  }

  do {
    std::string type = parse_type_name();
    const Token &name = consume(TokenType::IDENTIFIER, "Expected parameter name.");
    params.push_back(ast::Parameter{std::move(type), token_text(name)});
  } while (match(TokenType::COMMA));

  return params;
}

ast::StmtPtr Parser::function_body() {
  if (match(TokenType::LEFT_BRACE)) {
    return block_statement();
  }
  if (match(TokenType::FAT_ARROW)) {
    ast::ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression body.");
    const ast::SourceLocation location = value->location;
    return std::make_unique<ast::ReturnStmt>(location, std::move(value));
  }

  error_at(peek(), "Expected function body.");
  return std::make_unique<ast::BlockStmt>(location_of(peek()), std::vector<ast::StmtPtr>{});
}

bool Parser::is_at_end() const {
  return peek().type == TokenType::END_OF_FILE;
}

bool Parser::check(TokenType type) const {
  return !is_at_end() && peek().type == type;
}

bool Parser::check_next(TokenType type) const {
  return current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == type;
}

bool Parser::check_after_next(TokenType type) const {
  return current_ + 2 < tokens_.size() && tokens_[current_ + 2].type == type;
}

bool Parser::match(TokenType type) {
  if (!check(type)) {
    return false;
  }
  advance();
  return true;
}

bool Parser::match_any(std::initializer_list<TokenType> types) {
  for (TokenType type : types) {
    if (match(type)) {
      return true;
    }
  }
  return false;
}

const Token &Parser::advance() {
  if (!is_at_end()) {
    ++current_;
  }
  return previous();
}

const Token &Parser::peek() const {
  return tokens_[current_];
}

const Token &Parser::previous() const {
  return tokens_[current_ - 1];
}

const Token &Parser::consume(TokenType type, std::string_view message) {
  if (check(type)) {
    return advance();
  }
  error_at(peek(), message);
  return peek();
}

bool Parser::is_type_start(TokenType type) const {
  switch (type) {
  case TokenType::AUTO:
  case TokenType::INT:
  case TokenType::FLOAT:
  case TokenType::DOUBLE:
  case TokenType::BOOL:
  case TokenType::STRING:
  case TokenType::VOID:
  case TokenType::BYTE:
  case TokenType::IDENTIFIER:
    return true;
  default:
    return false;
  }
}

bool Parser::is_declaration_start() const {
  if (check(TokenType::CONST) || check(TokenType::MUT)) {
    return true;
  }
  return is_type_start(peek().type) && check_next(TokenType::IDENTIFIER);
}

bool Parser::is_function_declaration_start() const {
  return is_type_start(peek().type) && check_next(TokenType::IDENTIFIER) &&
         check_after_next(TokenType::LEFT_PAREN);
}

ast::SourceLocation Parser::location_of(const Token &token) const {
  return ast::SourceLocation{
      .line = token.line,
      .column = token.column,
  };
}

std::string Parser::token_text(const Token &token) const {
  return std::string(token.lexeme);
}

std::string Parser::parse_type_name() {
  if (!is_type_start(peek().type)) {
    error_at(peek(), "Expected type name.");
    return "<error>";
  }
  return token_text(advance());
}

void Parser::synchronize() {
  advance();
  while (!is_at_end()) {
    if (previous().type == TokenType::SEMICOLON) {
      return;
    }
    switch (peek().type) {
    case TokenType::CONST:
    case TokenType::MUT:
    case TokenType::RETURN:
    case TokenType::IF:
    case TokenType::WHILE:
    case TokenType::IMPORT:
      return;
    default:
      break;
    }
    advance();
  }
}

void Parser::error_at(const Token &token, std::string_view message) {
  errors_.push_back(ParseError{
      .line = token.line,
      .column = token.column,
      .message = std::string(message),
  });
}

} // namespace kinglet
