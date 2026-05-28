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

Parser::Parser(const std::vector<Token> &tokens, std::size_t completion_index)
    : tokens_(tokens), completion_mode_(true), completion_index_(completion_index) {}

bool Parser::at_completion() const {
  return completion_mode_ && current_ == completion_index_;
}

void Parser::set_completion(lsp::CompletionInfo info) {
  completion_result_ = std::move(info);
}

ParseResult Parser::parse() {
  std::vector<ast::DeclPtr> declarations;
  while (!is_at_end() && !has_completion()) {
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
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::TopLevelDecl, {}, {}, {}, {}, {}, {}});
    return nullptr;
  }

  if (match(TokenType::USING)) {
    return using_declaration();
  }

  if (match(TokenType::IMPORT)) {
    return import_declaration();
  }

  bool is_public = match(TokenType::PUB);

  if (match(TokenType::STRUCT)) {
    auto decl = struct_declaration();
    if (decl) {
      auto *sd = static_cast<ast::StructDecl *>(decl.get());
      sd->is_public = is_public;
    }
    return decl;
  }

  if (match(TokenType::ENUM)) {
    auto decl = enum_declaration();
    if (decl) {
      auto *ed = static_cast<ast::EnumDecl *>(decl.get());
      ed->is_public = is_public;
    }
    return decl;
  }

  if (match(TokenType::IMPL)) {
    auto decl = impl_declaration();
    if (decl) {
      auto *id = static_cast<ast::ImplDecl *>(decl.get());
      id->is_public = is_public;
    }
    return decl;
  }

  if (match(TokenType::TRAIT)) {
    auto decl = trait_declaration();
    if (decl) {
      auto *td = static_cast<ast::TraitDecl *>(decl.get());
      td->is_public = is_public;
    }
    return decl;
  }

  if (is_function_declaration_start()) {
    auto decl = function_declaration();
    if (decl) {
      auto *fd = static_cast<ast::FunctionDecl *>(decl.get());
      fd->is_public = is_public;
    }
    return decl;
  }

  if (is_public) {
    error_at(peek(), "'pub' can only be used before function, struct, enum, impl, or trait declarations.");
  }

  ast::StmtPtr stmt = statement();
  if (!stmt) {
    synchronize();
    return nullptr;
  }
  const ast::SourceLocation location = stmt->location;
  return std::make_unique<ast::TopLevelStmtDecl>(location, std::move(stmt));
}


ast::DeclPtr Parser::using_declaration() {
  const Token &using_token = previous();
  bool is_namespace = false;
  if (match(TokenType::NAMESPACE)) {
    is_namespace = true;
  }
  const Token &name = consume(TokenType::IDENTIFIER, "Expected namespace name after 'using'.");
  consume(TokenType::SEMICOLON, "Expected ';' after using declaration.");
  return std::make_unique<ast::UsingDecl>(location_of(using_token), token_text(name), is_namespace);
}

ast::DeclPtr Parser::import_declaration() {
  const Token &import_token = previous();
  const Token &path_token = consume(TokenType::STRING_LIT, "Expected file path string after 'import'.");
  std::string path(token_text(path_token));
  if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
    path = path.substr(1, path.size() - 2);
  }

  std::string alias;
  std::vector<std::string> selected;

  if (check(TokenType::IDENTIFIER) && token_text(peek()) == "as") {
    advance(); // consume 'as'
    const Token &alias_token = consume(TokenType::IDENTIFIER, "Expected alias name after 'as'.");
    alias = token_text(alias_token);
  } else if (match(TokenType::LEFT_BRACE)) {
    do {
      const Token &sym = consume(TokenType::IDENTIFIER, "Expected symbol name in import list.");
      selected.push_back(std::string(token_text(sym)));
    } while (match(TokenType::COMMA));
    consume(TokenType::RIGHT_BRACE, "Expected '}' after import list.");
  }

  match(TokenType::SEMICOLON);
  return std::make_unique<ast::ImportDecl>(location_of(import_token), std::move(path),
                                           std::move(alias), std::move(selected));
}

