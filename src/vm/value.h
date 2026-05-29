#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet {

enum class ValueType {
  Int,
  Double,
  Bool,
  Null,
  String,
  Function,
  Struct,
  Enum,
  Array,
  NativeFunction,
};

enum class NativeFn {
  IoOut,
  IoOutLine,
  IoErr,
  IoErrLine,
  IoIn,
  IoInSecret,
  FsRead,
  FsWrite,
  SysArgs,
};

struct Value;

struct StructData {
  int type_index;
  std::vector<Value> fields;
};

struct ArrayData {
  std::vector<Value> elements;
};

struct Value {
  static Value int_value(int64_t value);
  static Value double_value(double value);
  static Value bool_value(bool value);
  static Value null_value();
  static Value string_value(std::string value);
  static Value function_value(int index);
  static Value native_function_value(NativeFn fn);
  static Value struct_value(int type_index, std::vector<Value> fields);
  static Value enum_value(int type_index, int variant_index);
  static Value enum_value_with_payload(int type_index, int variant_index, std::vector<Value> payload);
  static Value array_value(std::vector<Value> elements);

  bool is_number() const;
  double as_double() const;

  ValueType type = ValueType::Null;
  int64_t int_value_storage = 0;
  double double_value_storage = 0.0;
  bool bool_value_storage = false;
  std::string string_storage;
  int function_index_storage = -1;
  NativeFn native_fn_storage = NativeFn::IoOut;
  std::shared_ptr<StructData> struct_storage;
  int enum_type_index = -1;
  int enum_variant_index = -1;
  std::vector<Value> enum_payload;
  std::shared_ptr<ArrayData> array_storage;
};

std::ostream &operator<<(std::ostream &out, const Value &value);

} // namespace kinglet
