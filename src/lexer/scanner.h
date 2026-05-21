#pragma once

#include "token.h"

#include <string>
#include <string_view>
#include <vector>

namespace kinglet {

class Scanner {
public:
  explicit Scanner(std::string source);

  std::vector<Token> scan_tokens();

private:
  bool is_at_end() const;

  Token scan_token();
  Token make_token(TokenType type) const;
  Token make_error(std::string_view message) const;

  void skip_whitespace_and_comments();

  Token identifier();
  Token number();
  Token string_literal();
  Token char_literal();

  TokenType identifier_type() const;
  std::string current_lexeme_without_separators() const;

  char advance();
  bool match(char expected);
  char peek() const;
  char peek_next() const;

  std::string source_;
  std::size_t start_ = 0;
  std::size_t current_ = 0;

  int line_ = 1;
  int column_ = 1;
  int token_line_ = 1;
  int token_column_ = 1;
};

} // namespace kinglet
