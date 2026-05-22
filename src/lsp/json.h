#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace kinglet::json {

struct Value;

using Object = std::unordered_map<std::string, Value>;
using Array = std::vector<Value>;
using Number = double;

struct Value {
  std::variant<std::string, Number, bool, Object, Array, std::monostate> data;

  bool is_object() const { return std::holds_alternative<Object>(data); }
  bool is_array() const { return std::holds_alternative<Array>(data); }
  bool is_string() const { return std::holds_alternative<std::string>(data); }
  bool is_number() const { return std::holds_alternative<Number>(data); }
  bool is_bool() const { return std::holds_alternative<bool>(data); }

  const Object &as_object() const { return std::get<Object>(data); }
  const Array &as_array() const { return std::get<Array>(data); }
  const std::string &as_string() const { return std::get<std::string>(data); }
  Number as_number() const { return std::get<Number>(data); }
  bool as_bool() const { return std::get<bool>(data); }

  static Value object(Object obj) { return Value{std::move(obj)}; }
  static Value array(Array arr) { return Value{std::move(arr)}; }
  static Value string(std::string s) { return Value{std::move(s)}; }
  static Value number(Number n) { return Value{n}; }
  static Value boolean(bool b) { return Value{b}; }
  static Value null() { return Value{std::monostate{}}; }

  explicit Value(std::monostate = std::monostate{}) : data(std::monostate{}) {}
  explicit Value(Object obj) : data(std::move(obj)) {}
  explicit Value(Array arr) : data(std::move(arr)) {}
  explicit Value(std::string s) : data(std::move(s)) {}
  explicit Value(Number n) : data(n) {}
  explicit Value(bool b) : data(b) {}
};

Value parse(std::string_view input, std::size_t &pos);
std::string to_string(const Value &val);

} // namespace kinglet::json
