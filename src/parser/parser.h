#pragma once

#include "ast/ast.h"
#include "lexer/token.h"
#include "lsp/completion_context.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kinglet {

struct ParseError {
  int line;
  int column;
  std::string message;
};

struct ParseResult {
  std::unique_ptr<ast::Program> program;
  std::vector<ParseError> errors;
};

class Parser {
public:
  explicit Parser(const std::vector<Token> &tokens);
  Parser(const std::vector<Token> &tokens, std::size_t completion_index);

  ParseResult parse();

  bool has_completion() const { return completion_result_.has_value(); }
  const lsp::CompletionInfo &completion_result() const { return *completion_result_; }

private:
  ast::DeclPtr declaration();
  ast::DeclPtr using_declaration();
  ast::DeclPtr import_declaration();
  ast::DeclPtr function_declaration();
  ast::DeclPtr struct_declaration();
  ast::DeclPtr enum_declaration();
  ast::DeclPtr impl_declaration();
  ast::DeclPtr trait_declaration();
  ast::StmtPtr statement();
  ast::StmtPtr block_statement();
  ast::StmtPtr return_statement();
  ast::StmtPtr if_statement();
  ast::StmtPtr while_statement();
  ast::StmtPtr guard_statement();
  ast::StmtPtr for_statement();
  ast::StmtPtr break_statement();
  ast::StmtPtr continue_statement();
  ast::StmtPtr var_declaration();
  ast::StmtPtr expression_statement();

  ast::ExprPtr expression();
  ast::ExprPtr assignment();
  ast::ExprPtr pipeline();
  ast::ExprPtr logical_or();
  ast::ExprPtr logical_and();
  ast::ExprPtr equality();
  ast::ExprPtr comparison();
  ast::ExprPtr term();
  ast::ExprPtr factor();
  ast::ExprPtr unary();
  ast::ExprPtr call();
  ast::ExprPtr primary();
  ast::ExprPtr match_expression(ast::ExprPtr value);
  ast::ExprPtr parse_array_pattern();

  ast::ExprPtr condition_expression();
  std::vector<ast::Parameter> parameters();
  ast::StmtPtr function_body();

  bool is_at_end() const;
  bool check(TokenType type) const;
  bool check_next(TokenType type) const;
  bool check_after_next(TokenType type) const;
  bool match(TokenType type);
  bool match_any(std::initializer_list<TokenType> types);
  const Token &advance();
  const Token &peek() const;
  const Token &previous() const;
  const Token &consume(TokenType type, std::string_view message);

  bool is_type_start(TokenType type) const;
  bool is_declaration_start() const;
  bool is_function_declaration_start() const;
  ast::SourceLocation location_of(const Token &token) const;
  std::string token_text(const Token &token) const;
  ast::TypeExpr parse_type_expr();
  void synchronize();
  void error_at(const Token &token, std::string_view message);

  bool at_completion() const;
  void set_completion(lsp::CompletionInfo info);

  const std::vector<Token> &tokens_;
  std::size_t current_ = 0;
  std::vector<ParseError> errors_;
  bool pending_greater_ = false;
  bool completion_mode_ = false;
  std::size_t completion_index_ = 0;
  std::optional<lsp::CompletionInfo> completion_result_;
};

} // namespace kinglet
