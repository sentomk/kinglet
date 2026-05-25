#include "vm/value.h"

namespace kinglet {

Value Value::int_value(int64_t value) {
  Value result;
  result.type = ValueType::Int;
  result.int_value_storage = value;
  return result;
}

Value Value::double_value(double value) {
  Value result;
  result.type = ValueType::Double;
  result.double_value_storage = value;
  return result;
}

Value Value::bool_value(bool value) {
  Value result;
  result.type = ValueType::Bool;
  result.bool_value_storage = value;
  return result;
}

Value Value::null_value() {
  return Value{};
}

Value Value::string_value(std::string value) {
  Value result;
  result.type = ValueType::String;
  result.string_storage = std::move(value);
  return result;
}

Value Value::function_value(int index) {
  Value result;
  result.type = ValueType::Function;
  result.function_index_storage = index;
  return result;
}

Value Value::native_function_value(NativeFn fn) {
  Value result;
  result.type = ValueType::NativeFunction;
  result.native_fn_storage = fn;
  return result;
}

Value Value::struct_value(int type_index, std::vector<Value> fields) {
  Value result;
  result.type = ValueType::Struct;
  result.struct_storage = std::make_shared<StructData>(StructData{type_index, std::move(fields)});
  return result;
}

Value Value::enum_value(int type_index, int variant_index) {
  Value result;
  result.type = ValueType::Enum;
  result.enum_type_index = type_index;
  result.enum_variant_index = variant_index;
  return result;
}

Value Value::array_value(std::vector<Value> elements) {
  Value result;
  result.type = ValueType::Array;
  result.array_storage = std::make_shared<ArrayData>(ArrayData{std::move(elements)});
  return result;
}

bool Value::is_number() const {
  return type == ValueType::Int || type == ValueType::Double;
}

double Value::as_double() const {
  if (type == ValueType::Int) {
    return static_cast<double>(int_value_storage);
  }
  return double_value_storage;
}

std::ostream &operator<<(std::ostream &out, const Value &value) {
  switch (value.type) {
  case ValueType::Int:
    out << value.int_value_storage;
    break;
  case ValueType::Double:
    out << value.double_value_storage;
    break;
  case ValueType::Bool:
    out << (value.bool_value_storage ? "true" : "false");
    break;
  case ValueType::Null:
    out << "null";
    break;
  case ValueType::String:
    out << value.string_storage;
    break;
  case ValueType::Function:
    out << "<function:" << value.function_index_storage << ">";
    break;
  case ValueType::Struct:
    out << "<struct:" << value.struct_storage->type_index << ">";
    break;
  case ValueType::Enum:
    out << "<enum:" << value.enum_type_index << ":" << value.enum_variant_index << ">";
    break;
  case ValueType::Array:
    out << "[";
    if (value.array_storage) {
      for (std::size_t i = 0; i < value.array_storage->elements.size(); ++i) {
        if (i > 0) {
          out << ", ";
        }
        out << value.array_storage->elements[i];
      }
    }
    out << "]";
    break;
  case ValueType::NativeFunction:
    out << "<native-fn>";
    break;
  }
  return out;
}

} // namespace kinglet
