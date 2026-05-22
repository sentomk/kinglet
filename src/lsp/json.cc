#include "lsp/json.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace kinglet::json {

namespace {

void skip_ws(std::string_view input, std::size_t &pos) {
  while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) {
    ++pos;
  }
}

std::string parse_string(std::string_view input, std::size_t &pos) {
  if (pos >= input.size() || input[pos] != '"') {
    return "";
  }
  ++pos; // skip opening "
  std::string result;
  while (pos < input.size() && input[pos] != '"') {
    if (input[pos] == '\\' && pos + 1 < input.size()) {
      ++pos;
      switch (input[pos]) {
      case 'n': result += '\n'; break;
      case 't': result += '\t'; break;
      case 'r': result += '\r'; break;
      case '\\': result += '\\'; break;
      case '"': result += '"'; break;
      default: result += input[pos]; break;
      }
    } else {
      result += input[pos];
    }
    ++pos;
  }
  if (pos < input.size()) ++pos; // skip closing "
  return result;
}

Number parse_number(std::string_view input, std::size_t &pos) {
  const std::size_t start = pos;
  if (pos < input.size() && input[pos] == '-') ++pos;
  while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
  if (pos < input.size() && input[pos] == '.') {
    ++pos;
    while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
  }
  if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
    ++pos;
    if (pos < input.size() && (input[pos] == '+' || input[pos] == '-')) ++pos;
    while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
  }
  const auto num_str = input.substr(start, pos - start);
  char *end = nullptr;
  return std::strtod(num_str.data(), &end);
}

Value parse_value(std::string_view input, std::size_t &pos);

Object parse_object(std::string_view input, std::size_t &pos) {
  Object obj;
  if (pos >= input.size() || input[pos] != '{') return obj;
  ++pos;
  skip_ws(input, pos);
  if (pos < input.size() && input[pos] == '}') { ++pos; return obj; }
  while (pos < input.size()) {
    skip_ws(input, pos);
    std::string key = parse_string(input, pos);
    skip_ws(input, pos);
    if (pos < input.size() && input[pos] == ':') ++pos;
    skip_ws(input, pos);
    obj[key] = parse_value(input, pos);
    skip_ws(input, pos);
    if (pos < input.size() && input[pos] == ',') { ++pos; continue; }
    if (pos < input.size() && input[pos] == '}') { ++pos; break; }
  }
  return obj;
}

Array parse_array(std::string_view input, std::size_t &pos) {
  Array arr;
  if (pos >= input.size() || input[pos] != '[') return arr;
  ++pos;
  skip_ws(input, pos);
  if (pos < input.size() && input[pos] == ']') { ++pos; return arr; }
  while (pos < input.size()) {
    skip_ws(input, pos);
    arr.push_back(parse_value(input, pos));
    skip_ws(input, pos);
    if (pos < input.size() && input[pos] == ',') { ++pos; continue; }
    if (pos < input.size() && input[pos] == ']') { ++pos; break; }
  }
  return arr;
}

Value parse_value(std::string_view input, std::size_t &pos) {
  skip_ws(input, pos);
  if (pos >= input.size()) return Value::null();
  switch (input[pos]) {
  case '{': return Value{parse_object(input, pos)};
  case '[': return Value{parse_array(input, pos)};
  case '"': return Value{parse_string(input, pos)};
  case 't': pos += 4; return Value{true};
  case 'f': pos += 5; return Value{false};
  case 'n': pos += 4; return Value::null();
  default: return Value{parse_number(input, pos)};
  }
}

std::string escape_json(const std::string &s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    default: result += c; break;
    }
  }
  return result;
}

} // namespace

Value parse(std::string_view input, std::size_t &pos) {
  return parse_value(input, pos);
}

std::string to_string(const Value &val) {
  if (val.is_string()) {
    return "\"" + escape_json(val.as_string()) + "\"";
  }
  if (val.is_bool()) {
    return val.as_bool() ? "true" : "false";
  }
  if (val.is_number()) {
    std::ostringstream oss;
    oss << val.as_number();
    return oss.str();
  }
  if (val.is_object()) {
    std::string result = "{";
    bool first = true;
    for (const auto &[k, v] : val.as_object()) {
      if (!first) result += ",";
      first = false;
      result += "\"" + escape_json(k) + "\":" + to_string(v);
    }
    result += "}";
    return result;
  }
  if (val.is_array()) {
    std::string result = "[";
    bool first = true;
    for (const auto &v : val.as_array()) {
      if (!first) result += ",";
      first = false;
      result += to_string(v);
    }
    result += "]";
    return result;
  }
  return "null";
}

} // namespace kinglet::json
