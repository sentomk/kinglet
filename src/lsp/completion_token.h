#pragma once

#include "lexer/token.h"

#include <string>
#include <vector>

namespace kinglet::lsp {

struct CompletionTokenResult {
  std::vector<Token> tokens;
  std::size_t completion_index;
  std::string prefix;
};

CompletionTokenResult inject_completion_token(
    const std::string &source, int line, int character);

} // namespace kinglet::lsp
