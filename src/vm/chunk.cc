#include "vm/chunk.h"

#include <ostream>
#include <utility>

namespace kinglet {

uint32_t Chunk::add_constant(Value value) {
  constants_.push_back(value);
  return static_cast<uint32_t>(constants_.size() - 1);
}

void Chunk::write(OpCode op, int line, int column) {
  instructions_.push_back(Instruction{
      .op = op,
      .operand = 0,
      .line = line,
      .column = column,
  });
}

void Chunk::write_operand(OpCode op, uint32_t operand, int line, int column) {
  instructions_.push_back(Instruction{
      .op = op,
      .operand = static_cast<int32_t>(operand),
      .line = line,
      .column = column,
  });
}

void Chunk::write_constant(Value value, int line, int column) {
  const uint32_t index = add_constant(value);
  instructions_.push_back(Instruction{
      .op = OpCode::Constant,
      .operand = static_cast<int32_t>(index),
      .line = line,
      .column = column,
  });
}

const std::vector<Value> &Chunk::constants() const {
  return constants_;
}

const std::vector<Instruction> &Chunk::instructions() const {
  return instructions_;
}

int Chunk::add_function(FunctionInfo info) {
  int index = static_cast<int>(functions_.size());
  functions_.push_back(std::move(info));
  return index;
}

const std::vector<FunctionInfo> &Chunk::functions() const {
  return functions_;
}

int Chunk::add_struct_meta(StructMeta meta) {
  int index = static_cast<int>(struct_metas_.size());
  struct_metas_.push_back(std::move(meta));
  return index;
}

int Chunk::add_enum_meta(EnumMeta meta) {
  int index = static_cast<int>(enum_metas_.size());
  enum_metas_.push_back(std::move(meta));
  return index;
}

const std::vector<StructMeta> &Chunk::struct_metas() const {
  return struct_metas_;
}

const std::vector<EnumMeta> &Chunk::enum_metas() const {
  return enum_metas_;
}

void Chunk::disassemble(std::ostream &out) const {
  for (std::size_t i = 0; i < instructions_.size(); ++i) {
    const Instruction &instruction = instructions_[i];
    out << i << "  " << instruction.line << ':' << instruction.column << "  "
        << opcode_name(instruction.op);
    if (instruction.op == OpCode::Constant) {
      out << " #" << instruction.operand << " ("
          << constants_[static_cast<std::size_t>(instruction.operand)] << ")";
    } else if (instruction.op == OpCode::LoadLocal ||
               instruction.op == OpCode::StoreLocal) {
      out << " slot " << instruction.operand;
    } else if (instruction.op == OpCode::Call) {
      out << " args=" << instruction.operand;
    } else if (instruction.op == OpCode::Jmp ||
               instruction.op == OpCode::JmpFalse) {
      out << " +" << instruction.operand;
    } else if (instruction.op == OpCode::NativeOut) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeOutLn) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeErr) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeErrLn) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeIn) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeInSecret) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeFsRead) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeFsWrite) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::NativeSysArgs) {
      out << " argc=" << instruction.operand;
    } else if (instruction.op == OpCode::ArrayNew) {
      out << " count=" << instruction.operand;
    } else if (instruction.op == OpCode::MapNew) {
      out << " pairs=" << instruction.operand;
    }
    out << '\n';
  }
}

