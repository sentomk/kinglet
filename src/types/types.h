#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet {

enum class TypeKind {
  Int,
  Float,
  Bool,
  String,
  Void,
  Null,
  Function,
  Struct,
  Enum,
  Array,
};

struct Type;

struct FieldInfo {
  std::string name;
  TypeKind type_kind;
  std::string type_name;
  // Full resolved type of the field, retaining information that type_kind alone
  // loses — notably an array field's element_type. May be null for fields
  // registered before this was populated; callers fall back to type_kind/name.
  std::shared_ptr<Type> type;
};

struct Type {
  explicit Type(TypeKind kind);
  Type(const Type &other);
  Type &operator=(const Type &other);

  bool is_numeric() const;
  bool is_compatible_with(const Type &other) const;
  static Type promote(const Type &a, const Type &b);

  TypeKind kind;
  std::string name;
  std::vector<Type> param_types;
  std::shared_ptr<Type> return_type;
  std::shared_ptr<Type> element_type;
  std::vector<FieldInfo> fields;
  std::vector<std::string> variants;
  std::vector<std::vector<Type>> variant_param_types;
};

const Type &int_type();
const Type &float_type();
const Type &bool_type();
const Type &string_type();
const Type &void_type();
const Type &null_type();
Type array_type(Type element_type);

std::ostream &operator<<(std::ostream &out, const Type &type);
std::ostream &operator<<(std::ostream &out, TypeKind kind);

} // namespace kinglet