ast::DeclPtr Parser::struct_declaration() {
  const Token &struct_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected struct name.");

  std::vector<std::string> type_params;
  if (match(TokenType::LESS)) {
    do {
      const Token &param = consume(TokenType::IDENTIFIER, "Expected type parameter name.");
      type_params.push_back(token_text(param));
    } while (match(TokenType::COMMA));
    consume(TokenType::GREATER, "Expected '>' after type parameters.");
  }

  consume(TokenType::LEFT_BRACE, "Expected '{' after struct name.");

  std::vector<ast::FieldDef> fields;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::StructFieldDecl, {}, {}, {}, {}, {}, {}});
      return nullptr;
    }
    size_t start_pos = current_;
    ast::TypeExpr type = parse_type_expr();
    if (has_completion()) return nullptr;
    const Token &field_name = consume(TokenType::IDENTIFIER, "Expected field name.");
    consume(TokenType::SEMICOLON, "Expected ';' after field declaration.");
    fields.push_back(ast::FieldDef{std::move(type), token_text(field_name)});
    if (current_ == start_pos) {
      advance();
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after struct body.");

  return std::make_unique<ast::StructDecl>(location_of(struct_token), token_text(name),
                                           std::move(type_params), std::move(fields));
}

ast::DeclPtr Parser::enum_declaration() {
  const Token &enum_token = previous();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected enum name.");
  consume(TokenType::LEFT_BRACE, "Expected '{' after enum name.");

  std::vector<ast::EnumVariantDecl> variants;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::EnumVariant, {}, {}, {}, {}, {}, {}});
      return nullptr;
    }
    size_t start_pos = current_;
    const Token &variant = consume(TokenType::IDENTIFIER, "Expected variant name.");
    ast::EnumVariantDecl decl;
    decl.name = token_text(variant);
    if (match(TokenType::LEFT_PAREN)) {
      while (!check(TokenType::RIGHT_PAREN) && !is_at_end()) {
        size_t inner_pos = current_;
        decl.param_types.push_back(parse_type_expr());
        if (!check(TokenType::RIGHT_PAREN)) {
          consume(TokenType::COMMA, "Expected ',' between variant parameter types.");
        }
        if (current_ == inner_pos) {
          advance();
        }
      }
      consume(TokenType::RIGHT_PAREN, "Expected ')' after variant parameters.");
    }
    variants.push_back(std::move(decl));
    if (!check(TokenType::RIGHT_BRACE)) {
      consume(TokenType::COMMA, "Expected ',' between enum variants.");
    }
    if (current_ == start_pos) {
      advance();
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after enum body.");

  return std::make_unique<ast::EnumDecl>(location_of(enum_token), token_text(name),
                                         std::move(variants));
}

ast::DeclPtr Parser::impl_declaration() {
  const Token &impl_token = previous();
  const Token &type_name = consume(TokenType::IDENTIFIER, "Expected type name after 'impl'.");
  std::string target_type = token_text(type_name);

  std::string trait_name;
  if (match(TokenType::COLON)) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::TraitName, target_type, {}, {}, {}, {}, {}});
      return nullptr;
    }
    const Token &trait_tok = consume(TokenType::IDENTIFIER, "Expected trait name after ':'.");
    trait_name = token_text(trait_tok);
  }

  consume(TokenType::LEFT_BRACE, "Expected '{' after impl header.");

  std::vector<std::unique_ptr<ast::FunctionDecl>> methods;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::ImplMethodDecl, target_type, trait_name, {}, {}, {}, {}});
      return nullptr;
    }
    size_t start_pos = current_;
    ast::TypeExpr return_type = parse_type_expr();
    if (has_completion()) return nullptr;
    const Token &method_name = consume(TokenType::IDENTIFIER, "Expected method name.");
    consume(TokenType::LEFT_PAREN, "Expected '(' after method name.");

    std::vector<ast::Parameter> params;
    if (match(TokenType::SELF)) {
      params.push_back(ast::Parameter{ast::TypeExpr{target_type, {}}, "self"});
      if (check(TokenType::COMMA)) advance();
    }
    if (!check(TokenType::RIGHT_PAREN)) {
      auto rest = parameters();
      params.insert(params.end(), std::make_move_iterator(rest.begin()),
                    std::make_move_iterator(rest.end()));
    }
    consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.");

    ast::StmtPtr body = function_body();
    auto method = std::make_unique<ast::FunctionDecl>(
        location_of(method_name), std::move(return_type), token_text(method_name),
        std::vector<std::string>{}, std::move(params), std::move(body));
    methods.push_back(std::move(method));

    if (current_ == start_pos) {
      advance();
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after impl body.");

  return std::make_unique<ast::ImplDecl>(location_of(impl_token), std::move(target_type),
                                         std::move(trait_name), std::move(methods));
}