const char *opcode_name(OpCode op) {
  switch (op) {
  case OpCode::Constant:
    return "Constant";
  case OpCode::Null:
    return "Null";
  case OpCode::True:
    return "True";
  case OpCode::False:
    return "False";
  case OpCode::Add:
    return "Add";
  case OpCode::Subtract:
    return "Subtract";
  case OpCode::Multiply:
    return "Multiply";
  case OpCode::Divide:
    return "Divide";
  case OpCode::Modulo:
    return "Modulo";
  case OpCode::Negate:
    return "Negate";
  case OpCode::Not:
    return "Not";
  case OpCode::BitNot:
    return "BitNot";
  case OpCode::BitAnd:
    return "BitAnd";
  case OpCode::BitOr:
    return "BitOr";
  case OpCode::BitXor:
    return "BitXor";
  case OpCode::Shl:
    return "Shl";
  case OpCode::Shr:
    return "Shr";
  case OpCode::LoadLocal:
    return "LoadLocal";
  case OpCode::StoreLocal:
    return "StoreLocal";
  case OpCode::Pop:
    return "Pop";
  case OpCode::Call:
    return "Call";
  case OpCode::Return:
    return "Return";
  case OpCode::Jmp:
    return "Jmp";
  case OpCode::JmpFalse:
    return "JmpFalse";
  case OpCode::Eq:
    return "Eq";
  case OpCode::Neq:
    return "Neq";
  case OpCode::Lt:
    return "Lt";
  case OpCode::Gt:
    return "Gt";
  case OpCode::Le:
    return "Le";
  case OpCode::Ge:
    return "Ge";
  case OpCode::NativeOut:
    return "NativeOut";
  case OpCode::NativeOutLn:
    return "NativeOutLn";
  case OpCode::NativeErr:
    return "NativeErr";
  case OpCode::NativeErrLn:
    return "NativeErrLn";
  case OpCode::NativeIn:
    return "NativeIn";
  case OpCode::NativeInSecret:
    return "NativeInSecret";
  case OpCode::NativeFsRead:
    return "NativeFsRead";
  case OpCode::NativeFsWrite:
    return "NativeFsWrite";
  case OpCode::NativeSysArgs:
    return "NativeSysArgs";
  case OpCode::StructNew:
    return "StructNew";
  case OpCode::FieldGet:
    return "FieldGet";
  case OpCode::FieldSet:
    return "FieldSet";
  case OpCode::EnumVariant:
    return "EnumVariant";
  case OpCode::ArrayNew:
    return "ArrayNew";
  case OpCode::IndexGet:
    return "IndexGet";
  case OpCode::IndexSet:
    return "IndexSet";
  case OpCode::ArrayLen:
    return "ArrayLen";
  case OpCode::ArrayPush:
    return "ArrayPush";
  case OpCode::ArrayResize:
    return "ArrayResize";
  case OpCode::ArrayPop:
    return "ArrayPop";
  case OpCode::ArrayRemove:
    return "ArrayRemove";
  case OpCode::ArrayContains:
    return "ArrayContains";
  case OpCode::ArrayClear:
    return "ArrayClear";
  case OpCode::ArrayInsert:
    return "ArrayInsert";
  case OpCode::ArrayIndexOf:
    return "ArrayIndexOf";
  case OpCode::ArraySlice:
    return "ArraySlice";
  case OpCode::ArrayReverse:
    return "ArrayReverse";
  case OpCode::StringStartsWith:
    return "StringStartsWith";
  case OpCode::StringEndsWith:
    return "StringEndsWith";
  case OpCode::StringReplace:
    return "StringReplace";
  case OpCode::StringSplit:
    return "StringSplit";
  case OpCode::StringTrim:
    return "StringTrim";
  case OpCode::StringToUpper:
    return "StringToUpper";
  case OpCode::StringToLower:
    return "StringToLower";
  case OpCode::StringToInt:
    return "StringToInt";
  case OpCode::StringToFloat:
    return "StringToFloat";
  case OpCode::StringCode:
    return "StringCode";
  case OpCode::StringCodeAt:
    return "StringCodeAt";
  case OpCode::EnumVariantPayload:
    return "EnumVariantPayload";
  case OpCode::EnumPayloadGet:
    return "EnumPayloadGet";
  case OpCode::MapNew:
    return "MapNew";
  case OpCode::MapGet:
    return "MapGet";
  case OpCode::MapSet:
    return "MapSet";
  case OpCode::MapHas:
    return "MapHas";
  case OpCode::MapRemove:
    return "MapRemove";
  case OpCode::MapKeys:
    return "MapKeys";
  case OpCode::MapLen:
    return "MapLen";
  }
  return "Unknown";
}

} // namespace kinglet
