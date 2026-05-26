#pragma once

#include "lsp/document_store.h"
#include "lsp/json.h"
#include "lsp/transport.h"

#include <string>

namespace kinglet::lsp {

class Server {
public:
  void run();

private:
  void handle_message(const json::Value &msg);
  void send_notification(const std::string &method, const json::Value &params);
  void send_response(const json::Value &id, const json::Value &result);

  json::Value handle_initialize(const json::Value &params);
  void handle_did_open(const json::Value &params);
  void handle_did_change(const json::Value &params);
  void handle_did_close(const json::Value &params);
  json::Value handle_completion(const json::Value &params);
  json::Value handle_definition(const json::Value &params);
  json::Value handle_hover(const json::Value &params);
  json::Value handle_document_symbol(const json::Value &params);
  json::Value handle_signature_help(const json::Value &params);
  json::Value handle_semantic_tokens(const json::Value &params);

  void publish_diagnostics(Document &doc);
  void ensure_analyzed(Document &doc);

  std::string uri_from_params(const json::Value &params) const;
  std::pair<int, int> position_from_params(const json::Value &params) const;
  std::string get_word_at(const std::string &text, int line, int character) const;
  std::string get_full_word_at(const std::string &text, int line, int character) const;

  Transport transport_;
  DocumentStore store_;
};

} // namespace kinglet::lsp
