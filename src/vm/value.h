#pragma once

#include <cstdint>
#include <ostream>

namespace kinglet {

enum class ValueType {
  Int,
  Double,
  Bool,
  Null,
};

struct Value {
  static Value int_value(int64_t value);
  static Value double_value(double value);
  static Value bool_value(bool value);
  static Value null_value();

  bool is_number() const;
  double as_double() const;

  ValueType type = ValueType::Null;
  int64_t int_value_storage = 0;
  double double_value_storage = 0.0;
  bool bool_value_storage = false;
};

std::ostream &operator<<(std::ostream &out, const Value &value);

} // namespace kinglet
