#include "vm/vm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
namespace kinglet {

namespace {

// Encode a map key Value into a lookup string. The "s:"/"i:" prefix keeps the
// string key "65" distinct from the int key 65. Only string and int keys are
// supported (enforced by the type checker); other types return empty, treated
// as a non-match by callers.
std::string encode_map_key(const Value &key) {
  if (key.type == ValueType::String) {
    return "s:" + key.string_storage;
  }
  if (key.type == ValueType::Int) {
    return "i:" + std::to_string(key.int_value_storage);
  }
  return std::string();
}

} // namespace

VmResult Vm::run(const Chunk &chunk, const std::vector<std::string> &args) {
  stack_.clear();
  frames_.clear();
  program_args_ = args;
  frames_.push_back(CallFrame{.chunk = &chunk, .ip = 0, .locals = {}});

  while (!frames_.empty()) {
    CallFrame &frame = frames_.back();
    const std::vector<Instruction> &instructions = frame.chunk->instructions();
    const std::vector<Value> &constants = frame.chunk->constants();

    if (frame.ip >= instructions.size()) {
      return runtime_error("Instruction pointer out of range.");
    }

    const Instruction &instruction = instructions[frame.ip];
    ++frame.ip;

    switch (instruction.op) {
    case OpCode::Constant:
      if (instruction.operand < 0 ||
          static_cast<std::size_t>(instruction.operand) >= constants.size()) {
        return runtime_error("Constant index out of range.");
      }
      push(constants[static_cast<std::size_t>(instruction.operand)]);
      break;
    case OpCode::Null:
      push(Value::null_value());
      break;
    case OpCode::True:
      push(Value::bool_value(true));
      break;
    case OpCode::False:
      push(Value::bool_value(false));
      break;
    case OpCode::Add:
    case OpCode::Subtract:
    case OpCode::Multiply:
    case OpCode::Divide:
    case OpCode::Modulo: {
      if (instruction.op == OpCode::Add && stack_.size() >= 2 &&
          stack_[stack_.size() - 1].type == ValueType::String &&
          stack_[stack_.size() - 2].type == ValueType::String) {
        Value right = pop();
        Value left = pop();
        push(Value::string_value(left.string_storage + right.string_storage));
        break;
      }
      std::string error;
      if (!binary_numeric(instruction.op, &error)) {
        return runtime_error(std::move(error));
      }
      break;
    }
    case OpCode::BitNot: {
      if (stack_.empty() || stack_.back().type != ValueType::Int) {
        return runtime_error("Operand must be an integer.");
      }
      Value value = pop();
      push(Value::int_value(~value.int_value_storage));
      break;
    }
    case OpCode::BitAnd:
    case OpCode::BitOr:
    case OpCode::BitXor:
    case OpCode::Shl:
    case OpCode::Shr: {
      if (stack_.size() < 2) {
        return runtime_error("Stack underflow.");
      }
      if (stack_[stack_.size() - 1].type != ValueType::Int ||
          stack_[stack_.size() - 2].type != ValueType::Int) {
        return runtime_error("Bitwise operands must be integers.");
      }
      Value right = pop();
      Value left = pop();
      int64_t l = left.int_value_storage;
      int64_t r = right.int_value_storage;
      int64_t result = 0;
      switch (instruction.op) {
      case OpCode::BitAnd:
        result = l & r;
        break;
      case OpCode::BitOr:
        result = l | r;
        break;
      case OpCode::BitXor:
        result = l ^ r;
        break;
      case OpCode::Shl:
      case OpCode::Shr:
        // A shift amount outside [0, 63] is undefined in C++; define it here as
        // yielding 0 so Kinglet programs get deterministic behaviour.
        if (r < 0 || r >= 64) {
          result = 0;
        } else if (instruction.op == OpCode::Shl) {
          result = static_cast<int64_t>(static_cast<uint64_t>(l) << r);
        } else {
          result = static_cast<int64_t>(static_cast<uint64_t>(l) >> r);
        }
        break;
      default:
        break;
      }
      push(Value::int_value(result));
      break;
    }
    case OpCode::Negate: {
      if (stack_.empty() || !stack_.back().is_number()) {
        return runtime_error("Operand must be numeric.");
      }
      Value value = pop();
      if (value.type == ValueType::Int) {
        push(Value::int_value(-value.int_value_storage));
      } else {
        push(Value::double_value(-value.double_value_storage));
      }
      break;
    }
    case OpCode::Not: {
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      Value value = pop();
      bool truthy = false;
      switch (value.type) {
      case ValueType::Bool:
        truthy = value.bool_value_storage;
        break;
      case ValueType::Null:
        truthy = false;
        break;
      case ValueType::Int:
        truthy = value.int_value_storage != 0;
        break;
      case ValueType::Double:
        truthy = value.double_value_storage != 0.0;
        break;
      case ValueType::String:
        truthy = !value.string_storage.empty();
        break;
      case ValueType::Function:
      case ValueType::Struct:
      case ValueType::Enum:
      case ValueType::Array:
      case ValueType::NativeFunction:
        truthy = true;
        break;
      }
      push(Value::bool_value(!truthy));
      break;
    }
    case OpCode::LoadLocal:
      if (instruction.operand < 0 ||
          static_cast<std::size_t>(instruction.operand) >= frame.locals.size()) {
        return runtime_error("Local slot out of range.");
      }
      push(frame.locals[static_cast<std::size_t>(instruction.operand)]);
      break;
    case OpCode::StoreLocal:
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      if (instruction.operand < 0) {
        return runtime_error("Invalid local slot.");
      }
      if (static_cast<std::size_t>(instruction.operand) >= frame.locals.size()) {
        frame.locals.resize(static_cast<std::size_t>(instruction.operand) + 1,
                            Value::null_value());
      }
      frame.locals[static_cast<std::size_t>(instruction.operand)] = stack_.back();
      break;
    case OpCode::Pop:
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      pop();
      break;
    case OpCode::Dup:
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      stack_.push_back(stack_.back());
      break;
    case OpCode::IsNull: {
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      Value v = pop();
      push(Value::bool_value(v.type == ValueType::Null));
      break;
    }
    case OpCode::CastTo: {
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      Value v = pop();
      // operand: 0 = int, 1 = float, 2 = string
      switch (instruction.operand) {
      case 0: { // -> int
        if (v.type == ValueType::Int) {
          push(v);
        } else if (v.type == ValueType::Double) {
          push(Value::int_value(static_cast<int64_t>(v.double_value_storage)));
        } else if (v.type == ValueType::String) {
          const std::string &s = v.string_storage;
          if (s.empty()) { push(Value::null_value()); break; }
          char *end = nullptr;
          long long parsed = std::strtoll(s.c_str(), &end, 10);
          if (end == s.c_str() || *end != '\0') {
            push(Value::null_value());
          } else {
            push(Value::int_value(static_cast<int64_t>(parsed)));
          }
        } else {
          return runtime_error("Cannot cast value to int.");
        }
        break;
      }
      case 1: { // -> float
        if (v.type == ValueType::Double) {
          push(v);
        } else if (v.type == ValueType::Int) {
          push(Value::double_value(static_cast<double>(v.int_value_storage)));
        } else if (v.type == ValueType::String) {
          const std::string &s = v.string_storage;
          if (s.empty()) { push(Value::null_value()); break; }
          char *end = nullptr;
          double parsed = std::strtod(s.c_str(), &end);
          if (end == s.c_str() || *end != '\0') {
            push(Value::null_value());
          } else {
            push(Value::double_value(parsed));
          }
        } else {
          return runtime_error("Cannot cast value to float.");
        }
        break;
      }
      case 2: { // -> string
        std::ostringstream out;
        if (v.type == ValueType::Int) {
          out << v.int_value_storage;
        } else if (v.type == ValueType::Double) {
          out << v.double_value_storage;
        } else if (v.type == ValueType::String) {
          out << v.string_storage;
        } else {
          return runtime_error("Cannot cast value to string.");
        }
        push(Value::string_value(out.str()));
        break;
      }
      default:
        return runtime_error("Invalid CastTo target.");
      }
      break;
    }
    case OpCode::Call: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count + 1) {
        return runtime_error("Stack underflow for function call.");
      }
      Value callee = pop();
      if (callee.type == ValueType::NativeFunction) {
        std::vector<Value> args(arg_count);
        for (uint32_t i = 0; i < arg_count; ++i) {
          args[arg_count - 1 - i] = pop();
        }
        switch (callee.native_fn_storage) {
        case NativeFn::IoOut:
        case NativeFn::IoOutLine:
          if (!args.empty() && args[0].type == ValueType::String) {
            const std::string &fmt = args[0].string_storage;
            std::size_t val_idx = 1;
            for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
              if (pos + 1 < fmt.size() && fmt[pos] == '{' && fmt[pos + 1] == '}') {
                if (val_idx < args.size()) {
                  std::cout << args[val_idx++];
                } else {
                  std::cout << "{}";
                }
                ++pos;
              } else {
                std::cout << fmt[pos];
              }
            }
          } else {
            for (const Value &arg : args) {
              std::cout << arg;
            }
          }
          if (callee.native_fn_storage == NativeFn::IoOutLine) {
            std::cout << '\n';
          }
          std::cout << std::flush;
          push(Value::null_value());
          break;
        case NativeFn::IoErr:
        case NativeFn::IoErrLine:
          if (!args.empty() && args[0].type == ValueType::String) {
            const std::string &fmt = args[0].string_storage;
            std::size_t val_idx = 1;
            for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
              if (pos + 1 < fmt.size() && fmt[pos] == '{' && fmt[pos + 1] == '}') {
                if (val_idx < args.size()) {
                  std::cerr << args[val_idx++];
                } else {
                  std::cerr << "{}";
                }
                ++pos;
              } else {
                std::cerr << fmt[pos];
              }
            }
          } else {
            for (const Value &arg : args) {
              std::cerr << arg;
            }
          }
          if (callee.native_fn_storage == NativeFn::IoErrLine) {
            std::cerr << '\n';
          }
          std::cerr << std::flush;
          push(Value::null_value());
          break;
        case NativeFn::IoIn:
        case NativeFn::IoInSecret:
          for (const Value &arg : args) {
            if (arg.type == ValueType::String) {
              std::cout << arg.string_storage << std::flush;
            }
          }
          if (callee.native_fn_storage == NativeFn::IoInSecret) {
            disable_echo();
          }
          {
            std::string line;
            if (!std::getline(std::cin, line)) {
              push(Value::null_value());
            } else {
              push(Value::string_value(std::move(line)));
            }
          }
          if (callee.native_fn_storage == NativeFn::IoInSecret) {
            restore_echo();
            std::cout << '\n';
          }
          break;
        case NativeFn::FsRead: {
          if (args.size() != 1 || args[0].type != ValueType::String) {
            push(Value::null_value());
            break;
          }
          std::ifstream file(args[0].string_storage, std::ios::binary);
          if (!file) {
            push(Value::null_value());
            break;
          }
          std::ostringstream buffer;
          buffer << file.rdbuf();
          if (file.bad()) {
            push(Value::null_value());
            break;
          }
          push(Value::string_value(buffer.str()));
          break;
        }
        case NativeFn::FsWrite: {
          if (args.size() == 2 && args[0].type == ValueType::String &&
              args[1].type == ValueType::String) {
            std::ofstream file(args[0].string_storage,
                               std::ios::binary | std::ios::trunc);
            if (file) {
              file.write(args[1].string_storage.data(),
                         static_cast<std::streamsize>(args[1].string_storage.size()));
            }
          }
          push(Value::null_value());
          break;
        }
        case NativeFn::SysArgs: {
          std::vector<Value> elements;
          elements.reserve(program_args_.size());
          for (const std::string &arg : program_args_) {
            elements.push_back(Value::string_value(arg));
          }
          push(Value::array_value(std::move(elements)));
          break;
        }
        }
        break;
      }
      if (callee.type != ValueType::Function) {
        return runtime_error("Attempted to call a non-function value.");
      }
      int func_idx = callee.function_index_storage;
      const auto &functions = frame.chunk->functions();
      if (func_idx < 0 || static_cast<std::size_t>(func_idx) >= functions.size()) {
        return runtime_error("Invalid function index.");
      }
      const FunctionInfo &info = functions[static_cast<std::size_t>(func_idx)];
      if (static_cast<int>(arg_count) != info.param_count) {
        return runtime_error("Expected " + std::to_string(info.param_count) +
                             " arguments but got " + std::to_string(arg_count) + ".");
      }
      CallFrame new_frame;
      new_frame.chunk = frame.chunk;
      new_frame.ip = info.entry;
      new_frame.locals.resize(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        new_frame.locals[arg_count - 1 - i] = pop();
      }
      frames_.push_back(std::move(new_frame));
      break;
    }
    case OpCode::Return: {
      Value result = stack_.empty() ? Value::null_value() : pop();
      frames_.pop_back();
      if (frames_.empty()) {
        return VmResult{.ok = true, .value = result, .error = ""};
      }
      push(result);
      break;
    }
    case OpCode::Jmp:
      frame.ip += static_cast<std::size_t>(instruction.operand);
      break;
    case OpCode::JmpFalse: {
      if (stack_.empty()) {
        return runtime_error("Stack underflow.");
      }
      Value condition = pop();
      bool truthy = false;
      switch (condition.type) {
      case ValueType::Bool:
        truthy = condition.bool_value_storage;
        break;
      case ValueType::Null:
        truthy = false;
        break;
      case ValueType::Int:
        truthy = condition.int_value_storage != 0;
        break;
      case ValueType::Double:
        truthy = condition.double_value_storage != 0.0;
        break;
      case ValueType::String:
        truthy = !condition.string_storage.empty();
        break;
      case ValueType::Function:
      case ValueType::Struct:
      case ValueType::Enum:
      case ValueType::Array:
      case ValueType::NativeFunction:
        truthy = true;
        break;
      }
      if (!truthy) {
        frame.ip += static_cast<std::size_t>(instruction.operand);
      }
      break;
    }
    case OpCode::Eq:
    case OpCode::Neq:
    case OpCode::Lt:
    case OpCode::Gt:
    case OpCode::Le:
    case OpCode::Ge: {
      if (stack_.size() < 2) {
        return runtime_error("Stack underflow.");
      }
      Value right = pop();
      Value left = pop();
      bool result = false;
      if (instruction.op == OpCode::Eq) {
        if (left.type != right.type) {
          result = false;
        } else if (left.type == ValueType::Enum) {
          result = left.enum_type_index == right.enum_type_index &&
                   left.enum_variant_index == right.enum_variant_index;
        } else if (left.type == ValueType::String) {
          result = left.string_storage == right.string_storage;
        } else if (left.type == ValueType::Array) {
          result = left.array_storage == right.array_storage;
        } else {
          result = left.int_value_storage == right.int_value_storage &&
                   left.double_value_storage == right.double_value_storage &&
                   left.bool_value_storage == right.bool_value_storage;
        }
      } else if (instruction.op == OpCode::Neq) {
        if (left.type != right.type) {
          result = true;
        } else if (left.type == ValueType::Enum) {
          result = left.enum_type_index != right.enum_type_index ||
                   left.enum_variant_index != right.enum_variant_index;
        } else if (left.type == ValueType::String) {
          result = left.string_storage != right.string_storage;
        } else if (left.type == ValueType::Array) {
          result = left.array_storage != right.array_storage;
        } else {
          result = !(left.int_value_storage == right.int_value_storage &&
                     left.double_value_storage == right.double_value_storage &&
                     left.bool_value_storage == right.bool_value_storage);
        }
      } else {
        if (left.type == ValueType::String && right.type == ValueType::String) {
          switch (instruction.op) {
          case OpCode::Lt:
            result = left.string_storage < right.string_storage;
            break;
          case OpCode::Gt:
            result = left.string_storage > right.string_storage;
            break;
          case OpCode::Le:
            result = left.string_storage <= right.string_storage;
            break;
          case OpCode::Ge:
            result = left.string_storage >= right.string_storage;
            break;
          default:
            break;
          }
        } else if (!left.is_number() || !right.is_number()) {
          return runtime_error("Comparison operands must be numeric.");
        } else {
          double lhs = left.as_double();
          double rhs = right.as_double();
          switch (instruction.op) {
          case OpCode::Lt:
            result = lhs < rhs;
            break;
          case OpCode::Gt:
            result = lhs > rhs;
            break;
          case OpCode::Le:
            result = lhs <= rhs;
            break;
          case OpCode::Ge:
            result = lhs >= rhs;
            break;
          default:
            break;
          }
        }
      }
      push(Value::bool_value(result));
      break;
    }
    case OpCode::NativeOut:
    case OpCode::NativeOutLn: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count) {
        return runtime_error("Stack underflow for io::out.");
      }

      std::vector<Value> args(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        args[arg_count - 1 - i] = pop();
      }

      if (!args.empty() && args[0].type == ValueType::String) {
        const std::string &fmt = args[0].string_storage;
        std::size_t val_idx = 1;
        for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
          if (pos + 1 < fmt.size() && fmt[pos] == '{' && fmt[pos + 1] == '}') {
            if (val_idx < args.size()) {
              std::cout << args[val_idx];
              ++val_idx;
            } else {
              std::cout << "{}";
            }
            ++pos;
          } else {
            std::cout << fmt[pos];
          }
        }
      } else {
        for (const Value &arg : args) {
          std::cout << arg;
        }
      }
      if (instruction.op == OpCode::NativeOutLn) {
        std::cout << '\n';
      }
      std::cout << std::flush;
      push(Value::null_value());
      break;
    }
    case OpCode::NativeErr:
    case OpCode::NativeErrLn: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count) {
        return runtime_error("Stack underflow for io::err.");
      }
      std::vector<Value> args(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        args[arg_count - 1 - i] = pop();
      }
      if (!args.empty() && args[0].type == ValueType::String) {
        const std::string &fmt = args[0].string_storage;
        std::size_t val_idx = 1;
        for (std::size_t pos = 0; pos < fmt.size(); ++pos) {
          if (pos + 1 < fmt.size() && fmt[pos] == '{' && fmt[pos + 1] == '}') {
            if (val_idx < args.size()) {
              std::cerr << args[val_idx];
              ++val_idx;
            } else {
              std::cerr << "{}";
            }
            ++pos;
          } else {
            std::cerr << fmt[pos];
          }
        }
      } else {
        for (const Value &arg : args) {
          std::cerr << arg;
        }
      }
      if (instruction.op == OpCode::NativeErrLn) {
        std::cerr << '\n';
      }
      std::cerr << std::flush;
      push(Value::null_value());
      break;
    }
    case OpCode::NativeIn:
    case OpCode::NativeInSecret: {
      const uint32_t argc = static_cast<uint32_t>(instruction.operand);
      for (uint32_t i = 0; i < argc; ++i) {
        if (stack_.empty()) {
          return runtime_error("Stack underflow for io::in.");
        }
        Value prompt = pop();
        if (prompt.type == ValueType::String) {
          std::cout << prompt.string_storage << std::flush;
        }
      }
      if (instruction.op == OpCode::NativeInSecret) {
        disable_echo();
      }
      std::string line;
      if (!std::getline(std::cin, line)) {
        push(Value::null_value());
      } else {
        push(Value::string_value(std::move(line)));
      }
      if (instruction.op == OpCode::NativeInSecret) {
        restore_echo();
        std::cout << '\n';
      }
      break;
    }
    case OpCode::NativeFsRead: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (arg_count != 1) {
        return runtime_error("fs::__read expects exactly one argument.");
      }
      if (stack_.empty()) {
        return runtime_error("Stack underflow for fs::__read.");
      }
      Value path = pop();
      if (path.type != ValueType::String) {
        return runtime_error("fs::__read expects a string path.");
      }
      std::ifstream file(path.string_storage, std::ios::binary);
      if (!file) {
        push(Value::null_value());
        break;
      }
      std::ostringstream buffer;
      buffer << file.rdbuf();
      if (file.bad()) {
        push(Value::null_value());
        break;
      }
      push(Value::string_value(buffer.str()));
      break;
    }
    case OpCode::NativeFsWrite: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (arg_count != 2) {
        return runtime_error("fs::__write expects exactly two arguments.");
      }
      if (stack_.size() < 2) {
        return runtime_error("Stack underflow for fs::__write.");
      }
      Value content = pop();
      Value path = pop();
      if (path.type != ValueType::String) {
        return runtime_error("fs::__write expects a string path.");
      }
      if (content.type != ValueType::String) {
        return runtime_error("fs::__write expects string content.");
      }
      std::ofstream file(path.string_storage, std::ios::binary | std::ios::trunc);
      if (file) {
        file.write(content.string_storage.data(),
                   static_cast<std::streamsize>(content.string_storage.size()));
      }
      push(Value::null_value());
      break;
    }
    case OpCode::NativeSysArgs: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (arg_count != 0) {
        return runtime_error("sys::args expects no arguments.");
      }
      std::vector<Value> elements;
      elements.reserve(program_args_.size());
      for (const std::string &arg : program_args_) {
        elements.push_back(Value::string_value(arg));
      }
      push(Value::array_value(std::move(elements)));
      break;
    }
    case OpCode::StructNew: {
      int type_idx = instruction.operand >> 16;
      int field_count = instruction.operand & 0xFFFF;
      if (static_cast<int>(stack_.size()) < field_count) {
        return runtime_error("Stack underflow for struct creation.");
      }
      std::vector<Value> fields(static_cast<std::size_t>(field_count));
      for (int i = field_count - 1; i >= 0; --i) {
        fields[static_cast<std::size_t>(i)] = pop();
      }
      push(Value::struct_value(type_idx, std::move(fields)));
      break;
    }
    case OpCode::FieldGet: {
      if (stack_.empty()) {
        return runtime_error("Stack underflow for field access.");
      }
      Value obj = pop();
      if (obj.type != ValueType::Struct || !obj.struct_storage) {
        return runtime_error("Cannot access field on non-struct value.");
      }
      const std::string &field_name =
          constants[static_cast<std::size_t>(instruction.operand)].string_storage;
      int type_idx = obj.struct_storage->type_index;
      const auto &meta = frame.chunk->struct_metas()[static_cast<std::size_t>(type_idx)];
      int field_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.field_names.size()); ++i) {
        if (meta.field_names[static_cast<std::size_t>(i)] == field_name) {
          field_idx = i;
          break;
        }
      }
      if (field_idx < 0 ||
          static_cast<std::size_t>(field_idx) >= obj.struct_storage->fields.size()) {
        return runtime_error("Unknown field '" + field_name + "'.");
      }
      push(obj.struct_storage->fields[static_cast<std::size_t>(field_idx)]);
      break;
    }
    case OpCode::FieldSet: {
      if (stack_.size() < 2) {
        return runtime_error("Stack underflow for field assignment.");
      }
      Value value = pop();
      Value obj = pop();
      if (obj.type != ValueType::Struct || !obj.struct_storage) {
        return runtime_error("Cannot set field on non-struct value.");
      }
      const std::string &field_name =
          constants[static_cast<std::size_t>(instruction.operand)].string_storage;
      int type_idx = obj.struct_storage->type_index;
      const auto &meta = frame.chunk->struct_metas()[static_cast<std::size_t>(type_idx)];
      int field_idx = -1;
      for (int i = 0; i < static_cast<int>(meta.field_names.size()); ++i) {
        if (meta.field_names[static_cast<std::size_t>(i)] == field_name) {
          field_idx = i;
          break;
        }
      }
      if (field_idx < 0 ||
          static_cast<std::size_t>(field_idx) >= obj.struct_storage->fields.size()) {
        return runtime_error("Unknown field '" + field_name + "'.");
      }
      obj.struct_storage->fields[static_cast<std::size_t>(field_idx)] = value;
      push(obj);
      break;
    }
    case OpCode::EnumVariant: {
      int type_idx = instruction.operand >> 16;
      int variant_idx = instruction.operand & 0xFFFF;
      push(Value::enum_value(type_idx, variant_idx));
      break;
    }
    case OpCode::EnumVariantPayload: {
      int type_idx = instruction.operand >> 16;
      int variant_idx = instruction.operand & 0xFFFF;
      const auto &meta = chunk.enum_metas()[static_cast<std::size_t>(type_idx)];
      int param_count = meta.variant_param_counts[static_cast<std::size_t>(variant_idx)];
      std::vector<Value> payload(static_cast<std::size_t>(param_count));
      for (int i = param_count - 1; i >= 0; --i) {
        payload[static_cast<std::size_t>(i)] = pop();
      }
      push(Value::enum_value_with_payload(type_idx, variant_idx, std::move(payload)));
      break;
    }
    case OpCode::EnumPayloadGet: {
      if (stack_.size() < 1) {
        return runtime_error("Stack underflow for enum payload get.");
      }
      Value enum_val = pop();
      if (enum_val.type != ValueType::Enum) {
        return runtime_error("EnumPayloadGet: value is not an enum.");
      }
      int payload_idx = instruction.operand;
      if (payload_idx < 0 || payload_idx >= static_cast<int>(enum_val.enum_payload.size())) {
        return runtime_error("EnumPayloadGet: payload index out of bounds.");
      }
      push(enum_val.enum_payload[static_cast<std::size_t>(payload_idx)]);
      break;
    }
    case OpCode::ArrayNew: {
      const uint32_t element_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < element_count) {
        return runtime_error("Stack underflow for array creation.");
      }
      std::vector<Value> elements(element_count);
      for (uint32_t i = 0; i < element_count; ++i) {
        elements[element_count - 1 - i] = pop();
      }
      push(Value::array_value(std::move(elements)));
      break;
    }
    case OpCode::IndexGet: {
      if (stack_.size() < 2) {
        return runtime_error("Stack underflow for array indexing.");
      }
      Value index = pop();
      Value object = pop();
      if (object.type == ValueType::Map) {
        if (!object.map_storage) {
          return runtime_error("Cannot index null map.");
        }
        auto it = object.map_storage->entries.find(encode_map_key(index));
        if (it == object.map_storage->entries.end()) {
          push(Value::null_value());
        } else {
          push(it->second.value);
        }
        break;
      }
      if (object.type == ValueType::String) {
        if (index.type != ValueType::Int) {
          return runtime_error("String index must be an integer.");
        }
        if (index.int_value_storage < 0 ||
            static_cast<std::size_t>(index.int_value_storage) >= object.string_storage.size()) {
          return runtime_error("String index out of bounds.");
        }
        push(Value::string_value(
            std::string(1, object.string_storage[static_cast<std::size_t>(index.int_value_storage)])));
        break;
      }
      if (object.type != ValueType::Array || !object.array_storage) {
        return runtime_error("Cannot index non-array value.");
      }
      if (index.type != ValueType::Int) {
        return runtime_error("Array index must be an integer.");
      }
      if (index.int_value_storage < 0 ||
          static_cast<std::size_t>(index.int_value_storage) >= object.array_storage->elements.size()) {
        return runtime_error("Array index out of bounds.");
      }
      push(object.array_storage->elements[static_cast<std::size_t>(index.int_value_storage)]);
      break;
    }
    case OpCode::IndexSet: {
      if (stack_.size() < 3) {
        return runtime_error("Stack underflow for array assignment.");
      }
      Value value = pop();
      Value index = pop();
      Value array = pop();
      if (array.type == ValueType::Map) {
        if (!array.map_storage) {
          return runtime_error("Cannot assign on null map.");
        }
        std::string ek = encode_map_key(index);
        if (array.map_storage->entries.find(ek) == array.map_storage->entries.end()) {
          array.map_storage->order.push_back(ek);
        }
        array.map_storage->entries[ek] = MapEntry{index, value};
        push(value);
        break;
      }
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot assign indexed value on non-array value.");
      }
      if (index.type != ValueType::Int) {
        return runtime_error("Array index must be an integer.");
      }
      if (index.int_value_storage < 0 ||
          static_cast<std::size_t>(index.int_value_storage) >= array.array_storage->elements.size()) {
        return runtime_error("Array index out of bounds.");
      }
      array.array_storage->elements[static_cast<std::size_t>(index.int_value_storage)] = value;
      push(value);
      break;
    }
    case OpCode::ArrayLen: {
      Value obj = pop();
      if (obj.type == ValueType::String) {
        push(Value::int_value(static_cast<int64_t>(obj.string_storage.size())));
        break;
      }
      if (obj.type == ValueType::Map && obj.map_storage) {
        push(Value::int_value(static_cast<int64_t>(obj.map_storage->order.size())));
        break;
      }
      if (obj.type != ValueType::Array || !obj.array_storage) {
        return runtime_error("Cannot call len() on non-array value.");
      }
      push(Value::int_value(static_cast<int64_t>(obj.array_storage->elements.size())));
      break;
    }
    case OpCode::ArrayPush: {
      Value value = pop();
      Value array = pop();
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call push() on non-array value.");
      }
      array.array_storage->elements.push_back(value);
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayResize: {
      Value default_value = pop();
      Value count = pop();
      Value array = pop();
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call resize() on non-array value.");
      }
      if (count.type != ValueType::Int) {
        return runtime_error("resize() count must be an integer.");
      }
      if (count.int_value_storage < 0) {
        return runtime_error("resize() count must be non-negative.");
      }
      array.array_storage->elements.resize(
          static_cast<std::size_t>(count.int_value_storage), default_value);
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayPop: {
      Value array = pop();
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call pop() on non-array value.");
      }
      if (array.array_storage->elements.empty()) {
        return runtime_error("Cannot pop from empty array.");
      }
      Value last = array.array_storage->elements.back();
      array.array_storage->elements.pop_back();
      push(last);
      break;
    }
    case OpCode::ArrayRemove: {
      Value index = pop();
      Value array = pop();
      if (array.type == ValueType::Map) {
        // Map remove(key): delete the entry, return null. (Array remove returns
        // the removed element, but map remove has no positional element.)
        if (!array.map_storage) {
          return runtime_error("Cannot call remove() on null map.");
        }
        std::string ek = encode_map_key(index);
        auto it = array.map_storage->entries.find(ek);
        if (it != array.map_storage->entries.end()) {
          array.map_storage->entries.erase(it);
          auto &order = array.map_storage->order;
          order.erase(std::remove(order.begin(), order.end(), ek), order.end());
        }
        push(Value::null_value());
        break;
      }
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call remove() on non-array value.");
      }
      if (index.type != ValueType::Int) {
        return runtime_error("remove() index must be an integer.");
      }
      auto idx = index.int_value_storage;
      if (idx < 0 || static_cast<std::size_t>(idx) >= array.array_storage->elements.size()) {
        return runtime_error("remove() index out of bounds.");
      }
      Value removed = array.array_storage->elements[static_cast<std::size_t>(idx)];
      array.array_storage->elements.erase(
          array.array_storage->elements.begin() + idx);
      push(removed);
      break;
    }
    case OpCode::ArrayContains: {
      Value needle = pop();
      Value obj = pop();
      if (obj.type == ValueType::String) {
        if (needle.type != ValueType::String) {
          return runtime_error("contains() argument must be a string.");
        }
        push(Value::bool_value(obj.string_storage.find(needle.string_storage) != std::string::npos));
        break;
      }
      if (obj.type != ValueType::Array || !obj.array_storage) {
        return runtime_error("Cannot call contains() on non-array value.");
      }
      bool found = false;
      for (const auto &elem : obj.array_storage->elements) {
        if (elem.type == needle.type) {
          if (elem.type == ValueType::Int && elem.int_value_storage == needle.int_value_storage) {
            found = true; break;
          }
          if (elem.type == ValueType::String && elem.string_storage == needle.string_storage) {
            found = true; break;
          }
          if (elem.type == ValueType::Bool && elem.bool_value_storage == needle.bool_value_storage) {
            found = true; break;
          }
          if (elem.type == ValueType::Double && elem.double_value_storage == needle.double_value_storage) {
            found = true; break;
          }
        }
      }
      push(Value::bool_value(found));
      break;
    }
    case OpCode::ArrayClear: {
      Value array = pop();
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call clear() on non-array value.");
      }
      array.array_storage->elements.clear();
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayInsert: {
      Value value = pop();
      Value index = pop();
      Value array = pop();
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call insert() on non-array value.");
      }
      if (index.type != ValueType::Int) {
        return runtime_error("insert() index must be an integer.");
      }
      auto idx = index.int_value_storage;
      auto &elems = array.array_storage->elements;
      if (idx < 0 || static_cast<std::size_t>(idx) > elems.size()) {
        return runtime_error("insert() index out of bounds.");
      }
      if (value.type == ValueType::Array && value.array_storage) {
        elems.insert(elems.begin() + idx,
                     value.array_storage->elements.begin(),
                     value.array_storage->elements.end());
      } else {
        elems.insert(elems.begin() + idx, value);
      }
      push(Value::null_value());
      break;
    }
    case OpCode::ArrayIndexOf: {
      Value needle = pop();
      Value obj = pop();
      if (obj.type == ValueType::String) {
        if (needle.type != ValueType::String) {
          return runtime_error("index_of() argument must be a string.");
        }
        auto pos = obj.string_storage.find(needle.string_storage);
        push(Value::int_value(pos == std::string::npos ? -1 : static_cast<int64_t>(pos)));
        break;
      }
      if (obj.type != ValueType::Array || !obj.array_storage) {
        return runtime_error("Cannot call index_of() on non-array value.");
      }
      int64_t found = -1;
      for (std::size_t i = 0; i < obj.array_storage->elements.size(); ++i) {
        const auto &elem = obj.array_storage->elements[i];
        if (elem.type == needle.type) {
          bool match = false;
          if (elem.type == ValueType::Int && elem.int_value_storage == needle.int_value_storage) match = true;
          if (elem.type == ValueType::String && elem.string_storage == needle.string_storage) match = true;
          if (elem.type == ValueType::Bool && elem.bool_value_storage == needle.bool_value_storage) match = true;
          if (elem.type == ValueType::Double && elem.double_value_storage == needle.double_value_storage) match = true;
          if (match) { found = static_cast<int64_t>(i); break; }
        }
      }
      push(Value::int_value(found));
      break;
    }
    case OpCode::ArraySlice: {
      Value end_val = pop();
      Value start_val = pop();
      Value obj = pop();
      if (start_val.type != ValueType::Int || end_val.type != ValueType::Int) {
        return runtime_error("slice() arguments must be integers.");
      }
      auto start = start_val.int_value_storage;
      auto end = end_val.int_value_storage;
      if (obj.type == ValueType::String) {
        auto len = static_cast<int64_t>(obj.string_storage.size());
        if (start < 0) start = 0;
        if (end > len) end = len;
        if (start >= end) {
          push(Value::string_value(""));
          break;
        }
        push(Value::string_value(obj.string_storage.substr(
            static_cast<std::size_t>(start),
            static_cast<std::size_t>(end - start))));
        break;
      }
      if (obj.type != ValueType::Array || !obj.array_storage) {
        return runtime_error("Cannot call slice() on non-array value.");
      }
      auto &elems = obj.array_storage->elements;
      if (start < 0) start = 0;
      if (end > static_cast<int64_t>(elems.size())) end = static_cast<int64_t>(elems.size());
      if (start >= end) {
        push(Value::array_value({}));
        break;
      }
      std::vector<Value> slice_elems(elems.begin() + start, elems.begin() + end);
      push(Value::array_value(std::move(slice_elems)));
      break;
    }
    case OpCode::ArrayReverse: {
      Value array = pop();
      if (array.type != ValueType::Array || !array.array_storage) {
        return runtime_error("Cannot call reverse() on non-array value.");
      }
      std::reverse(array.array_storage->elements.begin(),
                   array.array_storage->elements.end());
      push(Value::null_value());
      break;
    }
    case OpCode::MapNew: {
      const uint32_t pair_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < pair_count * 2) {
        return runtime_error("Stack underflow for map literal.");
      }
      Value map = Value::map_value();
      // Pairs were pushed key,value left-to-right; pop in reverse and insert so
      // the original textual order is preserved in `order`.
      std::vector<std::pair<Value, Value>> pairs(pair_count);
      for (uint32_t i = 0; i < pair_count; ++i) {
        Value v = pop();
        Value k = pop();
        pairs[pair_count - 1 - i] = {std::move(k), std::move(v)};
      }
      for (auto &[k, v] : pairs) {
        std::string ek = encode_map_key(k);
        if (map.map_storage->entries.find(ek) == map.map_storage->entries.end()) {
          map.map_storage->order.push_back(ek);
        }
        map.map_storage->entries[ek] = MapEntry{std::move(k), std::move(v)};
      }
      push(std::move(map));
      break;
    }
    case OpCode::MapGet: {
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.map_storage) {
        return runtime_error("Cannot index non-map value.");
      }
      auto it = map.map_storage->entries.find(encode_map_key(key));
      if (it == map.map_storage->entries.end()) {
        push(Value::null_value());
      } else {
        push(it->second.value);
      }
      break;
    }
    case OpCode::MapSet: {
      Value value = pop();
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.map_storage) {
        return runtime_error("Cannot assign on non-map value.");
      }
      std::string ek = encode_map_key(key);
      if (map.map_storage->entries.find(ek) == map.map_storage->entries.end()) {
        map.map_storage->order.push_back(ek);
      }
      map.map_storage->entries[ek] = MapEntry{std::move(key), value};
      push(Value::null_value());
      break;
    }
    case OpCode::MapHas: {
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.map_storage) {
        return runtime_error("Cannot call has() on non-map value.");
      }
      bool found = map.map_storage->entries.count(encode_map_key(key)) != 0;
      push(Value::bool_value(found));
      break;
    }
    case OpCode::MapRemove: {
      Value key = pop();
      Value map = pop();
      if (map.type != ValueType::Map || !map.map_storage) {
        return runtime_error("Cannot call remove() on non-map value.");
      }
      std::string ek = encode_map_key(key);
      auto it = map.map_storage->entries.find(ek);
      if (it != map.map_storage->entries.end()) {
        map.map_storage->entries.erase(it);
        auto &order = map.map_storage->order;
        order.erase(std::remove(order.begin(), order.end(), ek), order.end());
      }
      push(Value::null_value());
      break;
    }
    case OpCode::MapKeys: {
      Value map = pop();
      if (map.type != ValueType::Map || !map.map_storage) {
        return runtime_error("Cannot call keys() on non-map value.");
      }
      std::vector<Value> keys;
      keys.reserve(map.map_storage->order.size());
      for (const std::string &ek : map.map_storage->order) {
        keys.push_back(map.map_storage->entries.at(ek).key);
      }
      push(Value::array_value(std::move(keys)));
      break;
    }
    case OpCode::MapLen: {
      Value map = pop();
      if (map.type != ValueType::Map || !map.map_storage) {
        return runtime_error("Cannot call len() on non-map value.");
      }
      push(Value::int_value(static_cast<int64_t>(map.map_storage->order.size())));
      break;
    }
    case OpCode::StringStartsWith: {
      Value prefix = pop();
      Value str = pop();
      if (str.type != ValueType::String || prefix.type != ValueType::String) {
        return runtime_error("starts_with() requires string arguments.");
      }
      bool result = str.string_storage.size() >= prefix.string_storage.size() &&
                    str.string_storage.compare(0, prefix.string_storage.size(), prefix.string_storage) == 0;
      push(Value::bool_value(result));
      break;
    }
    case OpCode::StringEndsWith: {
      Value suffix = pop();
      Value str = pop();
      if (str.type != ValueType::String || suffix.type != ValueType::String) {
        return runtime_error("ends_with() requires string arguments.");
      }
      bool result = str.string_storage.size() >= suffix.string_storage.size() &&
                    str.string_storage.compare(
                        str.string_storage.size() - suffix.string_storage.size(),
                        suffix.string_storage.size(), suffix.string_storage) == 0;
      push(Value::bool_value(result));
      break;
    }
    case OpCode::StringReplace: {
      Value new_str = pop();
      Value old_str = pop();
      Value str = pop();
      if (str.type != ValueType::String || old_str.type != ValueType::String ||
          new_str.type != ValueType::String) {
        return runtime_error("replace() requires string arguments.");
      }
      std::string result = str.string_storage;
      if (!old_str.string_storage.empty()) {
        std::size_t pos = 0;
        while ((pos = result.find(old_str.string_storage, pos)) != std::string::npos) {
          result.replace(pos, old_str.string_storage.size(), new_str.string_storage);
          pos += new_str.string_storage.size();
        }
      }
      push(Value::string_value(std::move(result)));
      break;
    }
    case OpCode::StringSplit: {
      Value delim = pop();
      Value str = pop();
      if (str.type != ValueType::String || delim.type != ValueType::String) {
        return runtime_error("split() requires string arguments.");
      }
      std::vector<Value> parts;
      if (delim.string_storage.empty()) {
        for (char c : str.string_storage) {
          parts.push_back(Value::string_value(std::string(1, c)));
        }
      } else {
        std::size_t start = 0;
        std::size_t pos;
        while ((pos = str.string_storage.find(delim.string_storage, start)) != std::string::npos) {
          parts.push_back(Value::string_value(str.string_storage.substr(start, pos - start)));
          start = pos + delim.string_storage.size();
        }
        parts.push_back(Value::string_value(str.string_storage.substr(start)));
      }
      push(Value::array_value(std::move(parts)));
      break;
    }
    case OpCode::StringTrim: {
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("trim() requires a string.");
      }
      auto &s = str.string_storage;
      auto start = s.find_first_not_of(" \t\n\r");
      if (start == std::string::npos) {
        push(Value::string_value(""));
      } else {
        auto end = s.find_last_not_of(" \t\n\r");
        push(Value::string_value(s.substr(start, end - start + 1)));
      }
      break;
    }
    case OpCode::StringToUpper: {
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("to_upper() requires a string.");
      }
      std::string result = str.string_storage;
      for (auto &c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      push(Value::string_value(std::move(result)));
      break;
    }
    case OpCode::StringToLower: {
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("to_lower() requires a string.");
      }
      std::string result = str.string_storage;
      for (auto &c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      push(Value::string_value(std::move(result)));
      break;
    }
    case OpCode::StringToInt: {
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("to_int() requires a string.");
      }
      const std::string &s = str.string_storage;
      if (s.empty()) { push(Value::null_value()); break; }
      char *end = nullptr;
      long long v = std::strtoll(s.c_str(), &end, 10);
      if (end == s.c_str() || *end != '\0') {
        push(Value::null_value());
      } else {
        push(Value::int_value(static_cast<int64_t>(v)));
      }
      break;
    }
    case OpCode::StringToFloat: {
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("to_float() requires a string.");
      }
      const std::string &s = str.string_storage;
      if (s.empty()) { push(Value::null_value()); break; }
      char *end = nullptr;
      double v = std::strtod(s.c_str(), &end);
      if (end == s.c_str() || *end != '\0') {
        push(Value::null_value());
      } else {
        push(Value::double_value(v));
      }
      break;
    }
    case OpCode::StringCode: {
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("code() requires a string.");
      }
      if (str.string_storage.empty()) {
        push(Value::int_value(-1));
      } else {
        push(Value::int_value(static_cast<int64_t>(
            static_cast<unsigned char>(str.string_storage[0]))));
      }
      break;
    }
    case OpCode::StringCodeAt: {
      Value idx = pop();
      Value str = pop();
      if (str.type != ValueType::String) {
        return runtime_error("code_at() requires a string.");
      }
      if (idx.type != ValueType::Int) {
        return runtime_error("code_at() index must be an integer.");
      }
      int64_t i = idx.int_value_storage;
      if (i < 0 || static_cast<std::size_t>(i) >= str.string_storage.size()) {
        push(Value::int_value(-1));
      } else {
        push(Value::int_value(static_cast<int64_t>(
            static_cast<unsigned char>(str.string_storage[static_cast<std::size_t>(i)]))));
      }
      break;
    }
    }
  }

  return VmResult{.ok = true, .value = Value::null_value(), .error = ""};
}