ast::DeclPtr Parser::trait_declaration() {
  const Token &trait_token = previous();
  const Token &name_token = consume(TokenType::IDENTIFIER, "Expected trait name after 'trait'.");
  std::string name = token_text(name_token);

  consume(TokenType::LEFT_BRACE, "Expected '{' after trait name.");

  std::vector<ast::TraitMethodDecl> methods;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    if (at_completion()) {
      set_completion({lsp::CompletionPosition::TraitMethodDecl, token_text(name_token), {}, {}, {}, {}, {}});
      return nullptr;
    }
    size_t start_pos = current_;
    ast::TypeExpr return_type = parse_type_expr();
    if (has_completion()) return nullptr;
    const Token &method_name = consume(TokenType::IDENTIFIER, "Expected method name.");
    consume(TokenType::LEFT_PAREN, "Expected '(' after method name.");

    std::vector<ast::Parameter> params;
    if (match(TokenType::SELF)) {
      params.push_back(ast::Parameter{ast::TypeExpr{"Self", {}}, "self"});
      if (check(TokenType::COMMA)) advance();
    }
    if (!check(TokenType::RIGHT_PAREN)) {
      auto rest = parameters();
      params.insert(params.end(), std::make_move_iterator(rest.begin()),
                    std::make_move_iterator(rest.end()));
    }
    consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.");

    ast::StmtPtr default_body;
    if (check(TokenType::LEFT_BRACE) || check(TokenType::FAT_ARROW)) {
      default_body = function_body();
    } else {
      consume(TokenType::SEMICOLON, "Expected ';' or '{' after method signature.");
    }

    methods.push_back(ast::TraitMethodDecl{
        std::move(return_type), token_text(method_name), std::move(params),
        std::move(default_body)});

    if (current_ == start_pos) {
      advance();
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after trait body.");

  return std::make_unique<ast::TraitDecl>(location_of(trait_token), std::move(name),
                                          std::move(methods));
}

ast::DeclPtr Parser::function_declaration() {
  const Token &return_type_token = peek();
  ast::TypeExpr return_type = parse_type_expr();
  const Token &name = consume(TokenType::IDENTIFIER, "Expected function name.");

  std::vector<std::string> type_params;
  if (match(TokenType::LESS)) {
    do {
      const Token &param = consume(TokenType::IDENTIFIER, "Expected type parameter name.");
      type_params.push_back(token_text(param));
    } while (match(TokenType::COMMA));
    consume(TokenType::GREATER, "Expected '>' after type parameters.");
  }

  consume(TokenType::LEFT_PAREN, "Expected '(' after function name.");
  std::vector<ast::Parameter> params = parameters();
  consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.");
  ast::StmtPtr body = function_body();
  return std::make_unique<ast::FunctionDecl>(location_of(return_type_token), std::move(return_type),
                                             token_text(name), std::move(type_params),
                                             std::move(params), std::move(body));
}

ast::StmtPtr Parser::statement() {
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::Statement, {}, {}, {}, {}, {}, {}});
    return nullptr;
  }
  if (match(TokenType::LEFT_BRACE)) {
    return block_statement();
  }
  if (match(TokenType::RETURN)) {
    return return_statement();
  }
  if (match(TokenType::IF)) {
    return if_statement();
  }
  if (match(TokenType::GUARD)) {
    return guard_statement();
  }
  if (match(TokenType::WHILE)) {
    return while_statement();
  }
  if (match(TokenType::FOR)) {
    return for_statement();
  }
  if (match(TokenType::BREAK)) {
    return break_statement();
  }
  if (match(TokenType::CONTINUE)) {
    return continue_statement();
  }
  if (is_declaration_start()) {
    return var_declaration();
  }
  return expression_statement();
}

ast::StmtPtr Parser::block_statement() {
  const Token &left_brace = previous();
  std::vector<ast::StmtPtr> statements;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end() && !has_completion()) {
    statements.push_back(statement());
  }
  if (has_completion()) return nullptr;
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

ast::StmtPtr Parser::guard_statement() {
  const Token &guard_token = previous();
  ast::ExprPtr condition = expression();
  consume(TokenType::ELSE, "Expected 'else' after guard condition.");
  consume(TokenType::LEFT_BRACE, "Expected '{' after 'else' in guard statement.");
  ast::StmtPtr else_body = block_statement();
  return std::make_unique<ast::GuardStmt>(location_of(guard_token), std::move(condition),
                                          std::move(else_body));
}

