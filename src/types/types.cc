#include "types/types.h"

#include <utility>

namespace kinglet {

Type::Type(TypeKind kind) : kind(kind) {}

Type::Type(const Type &other)
    : kind(other.kind), param_types(other.param_types),
      return_type(other.return_type ? std::make_shared<Type>(*other.return_type) : nullptr) {}

Type &Type::operator=(const Type &other) {
  if (this != &other) {
    kind = other.kind;
    param_types = other.param_types;
    return_type = other.return_type ? std::make_shared<Type>(*other.return_type) : nullptr;
  }
  return *this;
}

bool Type::is_numeric() const {
  return kind == TypeKind::Int || kind == TypeKind::Float;
}

bool Type::is_compatible_with(const Type &other) const {
  if (kind == other.kind) {
    return true;
  }
  if (is_numeric() && other.is_numeric()) {
    return true;
  }
  if (kind == TypeKind::Null || other.kind == TypeKind::Null) {
    return true;
  }
  return false;
}

Type Type::promote(const Type &a, const Type &b) {
  if (a.kind == TypeKind::Float || b.kind == TypeKind::Float) {
    return float_type();
  }
  return a;
}

static const Type INT_INSTANCE{TypeKind::Int};
static const Type FLOAT_INSTANCE{TypeKind::Float};
static const Type BOOL_INSTANCE{TypeKind::Bool};
static const Type STRING_INSTANCE{TypeKind::String};
static const Type VOID_INSTANCE{TypeKind::Void};
static const Type NULL_INSTANCE{TypeKind::Null};

const Type &int_type() {
  return INT_INSTANCE;
}

const Type &float_type() {
  return FLOAT_INSTANCE;
}

const Type &bool_type() {
  return BOOL_INSTANCE;
}

const Type &string_type() {
  return STRING_INSTANCE;
}

const Type &void_type() {
  return VOID_INSTANCE;
}

const Type &null_type() {
  return NULL_INSTANCE;
}

std::ostream &operator<<(std::ostream &out, const Type &type) {
  return out << type.kind;
}

std::ostream &operator<<(std::ostream &out, TypeKind kind) {
  switch (kind) {
  case TypeKind::Int:
    return out << "Int";
  case TypeKind::Float:
    return out << "Float";
  case TypeKind::Bool:
    return out << "Bool";
  case TypeKind::String:
    return out << "String";
  case TypeKind::Void:
    return out << "Void";
  case TypeKind::Null:
    return out << "Null";
  case TypeKind::Function:
    return out << "Function";
  }
  return out << "Unknown";
}

} // namespace kinglet
