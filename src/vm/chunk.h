#pragma once

#include "vm/value.h"

#include <cstdint>
#include <ostream>
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
  Negate,
  Not,
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
  NativePrint,
};

struct Instruction {
  OpCode op;
  int32_t operand = 0;
  int line = 0;
  int column = 0;
};

class Chunk {
public:
  uint32_t add_constant(Value value);
  void write(OpCode op, int line, int column);
  void write_operand(OpCode op, uint32_t operand, int line, int column);
  void write_constant(Value value, int line, int column);

  const std::vector<Value> &constants() const;
  const std::vector<Instruction> &instructions() const;
  void disassemble(std::ostream &out) const;

private:
  std::vector<Value> constants_;
  std::vector<Instruction> instructions_;
};

const char *opcode_name(OpCode op);

} // namespace kinglet
