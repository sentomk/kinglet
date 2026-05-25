#pragma once

#include <cstdint>
#include <string_view>

namespace kinglet {

enum class TokenType {

  /* clang-format off */
  
  // Keywords
  AUTO, INT, FLOAT, DOUBLE, BOOL, STRING, VOID, BYTE, 
  CONST,
  RETURN, IF, ELSE, FOR, WHILE, BREAK, CONTINUE, GUARD,
  INSPECT, WHEN,
  IMPORT, EXPORT, NAMESPACE, USING,
  STRUCT, ENUM, TRAIT,
  SPAWN, SELECT,
  TRUE, FALSE, NULL_,

  // Operators
  PLUS, MINUS, STAR, SLASH, PERCENT,
  EQUAL, EQUAL_EQUAL, BANG_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL,
  AMP_AMP, PIPE_PIPE, BANG,
  AMP, PIPE, CARET, TILDE, LESS_LESS, GREATER_GREATER, PIPE_GREATER,
  ARROW, FAT_ARROW, QUESTION, COLON_COLON, DOT, DOT_DOT, DOT_DOT_DOT,
  PLUS_EQUAL, MINUS_EQUAL, STAR_EQUAL, SLASH_EQUAL,

  // Delimiters
  LEFT_PAREN, RIGHT_PAREN,
  LEFT_BRACE, RIGHT_BRACE,
  LEFT_BRACKET, RIGHT_BRACKET,
  COMMA, SEMICOLON, COLON,

  // Literals
  INTEGER, FLOAT_LIT, STRING_LIT, CHAR_LIT,

  // Identifier
  IDENTIFIER,

  // Special
  END_OF_FILE,
  ERROR

  /* clang-format on */

};

struct Token {
  TokenType type;
  std::string_view lexeme;
  int line;
  int column;

  // For literals
  union {
    int64_t int_value;
    double float_value;
  };
};

const char *token_type_name(TokenType type);

} // namespace kinglet
