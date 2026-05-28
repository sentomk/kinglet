#include "lsp/completion_token.h"

#include "lexer/scanner.h"

namespace kinglet::lsp {

CompletionTokenResult inject_completion_token(
    const std::string &source, int line, int character) {
  Scanner scanner(source);
  auto tokens = scanner.scan_tokens();

  int cursor_line = line + 1;
  int cursor_col = character + 1;

  CompletionTokenResult result;
  result.completion_index = tokens.size();

  Token comp_token;
  comp_token.type = TokenType::COMPLETION;
  comp_token.lexeme = {};
  comp_token.line = cursor_line;
  comp_token.column = cursor_col;

  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto &tok = tokens[i];

    if (tok.type == TokenType::END_OF_FILE) {
      result.completion_index = result.tokens.size();
      result.tokens.push_back(comp_token);
      result.tokens.push_back(tok);
      break;
    }

    int tok_end_col = tok.column + static_cast<int>(tok.lexeme.size());

    if (tok.line == cursor_line && tok.column <= cursor_col &&
        tok_end_col >= cursor_col &&
        (tok.type == TokenType::IDENTIFIER ||
         tok.type == TokenType::STRING ||
         tok.type == TokenType::INT ||
         tok.type == TokenType::FLOAT ||
         tok.type == TokenType::DOUBLE ||
         tok.type == TokenType::BOOL ||
         tok.type == TokenType::VOID ||
         tok.type == TokenType::BYTE ||
         tok.type == TokenType::AUTO ||
         tok.type == TokenType::SELF)) {
      int prefix_len = cursor_col - tok.column;
      if (prefix_len > 0 && prefix_len <= static_cast<int>(tok.lexeme.size())) {
        result.prefix = std::string(tok.lexeme.substr(0, static_cast<std::size_t>(prefix_len)));
      }
      result.completion_index = result.tokens.size();
      result.tokens.push_back(comp_token);
      continue;
    }

    if (tok.line > cursor_line ||
        (tok.line == cursor_line && tok.column > cursor_col)) {
      if (result.completion_index == tokens.size()) {
        result.completion_index = result.tokens.size();
        result.tokens.push_back(comp_token);
      }
    }

    result.tokens.push_back(tok);
  }

  if (result.completion_index >= result.tokens.size()) {
    result.completion_index = result.tokens.size();
    result.tokens.push_back(comp_token);
    Token eof;
    eof.type = TokenType::END_OF_FILE;
    eof.lexeme = {};
    eof.line = cursor_line;
    eof.column = cursor_col;
    result.tokens.push_back(eof);
  }

  return result;
}

} // namespace kinglet::lsp