ast::StmtPtr Parser::for_statement() {
  const Token &for_token = previous();
  consume(TokenType::LEFT_PAREN, "Expected '(' after 'for'.");

  // Parse init: either a variable declaration, an expression statement, or empty
  ast::StmtPtr init;
  if (is_declaration_start()) {
    init = var_declaration();
    // var_declaration already consumes its own trailing semicolon
  } else if (!check(TokenType::SEMICOLON)) {
    ast::ExprPtr expr = expression();
    init = std::make_unique<ast::ExprStmt>(expr->location, std::move(expr));
    consume(TokenType::SEMICOLON, "Expected ';' after for init.");
  } else {
    consume(TokenType::SEMICOLON, "Expected ';' after for init.");
  }

  // Parse condition
  ast::ExprPtr condition;
  if (!check(TokenType::SEMICOLON)) {
    condition = expression();
  }
  consume(TokenType::SEMICOLON, "Expected ';' after for condition.");

  // Parse step: expression without consuming trailing semicolon (the ')' closes the header)
  ast::StmtPtr step;
  if (!check(TokenType::RIGHT_PAREN)) {
    ast::ExprPtr step_expr = expression();
    step = std::make_unique<ast::ExprStmt>(step_expr->location, std::move(step_expr));
  }
  consume(TokenType::RIGHT_PAREN, "Expected ')' after for clauses.");

  ast::StmtPtr body = statement();
  return std::make_unique<ast::ForStmt>(location_of(for_token), std::move(init),
                                        std::move(condition), std::move(step), std::move(body));
}

ast::StmtPtr Parser::break_statement() {
  const Token &break_token = previous();
  consume(TokenType::SEMICOLON, "Expected ';' after 'break'.");
  return std::make_unique<ast::BreakStmt>(location_of(break_token));
}

ast::StmtPtr Parser::continue_statement() {
  const Token &continue_token = previous();
  consume(TokenType::SEMICOLON, "Expected ';' after 'continue'.");
  return std::make_unique<ast::ContinueStmt>(location_of(continue_token));
}

ast::StmtPtr Parser::var_declaration() {
  const Token &start_token = peek();
  std::string storage;
  if (match(TokenType::CONST)) {
    storage = token_text(previous());
  }

  ast::TypeExpr type;
  Token name = peek();
  bool has_type = false;

  if (is_type_start(peek().type)) {
    size_t pos = current_ + 1;
    if (pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
        peek().type == TokenType::AUTO) {
      advance(); // consume type token (auto/int/etc)
      advance(); // consume '['
      std::vector<std::string> names;
      std::string rest_name;
      while (!check(TokenType::RIGHT_BRACKET) && !is_at_end()) {
        if (match(TokenType::DOT_DOT_DOT)) {
          Token rest = consume(TokenType::IDENTIFIER, "Expected name after '...'.");
          rest_name = token_text(rest);
        } else {
          Token n = consume(TokenType::IDENTIFIER, "Expected variable name in destructuring.");
          names.push_back(token_text(n));
        }
        if (!check(TokenType::RIGHT_BRACKET)) {
          consume(TokenType::COMMA, "Expected ',' between names.");
        }
      }
      consume(TokenType::RIGHT_BRACKET, "Expected ']' after destructuring names.");
      consume(TokenType::EQUAL, "Expected '=' after destructuring pattern.");
      ast::ExprPtr init = expression();
      consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
      return std::make_unique<ast::UnpackDeclStmt>(location_of(start_token), std::move(names),
                                                   std::move(rest_name), std::move(init));
    }
  }
  if (is_type_start(peek().type)) {
    size_t pos = current_ + 1;
    if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
      int depth = 1;
      ++pos;
      while (pos < tokens_.size() && depth > 0) {
        if (tokens_[pos].type == TokenType::LESS) ++depth;
        else if (tokens_[pos].type == TokenType::GREATER) --depth;
        else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
        ++pos;
      }
      while (pos + 1 < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
             tokens_[pos + 1].type == TokenType::RIGHT_BRACKET) {
        pos += 2;
      }
      has_type = pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
    } else {
      while (pos + 1 < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
             tokens_[pos + 1].type == TokenType::RIGHT_BRACKET) {
        pos += 2;
      }
      has_type = pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
    }
  }
  if (has_type) {
    type = parse_type_expr();
    if (pending_greater_) pending_greater_ = false;
    name = consume(TokenType::IDENTIFIER, "Expected variable name.");
  } else {
    name = consume(TokenType::IDENTIFIER, "Expected variable name.");
  }

  ast::ExprPtr init;
  if (match(TokenType::EQUAL)) {
    init = expression();
  } else if (!type.name.empty() && check(TokenType::LEFT_BRACE)) {
    advance(); // consume '{'
    std::vector<ast::StructLiteralExpr::FieldInit> fields;
    while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
      ast::ExprPtr value = expression();
      fields.push_back(ast::StructLiteralExpr::FieldInit{"", std::move(value)});
      if (!check(TokenType::RIGHT_BRACE)) {
        consume(TokenType::COMMA, "Expected ',' between struct fields.");
      }
    }
    consume(TokenType::RIGHT_BRACE, "Expected '}' after struct literal.");
    init = std::make_unique<ast::StructLiteralExpr>(
        location_of(start_token), type, std::move(fields));
  }
  consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.");
  return std::make_unique<ast::VarDeclStmt>(location_of(start_token), std::move(storage), std::move(type),
                                            token_text(name), std::move(init));
}

