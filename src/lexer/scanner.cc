#include "lexer/scanner.h"

#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kinglet {

namespace {

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

bool is_hex_digit(char c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool is_binary_digit(char c) {
  return c == '0' || c == '1';
}

bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_alpha_numeric(char c) {
  return is_alpha(c) || is_digit(c);
}

int escaped_char_value(char c) {
  switch (c) {
  case '0':
    return '\0';
  case 'n':
    return '\n';
  case 'r':
    return '\r';
  case 't':
    return '\t';
  case '\\':
    return '\\';
  case '\'':
    return '\'';
  case '"':
    return '"';
  default:
    return c;
  }
}

} // namespace

Scanner::Scanner(std::string source) : source_(std::move(source)) {}

std::vector<Token> Scanner::scan_tokens() {
  std::vector<Token> tokens;

  while (true) {
    Token token = scan_token();
    tokens.push_back(token);
    if (token.type == TokenType::END_OF_FILE) {
      break;
    }
  }

  return tokens;
}

bool Scanner::is_at_end() const {
  return current_ >= source_.size();
}

Token Scanner::scan_token() {
  skip_whitespace_and_comments();

  start_ = current_;
  token_line_ = line_;
  token_column_ = column_;

  if (is_at_end()) {
    return make_token(TokenType::END_OF_FILE);
  }

  const char c = advance();

  if (is_alpha(c)) {
    return identifier();
  }
  if (is_digit(c)) {
    return number();
  }

  switch (c) {
  case '(':
    return make_token(TokenType::LEFT_PAREN);
  case ')':
    return make_token(TokenType::RIGHT_PAREN);
  case '{':
    return make_token(TokenType::LEFT_BRACE);
  case '}':
    return make_token(TokenType::RIGHT_BRACE);
  case '[':
    return make_token(TokenType::LEFT_BRACKET);
  case ']':
    return make_token(TokenType::RIGHT_BRACKET);
  case ',':
    return make_token(TokenType::COMMA);
  case ';':
    return make_token(TokenType::SEMICOLON);
  case ':':
    return make_token(match(':') ? TokenType::COLON_COLON : TokenType::COLON);
  case '?':
    return make_token(TokenType::QUESTION);
  case '.':
    return make_token(match('.') ? TokenType::DOT_DOT : TokenType::DOT);
  case '+':
    return make_token(match('=') ? TokenType::PLUS_EQUAL : TokenType::PLUS);
  case '-':
    return make_token(match('>') ? TokenType::ARROW
                                 : match('=') ? TokenType::MINUS_EQUAL
                                              : TokenType::MINUS);
  case '*':
    return make_token(match('=') ? TokenType::STAR_EQUAL : TokenType::STAR);
  case '/':
    return make_token(match('=') ? TokenType::SLASH_EQUAL : TokenType::SLASH);
  case '%':
    return make_token(TokenType::PERCENT);
  case '=':
    return make_token(match('=') ? TokenType::EQUAL_EQUAL
                                 : match('>') ? TokenType::FAT_ARROW
                                              : TokenType::EQUAL);
  case '!':
    return make_token(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
  case '<':
    return make_token(match('=') ? TokenType::LESS_EQUAL
                                 : match('<') ? TokenType::LESS_LESS
                                              : TokenType::LESS);
  case '>':
    return make_token(match('=') ? TokenType::GREATER_EQUAL
                                 : match('>') ? TokenType::GREATER_GREATER
                                              : TokenType::GREATER);
  case '&':
    return make_token(match('&') ? TokenType::AMP_AMP : TokenType::AMP);
  case '|':
    return make_token(match('|') ? TokenType::PIPE_PIPE : TokenType::PIPE);
  case '^':
    return make_token(TokenType::CARET);
  case '~':
    return make_token(TokenType::TILDE);
  case '"':
    return string_literal();
  case '\'':
    return char_literal();
  default:
    return make_error("Unexpected character.");
  }
}

Token Scanner::make_token(TokenType type) const {
  Token token = {
      .type = type,
      .lexeme = std::string_view(source_).substr(start_, current_ - start_),
      .line = token_line_,
      .column = token_column_,
  };
  token.int_value = 0;
  return token;
}

Token Scanner::make_error(std::string_view message) const {
  Token token = {
      .type = TokenType::ERROR,
      .lexeme = message,
      .line = token_line_,
      .column = token_column_,
  };
  token.int_value = 0;
  return token;
}

void Scanner::skip_whitespace_and_comments() {
  while (!is_at_end()) {
    switch (peek()) {
    case ' ':
    case '\r':
    case '\t':
      advance();
      break;
    case '\n':
      advance();
      break;
    case '/':
      if (peek_next() == '/') {
        while (peek() != '\n' && !is_at_end()) {
          advance();
        }
      } else if (peek_next() == '*') {
        advance();
        advance();
        while (!is_at_end()) {
          if (peek() == '*' && peek_next() == '/') {
            advance();
            advance();
            break;
          }
          advance();
        }
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

Token Scanner::identifier() {
  while (is_alpha_numeric(peek())) {
    advance();
  }

  return make_token(identifier_type());
}

Token Scanner::number() {
  TokenType type = TokenType::INTEGER;

  if (source_[start_] == '0' && (peek() == 'x' || peek() == 'X')) {
    advance();
    if (!is_hex_digit(peek())) {
      return make_error("Expected hexadecimal digit after 0x.");
    }
    while (is_hex_digit(peek()) || peek() == '_') {
      advance();
    }

    Token token = make_token(TokenType::INTEGER);
    const std::string value = current_lexeme_without_separators();
    token.int_value = static_cast<int64_t>(std::strtoll(value.c_str(), nullptr, 16));
    return token;
  }

  if (source_[start_] == '0' && (peek() == 'b' || peek() == 'B')) {
    advance();
    if (!is_binary_digit(peek())) {
      return make_error("Expected binary digit after 0b.");
    }
    while (is_binary_digit(peek()) || peek() == '_') {
      advance();
    }

    Token token = make_token(TokenType::INTEGER);
    const std::string value = current_lexeme_without_separators();
    token.int_value = static_cast<int64_t>(std::strtoll(value.c_str() + 2, nullptr, 2));
    return token;
  }

  while (is_digit(peek()) || peek() == '_') {
    advance();
  }

  if (peek() == '.' && is_digit(peek_next())) {
    type = TokenType::FLOAT_LIT;
    advance();
    while (is_digit(peek()) || peek() == '_') {
      advance();
    }
  }

  if (peek() == 'e' || peek() == 'E') {
    type = TokenType::FLOAT_LIT;
    advance();
    if (peek() == '+' || peek() == '-') {
      advance();
    }
    if (!is_digit(peek())) {
      return make_error("Expected digit in exponent.");
    }
    while (is_digit(peek()) || peek() == '_') {
      advance();
    }
  }

  Token token = make_token(type);
  const std::string value = current_lexeme_without_separators();
  if (type == TokenType::FLOAT_LIT) {
    token.float_value = std::strtod(value.c_str(), nullptr);
  } else {
    token.int_value = static_cast<int64_t>(std::strtoll(value.c_str(), nullptr, 10));
  }
  return token;
}

Token Scanner::string_literal() {
  while (!is_at_end()) {
    if (peek() == '"') {
      advance();
      return make_token(TokenType::STRING_LIT);
    }

    if (peek() == '\\') {
      advance();
      if (!is_at_end()) {
        advance();
      }
      continue;
    }

    advance();
  }

  return make_error("Unterminated string literal.");
}

Token Scanner::char_literal() {
  if (is_at_end() || peek() == '\n') {
    return make_error("Unterminated character literal.");
  }

  int value = 0;
  if (peek() == '\\') {
    advance();
    if (is_at_end() || peek() == '\n') {
      return make_error("Unterminated character literal.");
    }
    value = escaped_char_value(advance());
  } else {
    value = advance();
  }

  if (peek() != '\'') {
    while (!is_at_end() && peek() != '\'' && peek() != '\n') {
      advance();
    }
    if (peek() == '\'') {
      advance();
    }
    return make_error("Character literal must contain exactly one character.");
  }

  advance();
  Token token = make_token(TokenType::CHAR_LIT);
  token.int_value = value;
  return token;
}

TokenType Scanner::identifier_type() const {
  static const std::unordered_map<std::string_view, TokenType> keywords = {
      {"auto", TokenType::AUTO},
      {"int", TokenType::INT},
      {"float", TokenType::FLOAT},
      {"double", TokenType::DOUBLE},
      {"bool", TokenType::BOOL},
      {"string", TokenType::STRING},
      {"void", TokenType::VOID},
      {"byte", TokenType::BYTE},
      {"const", TokenType::CONST},
      {"mut", TokenType::MUT},
      {"return", TokenType::RETURN},
      {"if", TokenType::IF},
      {"else", TokenType::ELSE},
      {"for", TokenType::FOR},
      {"while", TokenType::WHILE},
      {"break", TokenType::BREAK},
      {"continue", TokenType::CONTINUE},
      {"inspect", TokenType::INSPECT},
      {"when", TokenType::WHEN},
      {"import", TokenType::IMPORT},
      {"export", TokenType::EXPORT},
      {"namespace", TokenType::NAMESPACE},
      {"using", TokenType::USING},
      {"struct", TokenType::STRUCT},
      {"enum", TokenType::ENUM},
      {"trait", TokenType::TRAIT},
      {"spawn", TokenType::SPAWN},
      {"select", TokenType::SELECT},
      {"true", TokenType::TRUE},
      {"false", TokenType::FALSE},
      {"null", TokenType::NULL_},
  };

  const std::string_view text = std::string_view(source_).substr(start_, current_ - start_);
  const auto it = keywords.find(text);
  if (it == keywords.end()) {
    return TokenType::IDENTIFIER;
  }
  return it->second;
}

std::string Scanner::current_lexeme_without_separators() const {
  std::string result;
  result.reserve(current_ - start_);
  for (std::size_t i = start_; i < current_; ++i) {
    if (source_[i] != '_') {
      result.push_back(source_[i]);
    }
  }
  return result;
}

char Scanner::advance() {
  const char c = source_[current_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

bool Scanner::match(char expected) {
  if (is_at_end() || source_[current_] != expected) {
    return false;
  }

  advance();
  return true;
}

char Scanner::peek() const {
  if (is_at_end()) {
    return '\0';
  }
  return source_[current_];
}

char Scanner::peek_next() const {
  if (current_ + 1 >= source_.size()) {
    return '\0';
  }
  return source_[current_ + 1];
}

} // namespace kinglet
