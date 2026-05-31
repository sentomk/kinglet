#include "vm/chunk.h"

#include <cstdint>
#include <cstring>
#include <ostream>
#include <istream>
#include <utility>

namespace kinglet {

namespace {

// .kbc binary format (little-endian, fixed-width):
//
//   magic         "KBC\0"   (4 bytes)
//   version       u32       (currently 1)
//   constants     u32 count + each entry: tag(u8) + tag-specific payload
//   instructions  u32 count + each entry: op(u8) operand(i32) line(i32) col(i32)
//   functions     u32 count + each entry: name(string) entry(u64) param_count(i32)
//   struct_metas  u32 count + each entry: name(string) field_names(u32 + each string)
//   enum_metas    u32 count + each entry: name(string) variants(u32 + each string)
//                                          + variant_param_counts(u32 + each i32)
//
// Strings are encoded as u32 byte length followed by raw bytes (no NUL).
//
// Constant tags mirror ValueType for the scalar subset the compiler ever puts
// in the pool. Anything outside this subset (Struct/Enum/Array/Map/NativeFn) is
// rejected — the bootstrap compiler never emits those into the constant pool.
enum class ConstTag : uint8_t {
  Int = 0,
  Double = 1,
  Bool = 2,
  Null = 3,
  String = 4,
  Function = 5,
};

constexpr char kMagic[4] = {'K', 'B', 'C', '\0'};
constexpr uint32_t kVersion = 1;

void write_u8(std::ostream &out, uint8_t v) { out.put(static_cast<char>(v)); }

void write_u32(std::ostream &out, uint32_t v) {
  char buf[4] = {
      static_cast<char>(v & 0xff),
      static_cast<char>((v >> 8) & 0xff),
      static_cast<char>((v >> 16) & 0xff),
      static_cast<char>((v >> 24) & 0xff),
  };
  out.write(buf, 4);
}

void write_i32(std::ostream &out, int32_t v) {
  write_u32(out, static_cast<uint32_t>(v));
}

void write_u64(std::ostream &out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.put(static_cast<char>((v >> (i * 8)) & 0xff));
  }
}

void write_i64(std::ostream &out, int64_t v) {
  write_u64(out, static_cast<uint64_t>(v));
}

void write_double(std::ostream &out, double v) {
  uint64_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  write_u64(out, bits);
}

void write_string(std::ostream &out, const std::string &s) {
  write_u32(out, static_cast<uint32_t>(s.size()));
  out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool read_bytes(std::istream &in, char *buf, std::size_t n) {
  in.read(buf, static_cast<std::streamsize>(n));
  return in.gcount() == static_cast<std::streamsize>(n);
}

bool read_u8(std::istream &in, uint8_t *out) {
  char c;
  if (!read_bytes(in, &c, 1)) return false;
  *out = static_cast<uint8_t>(c);
  return true;
}

bool read_u32(std::istream &in, uint32_t *out) {
  unsigned char buf[4];
  if (!read_bytes(in, reinterpret_cast<char *>(buf), 4)) return false;
  *out = static_cast<uint32_t>(buf[0])
       | (static_cast<uint32_t>(buf[1]) << 8)
       | (static_cast<uint32_t>(buf[2]) << 16)
       | (static_cast<uint32_t>(buf[3]) << 24);
  return true;
}

bool read_i32(std::istream &in, int32_t *out) {
  uint32_t v;
  if (!read_u32(in, &v)) return false;
  *out = static_cast<int32_t>(v);
  return true;
}

bool read_u64(std::istream &in, uint64_t *out) {
  unsigned char buf[8];
  if (!read_bytes(in, reinterpret_cast<char *>(buf), 8)) return false;
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<uint64_t>(buf[i]) << (i * 8);
  }
  *out = v;
  return true;
}

bool read_i64(std::istream &in, int64_t *out) {
  uint64_t v;
  if (!read_u64(in, &v)) return false;
  *out = static_cast<int64_t>(v);
  return true;
}

bool read_double(std::istream &in, double *out) {
  uint64_t bits;
  if (!read_u64(in, &bits)) return false;
  std::memcpy(out, &bits, sizeof(*out));
  return true;
}

bool read_string(std::istream &in, std::string *out) {
  uint32_t len;
  if (!read_u32(in, &len)) return false;
  out->resize(len);
  if (len == 0) return true;
  return read_bytes(in, out->data(), len);
}

} // namespace