bool Vm::push(Value value) {
  stack_.push_back(value);
  return true;
}

Value Vm::pop() {
  Value value = stack_.back();
  stack_.pop_back();
  return value;
}

VmResult Vm::runtime_error(std::string message) const {
  return VmResult{.ok = false, .value = Value::null_value(), .error = std::move(message)};
}

bool Vm::binary_numeric(OpCode op, std::string *error) {
  if (stack_.size() < 2) {
    *error = "Stack underflow.";
    return false;
  }

  Value right = pop();
  Value left = pop();
  if (!left.is_number() || !right.is_number()) {
    *error = "Operands must be numeric.";
    return false;
  }

  const bool both_int = left.type == ValueType::Int && right.type == ValueType::Int;
  if (both_int) {
    switch (op) {
    case OpCode::Add:
      push(Value::int_value(left.int_value_storage + right.int_value_storage));
      return true;
    case OpCode::Subtract:
      push(Value::int_value(left.int_value_storage - right.int_value_storage));
      return true;
    case OpCode::Multiply:
      push(Value::int_value(left.int_value_storage * right.int_value_storage));
      return true;
    case OpCode::Divide:
      if (right.int_value_storage == 0) {
        *error = "Division by zero.";
        return false;
      }
      push(Value::int_value(left.int_value_storage / right.int_value_storage));
      return true;
    case OpCode::Modulo:
      if (right.int_value_storage == 0) {
        *error = "Modulo by zero.";
        return false;
      }
      push(Value::int_value(left.int_value_storage % right.int_value_storage));
      return true;
    default:
      break;
    }
  }

  const double lhs = left.as_double();
  const double rhs = right.as_double();
  switch (op) {
  case OpCode::Add:
    push(Value::double_value(lhs + rhs));
    return true;
  case OpCode::Subtract:
    push(Value::double_value(lhs - rhs));
    return true;
  case OpCode::Multiply:
    push(Value::double_value(lhs * rhs));
    return true;
  case OpCode::Divide:
    push(Value::double_value(lhs / rhs));
    return true;
  case OpCode::Modulo:
    push(Value::double_value(std::fmod(lhs, rhs)));
    return true;
  default:
    *error = "Invalid numeric opcode.";
    return false;
  }
}

void Vm::disable_echo() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(h, &mode);
  SetConsoleMode(h, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
#else
  struct termios t;
  tcgetattr(STDIN_FILENO, &t);
  t.c_lflag &= ~static_cast<tcflag_t>(ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
}

void Vm::restore_echo() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  GetConsoleMode(h, &mode);
  SetConsoleMode(h, mode | ENABLE_ECHO_INPUT);
#else
  struct termios t;
  tcgetattr(STDIN_FILENO, &t);
  t.c_lflag |= ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif
}

} // namespace kinglet
