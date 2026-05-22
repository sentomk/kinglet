#pragma once

#include "lsp/json.h"

#include <string>
#include <string_view>
#include <vector>

namespace kinglet::lsp {

struct Diagnostic {
  int line;
  int column;
  std::string message;
};

struct TextDocument {
  std::string uri;
  std::string text;
};

class Server {
public:
  void run();

private:
  std::string read_message();
  void write_message(const json::Value &msg);
  void handle_message(const json::Value &msg);
  void send_diagnostics(const TextDocument &doc);
  void send_notification(const std::string &method, const json::Value &params);

  json::Value handle_initialize(const json::Value &params);
  void handle_did_open(const json::Value &params);
  void handle_did_change(const json::Value &params);

  std::vector<TextDocument> documents_;
};

} // namespace kinglet::lsp