bool Chunk::serialize(std::ostream &out) const {
  out.write(kMagic, 4);
  write_u32(out, kVersion);

  write_u32(out, static_cast<uint32_t>(constants_.size()));
  for (const Value &v : constants_) {
    switch (v.type) {
    case ValueType::Int:
      write_u8(out, static_cast<uint8_t>(ConstTag::Int));
      write_i64(out, v.int_value_storage);
      break;
    case ValueType::Double:
      write_u8(out, static_cast<uint8_t>(ConstTag::Double));
      write_double(out, v.double_value_storage);
      break;
    case ValueType::Bool:
      write_u8(out, static_cast<uint8_t>(ConstTag::Bool));
      write_u8(out, v.bool_value_storage ? 1 : 0);
      break;
    case ValueType::Null:
      write_u8(out, static_cast<uint8_t>(ConstTag::Null));
      break;
    case ValueType::String:
      write_u8(out, static_cast<uint8_t>(ConstTag::String));
      write_string(out, v.string_storage);
      break;
    case ValueType::Function:
      write_u8(out, static_cast<uint8_t>(ConstTag::Function));
      write_i32(out, v.function_index_storage);
      break;
    default:
      // Compiler invariant: only the scalar subset is ever placed in the pool.
      return false;
    }
  }

  write_u32(out, static_cast<uint32_t>(instructions_.size()));
  for (const Instruction &ins : instructions_) {
    write_u8(out, static_cast<uint8_t>(ins.op));
    write_i32(out, ins.operand);
    write_i32(out, ins.line);
    write_i32(out, ins.column);
  }

  write_u32(out, static_cast<uint32_t>(functions_.size()));
  for (const FunctionInfo &fi : functions_) {
    write_string(out, fi.name);
    write_u64(out, static_cast<uint64_t>(fi.entry));
    write_i32(out, fi.param_count);
  }

  write_u32(out, static_cast<uint32_t>(struct_metas_.size()));
  for (const StructMeta &sm : struct_metas_) {
    write_string(out, sm.name);
    write_u32(out, static_cast<uint32_t>(sm.field_names.size()));
    for (const std::string &fn : sm.field_names) {
      write_string(out, fn);
    }
  }

  write_u32(out, static_cast<uint32_t>(enum_metas_.size()));
  for (const EnumMeta &em : enum_metas_) {
    write_string(out, em.name);
    write_u32(out, static_cast<uint32_t>(em.variants.size()));
    for (const std::string &vn : em.variants) {
      write_string(out, vn);
    }
    write_u32(out, static_cast<uint32_t>(em.variant_param_counts.size()));
    for (int pc : em.variant_param_counts) {
      write_i32(out, pc);
    }
  }

  return out.good();
}

