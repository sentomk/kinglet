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
};

struct Type {
  explicit Type(TypeKind kind);
  Type(const Type &other);
  Type &operator=(const Type &other);

  bool is_numeric() const;
  bool is_compatible_with(const Type &other) const;
  static Type promote(const Type &a, const Type &b);

  TypeKind kind;
  std::vector<Type> param_types;
  std::shared_ptr<Type> return_type;
};

const Type &int_type();
const Type &float_type();
const Type &bool_type();
const Type &string_type();
const Type &void_type();
const Type &null_type();

std::ostream &operator<<(std::ostream &out, const Type &type);
std::ostream &operator<<(std::ostream &out, TypeKind kind);

} // namespace kinglet
