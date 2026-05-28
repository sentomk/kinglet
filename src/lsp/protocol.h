#pragma once

#include "lsp/json.h"

#include <string>

namespace kinglet::lsp::protocol {

inline json::Value position(int line, int character) {
  json::Object pos;
  pos["line"] = json::Value::number(line);
  pos["character"] = json::Value::number(character);
  return json::Value(pos);
}

inline json::Value range(int start_line, int start_char, int end_line, int end_char) {
  json::Object r;
  r["start"] = position(start_line, start_char);
  r["end"] = position(end_line, end_char);
  return json::Value(r);
}

inline json::Value diagnostic(int line, int col, const std::string &message, int severity = 1, int length = 1) {
  json::Object item;
  item["range"] = range(line - 1, col - 1, line - 1, col - 1 + length);
  item["message"] = json::Value::string(message);
  item["severity"] = json::Value::number(severity);
  item["source"] = json::Value::string("kinglet");
  return json::Value(item);
}

inline json::Value location(const std::string &uri, int line, int col) {
  json::Object loc;
  loc["uri"] = json::Value::string(uri);
  loc["range"] = range(line - 1, col - 1, line - 1, col);
  return json::Value(loc);
}

inline json::Value completion_item(const std::string &label, int kind,
                                   const std::string &detail = "",
                                   const std::string &insert_text = "",
                                   int insert_text_format = 1) {
  json::Object item;
  item["label"] = json::Value::string(label);
  item["kind"] = json::Value::number(kind);
  if (!detail.empty()) item["detail"] = json::Value::string(detail);
  if (!insert_text.empty()) {
    item["insertText"] = json::Value::string(insert_text);
    item["insertTextFormat"] = json::Value::number(insert_text_format);
  }
  return json::Value(item);
}

inline json::Value snippet_item_with_edit(const std::string &label, int kind,
                                          const std::string &detail,
                                          const std::string &snippet_body,
                                          int line, int start_char, int end_char) {
  json::Object item;
  item["label"] = json::Value::string(label);
  item["kind"] = json::Value::number(kind);
  if (!detail.empty()) item["detail"] = json::Value::string(detail);
  item["insertTextFormat"] = json::Value::number(2);
  item["filterText"] = json::Value::string(label);
  json::Object text_edit;
  text_edit["range"] = range(line, start_char, line, end_char);
  text_edit["newText"] = json::Value::string(snippet_body);
  item["textEdit"] = json::Value(text_edit);
  return json::Value(item);
}

inline json::Value completion_item_with_edit(const std::string &label, int kind,
                                            const std::string &detail,
                                            int line, int start_char, int end_char) {
  json::Object item;
  item["label"] = json::Value::string(label);
  item["kind"] = json::Value::number(kind);
  if (!detail.empty()) item["detail"] = json::Value::string(detail);
  item["filterText"] = json::Value::string(label);
  json::Object text_edit;
  text_edit["range"] = range(line, start_char, line, end_char);
  text_edit["newText"] = json::Value::string(label);
  item["textEdit"] = json::Value(text_edit);
  return json::Value(item);
}

} // namespace kinglet::lsp::protocol