bool Chunk::deserialize(std::istream &in, std::string *error) {
  auto fail = [&](const char *msg) {
    if (error) *error = msg;
    return false;
  };

  char magic[4];
  if (!read_bytes(in, magic, 4)) return fail("kbc: short read on magic");
  if (std::memcmp(magic, kMagic, 4) != 0) return fail("kbc: bad magic");

  uint32_t version;
  if (!read_u32(in, &version)) return fail("kbc: short read on version");
  if (version != kVersion) return fail("kbc: unsupported version");

  constants_.clear();
  instructions_.clear();
  functions_.clear();
  struct_metas_.clear();
  enum_metas_.clear();

  uint32_t n_constants;
  if (!read_u32(in, &n_constants)) return fail("kbc: short read on constants count");
  constants_.reserve(n_constants);
  for (uint32_t i = 0; i < n_constants; ++i) {
    uint8_t tag;
    if (!read_u8(in, &tag)) return fail("kbc: short read on constant tag");
    switch (static_cast<ConstTag>(tag)) {
    case ConstTag::Int: {
      int64_t v;
      if (!read_i64(in, &v)) return fail("kbc: short read on int constant");
      constants_.push_back(Value::int_value(v));
      break;
    }
    case ConstTag::Double: {
      double v;
      if (!read_double(in, &v)) return fail("kbc: short read on double constant");
      constants_.push_back(Value::double_value(v));
      break;
    }
    case ConstTag::Bool: {
      uint8_t v;
      if (!read_u8(in, &v)) return fail("kbc: short read on bool constant");
      constants_.push_back(Value::bool_value(v != 0));
      break;
    }
    case ConstTag::Null:
      constants_.push_back(Value::null_value());
      break;
    case ConstTag::String: {
      std::string s;
      if (!read_string(in, &s)) return fail("kbc: short read on string constant");
      constants_.push_back(Value::string_value(std::move(s)));
      break;
    }
    case ConstTag::Function: {
      int32_t idx;
      if (!read_i32(in, &idx)) return fail("kbc: short read on function constant");
      constants_.push_back(Value::function_value(idx));
      break;
    }
    default:
      return fail("kbc: unknown constant tag");
    }
  }

  uint32_t n_insns;
  if (!read_u32(in, &n_insns)) return fail("kbc: short read on instructions count");
  instructions_.reserve(n_insns);
  for (uint32_t i = 0; i < n_insns; ++i) {
    uint8_t op;
    int32_t operand, line, col;
    if (!read_u8(in, &op)) return fail("kbc: short read on opcode");
    if (!read_i32(in, &operand)) return fail("kbc: short read on operand");
    if (!read_i32(in, &line)) return fail("kbc: short read on line");
    if (!read_i32(in, &col)) return fail("kbc: short read on column");
    instructions_.push_back(Instruction{
        .op = static_cast<OpCode>(op),
        .operand = operand,
        .line = line,
        .column = col,
    });
  }

  uint32_t n_funcs;
  if (!read_u32(in, &n_funcs)) return fail("kbc: short read on functions count");
  functions_.reserve(n_funcs);
  for (uint32_t i = 0; i < n_funcs; ++i) {
    FunctionInfo fi;
    uint64_t entry;
    int32_t pc;
    if (!read_string(in, &fi.name)) return fail("kbc: short read on function name");
    if (!read_u64(in, &entry)) return fail("kbc: short read on function entry");
    if (!read_i32(in, &pc)) return fail("kbc: short read on function param_count");
    fi.entry = static_cast<std::size_t>(entry);
    fi.param_count = pc;
    functions_.push_back(std::move(fi));
  }

  uint32_t n_structs;
  if (!read_u32(in, &n_structs)) return fail("kbc: short read on struct_metas count");
  struct_metas_.reserve(n_structs);
  for (uint32_t i = 0; i < n_structs; ++i) {
    StructMeta sm;
    if (!read_string(in, &sm.name)) return fail("kbc: short read on struct name");
    uint32_t n_fields;
    if (!read_u32(in, &n_fields)) return fail("kbc: short read on struct fields count");
    sm.field_names.reserve(n_fields);
    for (uint32_t j = 0; j < n_fields; ++j) {
      std::string fn;
      if (!read_string(in, &fn)) return fail("kbc: short read on struct field name");
      sm.field_names.push_back(std::move(fn));
    }
    struct_metas_.push_back(std::move(sm));
  }

  uint32_t n_enums;
  if (!read_u32(in, &n_enums)) return fail("kbc: short read on enum_metas count");
  enum_metas_.reserve(n_enums);
  for (uint32_t i = 0; i < n_enums; ++i) {
    EnumMeta em;
    if (!read_string(in, &em.name)) return fail("kbc: short read on enum name");
    uint32_t n_variants;
    if (!read_u32(in, &n_variants)) return fail("kbc: short read on enum variants count");
    em.variants.reserve(n_variants);
    for (uint32_t j = 0; j < n_variants; ++j) {
      std::string vn;
      if (!read_string(in, &vn)) return fail("kbc: short read on enum variant name");
      em.variants.push_back(std::move(vn));
    }
    uint32_t n_pcs;
    if (!read_u32(in, &n_pcs)) return fail("kbc: short read on enum param_counts count");
    em.variant_param_counts.reserve(n_pcs);
    for (uint32_t j = 0; j < n_pcs; ++j) {
      int32_t pc;
      if (!read_i32(in, &pc)) return fail("kbc: short read on enum variant param_count");
      em.variant_param_counts.push_back(pc);
    }
    enum_metas_.push_back(std::move(em));
  }

  return true;
}

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
    } else if (instruction.op == OpCode::CastTo) {
      const char *kind = instruction.operand == 0 ? "int"
                       : instruction.operand == 1 ? "float"
                       : instruction.operand == 2 ? "string" : "?";
      out << " -> " << kind;
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
  case OpCode::Dup:
    return "Dup";
  case OpCode::IsNull:
    return "IsNull";
  case OpCode::CastTo:
    return "CastTo";
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