ast::StmtPtr Parser::expression_statement() {
  ast::ExprPtr expr = expression();
  if (has_completion()) return nullptr;
  const ast::SourceLocation location = expr->location;
  consume(TokenType::SEMICOLON, "Expected ';' after expression.");
  return std::make_unique<ast::ExprStmt>(location, std::move(expr));
}

ast::ExprPtr Parser::expression() {
  return assignment();
}

ast::ExprPtr Parser::assignment() {
  ast::ExprPtr expr = pipeline();
  if (has_completion()) return expr;
  if (is_assignment_operator(peek().type)) {
    const Token &op = advance();
    ast::ExprPtr value = assignment();
    auto *identifier = dynamic_cast<ast::IdentifierExpr *>(expr.get());
    if (identifier != nullptr) {
      const ast::SourceLocation location = expr->location;
      return std::make_unique<ast::AssignExpr>(location, identifier->name,
                                               token_to_assign_op(op.type), std::move(value));
    }
    auto *index_expr = dynamic_cast<ast::IndexExpr *>(expr.get());
    if (index_expr != nullptr) {
      if (op.type != TokenType::EQUAL) {
        error_at(op, "Compound assignment to indexed values is not supported.");
        return value;
      }
      const ast::SourceLocation location = expr->location;
      return std::make_unique<ast::IndexAssignExpr>(location, std::move(index_expr->object),
                                                    std::move(index_expr->index),
                                                    std::move(value));
    }
    auto *field_access = dynamic_cast<ast::FieldAccessExpr *>(expr.get());
    if (field_access != nullptr) {
      const ast::SourceLocation location = expr->location;
      return std::make_unique<ast::FieldAssignExpr>(location, std::move(field_access->object),
                                                    field_access->field_name, std::move(value));
    }
    error_at(op, "Invalid assignment target.");
    return value;
  }
  return expr;
}

