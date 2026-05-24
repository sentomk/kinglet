#pragma once

#include "vm/value.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace kinglet {

enum class OpCode : uint8_t {
  Constant,
  Null,
  True,
  False,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  Negate,
  Not,
  BitNot,
  LoadLocal,
  StoreLocal,
  Pop,
  Call,
  Return,
  Jmp,
  JmpFalse,
  Eq,
  Neq,
  Lt,
  Gt,
  Le,
  Ge,
  NativeOut,
  NativeErr,
  NativeIn,
  StructNew,
  FieldGet,
  FieldSet,
  EnumVariant,
  ArrayNew,
  IndexGet,
  IndexSet,
};

struct Instruction {
  OpCode op;
  int32_t operand = 0;
  int line = 0;
  int column = 0;
};

struct FunctionInfo {
  std::string name;
  std::size_t entry = 0;
  int param_count = 0;
};

struct StructMeta {
  std::string name;
  std::vector<std::string> field_names;
};

struct EnumMeta {
  std::string name;
  std::vector<std::string> variants;
};

class Chunk {
public:
  uint32_t add_constant(Value value);
  void write(OpCode op, int line, int column);
  void write_operand(OpCode op, uint32_t operand, int line, int column);
  void write_constant(Value value, int line, int column);

  int add_function(FunctionInfo info);
  int add_struct_meta(StructMeta meta);
  int add_enum_meta(EnumMeta meta);
  const std::vector<FunctionInfo> &functions() const;
  const std::vector<StructMeta> &struct_metas() const;
  const std::vector<EnumMeta> &enum_metas() const;

  const std::vector<Value> &constants() const;
  const std::vector<Instruction> &instructions() const;
  void disassemble(std::ostream &out) const;

private:
  std::vector<Value> constants_;
  std::vector<Instruction> instructions_;
  std::vector<FunctionInfo> functions_;
  std::vector<StructMeta> struct_metas_;
  std::vector<EnumMeta> enum_metas_;
};

const char *opcode_name(OpCode op);

} // namespace kinglet