ast::ExprPtr Parser::pipeline() {
  ast::ExprPtr expr = logical_or();
  while (match(TokenType::PIPE_GREATER)) {
    auto loc = location_of(previous());
    ast::ExprPtr func = logical_or();
    std::vector<ast::ExprPtr> args;
    args.push_back(std::move(expr));
    std::vector<ast::TypeExpr> type_args;
    expr = std::make_unique<ast::CallExpr>(loc, std::move(func), std::move(type_args), std::move(args));
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

  auto is_comparison_op = [this]() {
    return check(TokenType::LESS) || check(TokenType::LESS_EQUAL) ||
           check(TokenType::GREATER) || check(TokenType::GREATER_EQUAL);
  };

  if (!is_comparison_op()) {
    return expr;
  }

  advance();
  auto op1_loc = location_of(previous());
  ast::BinaryOp bin_op1 = token_to_binary_op(previous().type);

  std::size_t mid_start = current_;
  ast::ExprPtr mid = term();

  if (!is_comparison_op()) {
    return std::make_unique<ast::BinaryExpr>(op1_loc, std::move(expr), bin_op1, std::move(mid));
  }

  std::size_t saved = current_;
  current_ = mid_start;
  ast::ExprPtr mid_copy = term();
  current_ = saved;

  ast::ExprPtr left_cmp = std::make_unique<ast::BinaryExpr>(op1_loc, std::move(expr), bin_op1, std::move(mid));

  advance();
  auto op2_loc = location_of(previous());
  ast::BinaryOp bin_op2 = token_to_binary_op(previous().type);
  ast::ExprPtr right_term = term();

  ast::ExprPtr right_cmp = std::make_unique<ast::BinaryExpr>(op2_loc, std::move(mid_copy), bin_op2, std::move(right_term));

  return std::make_unique<ast::BinaryExpr>(op1_loc, std::move(left_cmp), ast::BinaryOp::And, std::move(right_cmp));
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
  if (has_completion()) return expr;
  while (true) {
    if (check(TokenType::LESS) && dynamic_cast<const ast::IdentifierExpr *>(expr.get())) {
      size_t saved = current_;
      size_t pos = current_ + 1;
      int depth = 1;
      bool valid = false;
      while (pos < tokens_.size() && depth > 0) {
        if (tokens_[pos].type == TokenType::LESS) ++depth;
        else if (tokens_[pos].type == TokenType::GREATER) --depth;
        else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
        else if (tokens_[pos].type == TokenType::SEMICOLON || tokens_[pos].type == TokenType::END_OF_FILE) break;
        ++pos;
      }
      if (depth == 0 && pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_PAREN) {
        advance();
        std::vector<ast::TypeExpr> type_args;
        do {
          type_args.push_back(parse_type_expr());
        } while (match(TokenType::COMMA));
        if (pending_greater_) {
          pending_greater_ = false;
        } else {
          consume(TokenType::GREATER, "Expected '>' after type arguments.");
        }
        consume(TokenType::LEFT_PAREN, "Expected '(' after type arguments.");
        std::vector<ast::ExprPtr> args;
        if (!check(TokenType::RIGHT_PAREN)) {
          do {
            args.push_back(expression());
          } while (match(TokenType::COMMA));
        }
        consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");
        const ast::SourceLocation location = expr->location;
        expr = std::make_unique<ast::CallExpr>(location, std::move(expr),
                                               std::move(type_args), std::move(args));
        valid = true;
      }
      if (!valid) {
        current_ = saved;
      }
      if (valid) continue;
    }
    if (match(TokenType::LEFT_PAREN)) {
      std::vector<ast::ExprPtr> args;
      if (!check(TokenType::RIGHT_PAREN)) {
        do {
          args.push_back(expression());
        } while (match(TokenType::COMMA));
      }
      consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments.");
      const ast::SourceLocation location = expr->location;
      expr = std::make_unique<ast::CallExpr>(location, std::move(expr),
                                             std::vector<ast::TypeExpr>{}, std::move(args));
    } else if (match(TokenType::DOT)) {
      if (at_completion()) {
        set_completion({lsp::CompletionPosition::FieldAccess, {}, {}, {}, {}, {}, {}});
        return expr;
      }
      const Token &field = consume(TokenType::IDENTIFIER, "Expected field name after '.'.");
      const ast::SourceLocation location = expr->location;
      expr = std::make_unique<ast::FieldAccessExpr>(location, std::move(expr), token_text(field));
    } else if (match(TokenType::LEFT_BRACKET)) {
      ast::ExprPtr index = expression();
      consume(TokenType::RIGHT_BRACKET, "Expected ']' after index.");
      const ast::SourceLocation location = expr->location;
      expr = std::make_unique<ast::IndexExpr>(location, std::move(expr), std::move(index));
    } else if (match(TokenType::MATCH)) {
      expr = match_expression(std::move(expr));
    } else {
      break;
    }
  }
  return expr;
}

ast::ExprPtr Parser::primary() {
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::ExpressionStart, {}, {}, {}, {}, {}, {}});
    return nullptr;
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
    std::string text = token_text(literal);
    // Strip surrounding quotes
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
      text = text.substr(1, text.size() - 2);
    }
    return std::make_unique<ast::StringLiteralExpr>(location_of(literal), std::move(text));
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
  if (match(TokenType::LEFT_BRACKET)) {
    const Token &left_bracket = previous();
    std::vector<ast::ExprPtr> elements;
    if (!check(TokenType::RIGHT_BRACKET)) {
      do {
        elements.push_back(expression());
      } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACKET, "Expected ']' after array literal.");
    return std::make_unique<ast::ArrayLiteralExpr>(location_of(left_bracket),
                                                   std::move(elements));
  }
  if (match(TokenType::SELF)) {
    return std::make_unique<ast::IdentifierExpr>(location_of(previous()), "self");
  }
  if (match(TokenType::IDENTIFIER)) {
    const Token &identifier = previous();
    if (match(TokenType::COLON_COLON)) {
      const Token &member =
          consume(TokenType::IDENTIFIER, "Expected member name after '::'.");
      return std::make_unique<ast::NamespaceAccessExpr>(
          location_of(identifier), token_text(identifier), token_text(member));
    }
    if (check(TokenType::LEFT_BRACE) && current_ + 1 < tokens_.size() &&
        tokens_[current_ + 1].type != TokenType::RIGHT_BRACE &&
        !token_text(identifier).empty() && std::isupper(token_text(identifier)[0])) {
      advance(); // consume '{'
      std::vector<ast::StructLiteralExpr::FieldInit> fields;
      while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
        ast::ExprPtr value = expression();
        fields.push_back(ast::StructLiteralExpr::FieldInit{"", std::move(value)});
        if (!check(TokenType::RIGHT_BRACE)) {
          consume(TokenType::COMMA, "Expected ',' between struct fields.");
        }
      }
      consume(TokenType::RIGHT_BRACE, "Expected '}' after struct literal.");
      return std::make_unique<ast::StructLiteralExpr>(location_of(identifier),
                                                      ast::TypeExpr{token_text(identifier), {}},
                                                      std::move(fields));
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

ast::ExprPtr Parser::parse_array_pattern() {
  const Token &bracket = previous();
  std::vector<ast::ExprPtr> elements;
  while (!check(TokenType::RIGHT_BRACKET) && !is_at_end()) {
    if (match(TokenType::LET)) {
      const Token &name_token = consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
      elements.push_back(std::make_unique<ast::BindingPattern>(location_of(name_token), std::string(name_token.lexeme)));
    } else {
      elements.push_back(expression());
    }
    if (!check(TokenType::RIGHT_BRACKET)) {
      consume(TokenType::COMMA, "Expected ',' between array pattern elements.");
    }
  }
  consume(TokenType::RIGHT_BRACKET, "Expected ']' after array pattern.");
  return std::make_unique<ast::ArrayPattern>(location_of(bracket), std::move(elements));
}

ast::ExprPtr Parser::match_expression(ast::ExprPtr value) {
  const Token &match_token = previous();
  consume(TokenType::LEFT_BRACE, "Expected '{' after 'match'.");

  std::vector<ast::MatchArm> arms;
  while (!check(TokenType::RIGHT_BRACE) && !is_at_end()) {
    ast::ExprPtr pattern;
    if (match(TokenType::LET)) {
      const Token &name_token = consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
      pattern = std::make_unique<ast::BindingPattern>(location_of(name_token), std::string(name_token.lexeme));
    } else if (match(TokenType::LEFT_BRACKET)) {
      pattern = parse_array_pattern();
    } else if (check(TokenType::IDENTIFIER) && current_ + 1 < tokens_.size() &&
               tokens_[current_ + 1].type == TokenType::COLON_COLON) {
      const Token &enum_token = advance();
      std::string enum_name(token_text(enum_token));
      advance(); // consume '::'
      const Token &variant_token = consume(TokenType::IDENTIFIER, "Expected variant name after '::'.");
      std::string variant_name(token_text(variant_token));
      std::vector<ast::ExprPtr> fields;
      if (match(TokenType::LEFT_PAREN)) {
        while (!check(TokenType::RIGHT_PAREN) && !is_at_end()) {
          if (match(TokenType::LET)) {
            const Token &name_tok = consume(TokenType::IDENTIFIER, "Expected variable name after 'let'.");
            fields.push_back(std::make_unique<ast::BindingPattern>(location_of(name_tok), std::string(name_tok.lexeme)));
          } else {
            fields.push_back(expression());
          }
          if (!check(TokenType::RIGHT_PAREN)) {
            consume(TokenType::COMMA, "Expected ',' between enum pattern fields.");
          }
        }
        consume(TokenType::RIGHT_PAREN, "Expected ')' after enum pattern fields.");
      }
      pattern = std::make_unique<ast::EnumPattern>(location_of(enum_token), std::move(enum_name), std::move(variant_name), std::move(fields));
    } else {
      pattern = expression();
    }
    ast::ExprPtr guard;
    if (match(TokenType::IF)) {
      consume(TokenType::LEFT_PAREN, "Expected '(' after 'if' in guard.");
      guard = expression();
      consume(TokenType::RIGHT_PAREN, "Expected ')' after guard condition.");
    }
    consume(TokenType::FAT_ARROW, "Expected '=>' after match pattern.");
    ast::ExprPtr body = expression();
    arms.push_back(ast::MatchArm{std::move(pattern), std::move(guard), std::move(body)});
    if (!check(TokenType::RIGHT_BRACE)) {
      consume(TokenType::COMMA, "Expected ',' after match arm.");
    }
  }
  consume(TokenType::RIGHT_BRACE, "Expected '}' after match arms.");
  return std::make_unique<ast::MatchExpr>(location_of(match_token), std::move(value),
                                          std::move(arms));
}

std::vector<ast::Parameter> Parser::parameters() {
  std::vector<ast::Parameter> params;
  if (check(TokenType::RIGHT_PAREN)) {
    return params;
  }

  do {
    ast::TypeExpr type = parse_type_expr();
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
    if (match(TokenType::LEFT_BRACE)) {
      return block_statement();
    }
    ast::ExprPtr value = expression();
    if (has_completion()) return nullptr;
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
  if (type == TokenType::GREATER && pending_greater_) return true;
  return !is_at_end() && peek().type == type;
}

bool Parser::check_next(TokenType type) const {
  if (pending_greater_) {
    if (type == TokenType::GREATER) return true;
    return current_ < tokens_.size() && tokens_[current_].type == type;
  }
  return current_ + 1 < tokens_.size() && tokens_[current_ + 1].type == type;
}

bool Parser::check_after_next(TokenType type) const {
  return current_ + 2 < tokens_.size() && tokens_[current_ + 2].type == type;
}

bool Parser::match(TokenType type) {
  if (type == TokenType::GREATER && pending_greater_) {
    pending_greater_ = false;
    return true;
  }
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
  if (check(TokenType::CONST)) {
    return true;
  }
  if (!is_type_start(peek().type)) return false;
  size_t pos = current_ + 1;
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
      peek().type == TokenType::AUTO) {
    return true;
  }
  // Skip balanced <...> to find the identifier after the type
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
    int depth = 1;
    ++pos;
    while (pos < tokens_.size() && depth > 0) {
      if (tokens_[pos].type == TokenType::LESS) ++depth;
      else if (tokens_[pos].type == TokenType::GREATER) --depth;
      else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
      ++pos;
    }
  }
  while (pos + 1 < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
         tokens_[pos + 1].type == TokenType::RIGHT_BRACKET) {
    pos += 2;
  }
  return pos < tokens_.size() && tokens_[pos].type == TokenType::IDENTIFIER;
}

bool Parser::is_function_declaration_start() const {
  if (!is_type_start(peek().type)) return false;
  // Skip balanced <...> after the type name to find IDENTIFIER then '(' or '<'
  size_t pos = current_ + 1;
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
    int depth = 1;
    ++pos;
    while (pos < tokens_.size() && depth > 0) {
      if (tokens_[pos].type == TokenType::LESS) ++depth;
      else if (tokens_[pos].type == TokenType::GREATER) --depth;
      else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
      ++pos;
    }
  }
  while (pos + 1 < tokens_.size() && tokens_[pos].type == TokenType::LEFT_BRACKET &&
         tokens_[pos + 1].type == TokenType::RIGHT_BRACKET) {
    pos += 2;
  }
  if (pos >= tokens_.size() || tokens_[pos].type != TokenType::IDENTIFIER) return false;
  ++pos; // skip function name
  // Function may have type params: name<T>(...)
  if (pos < tokens_.size() && tokens_[pos].type == TokenType::LESS) {
    int depth = 1;
    ++pos;
    while (pos < tokens_.size() && depth > 0) {
      if (tokens_[pos].type == TokenType::LESS) ++depth;
      else if (tokens_[pos].type == TokenType::GREATER) --depth;
      else if (tokens_[pos].type == TokenType::GREATER_GREATER) depth -= 2;
      ++pos;
    }
  }
  return pos < tokens_.size() && tokens_[pos].type == TokenType::LEFT_PAREN;
}

ast::SourceLocation Parser::location_of(const Token &token) const {
  return ast::SourceLocation{
      .line = token.line,
      .column = token.column,
      .length = static_cast<int>(token.lexeme.size()),
  };
}

std::string Parser::token_text(const Token &token) const {
  return std::string(token.lexeme);
}

ast::TypeExpr Parser::parse_type_expr() {
  if (at_completion()) {
    set_completion({lsp::CompletionPosition::TypeExpr, {}, {}, {}, {}, {}, {}});
    return ast::TypeExpr{"<error>", {}};
  }
  if (!is_type_start(peek().type)) {
    error_at(peek(), "Expected type name.");
    return ast::TypeExpr{"<error>", {}};
  }
  std::string name = token_text(advance());
  std::vector<ast::TypeExpr> type_args;
  if (check(TokenType::LESS) && !pending_greater_) {
    advance(); // consume '<'
    do {
      type_args.push_back(parse_type_expr());
    } while (match(TokenType::COMMA));
    if (peek().type == TokenType::GREATER_GREATER && !pending_greater_) {
      advance(); // consume '>>'
      pending_greater_ = true;
    } else {
      if (!match(TokenType::GREATER)) {
        error_at(peek(), "Expected '>' after type arguments.");
      }
    }
  }
  while (match(TokenType::LEFT_BRACKET)) {
    consume(TokenType::RIGHT_BRACKET, "Expected ']' after array type suffix.");
    std::vector<ast::TypeExpr> array_arg;
    array_arg.push_back(ast::TypeExpr{std::move(name), std::move(type_args)});
    name = "Array";
    type_args = std::move(array_arg);
  }
  return ast::TypeExpr{std::move(name), std::move(type_args)};
}

void Parser::synchronize() {
  advance();
  while (!is_at_end()) {
    if (previous().type == TokenType::SEMICOLON) {
      return;
    }
    switch (peek().type) {
    case TokenType::CONST:
    case TokenType::RETURN:
    case TokenType::IF:
    case TokenType::FOR:
    case TokenType::BREAK:
    case TokenType::CONTINUE:
    case TokenType::WHILE:
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
