#include "vm/vm.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>

namespace kinglet {

VmResult Vm::run(const Chunk &chunk) {
  stack_.clear();
  frames_.clear();
  frames_.push_back(CallFrame{.chunk = &chunk, .ip = 0});

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
    case OpCode::Call: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count + 1) {
        return runtime_error("Stack underflow for function call.");
      }
      Value callee = pop();
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
        } else {
          result = !(left.int_value_storage == right.int_value_storage &&
                     left.double_value_storage == right.double_value_storage &&
                     left.bool_value_storage == right.bool_value_storage);
        }
      } else {
        if (!left.is_number() || !right.is_number()) {
          return runtime_error("Comparison operands must be numeric.");
        }
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
      push(Value::bool_value(result));
      break;
    }
    case OpCode::NativeOut: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      if (stack_.size() < arg_count) {
        return runtime_error("Stack underflow for io::out.");
      }

      // Pop all args (they're in reverse order on stack)
      std::vector<Value> args(arg_count);
      for (uint32_t i = 0; i < arg_count; ++i) {
        args[arg_count - 1 - i] = pop();
      }

      // First arg is format string, rest are values
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
            ++pos; // skip closing }
          } else {
            std::cout << fmt[pos];
          }
        }
      } else {
        // No format string, just print all args
        for (const Value &arg : args) {
          std::cout << arg;
        }
      }
      std::cout << std::flush;
      push(Value::null_value());
      break;
    }
    case OpCode::NativeErr: {
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
      std::cerr << std::flush;
      push(Value::null_value());
      break;
    }
    case OpCode::NativeIn: {
      const uint32_t argc = static_cast<uint32_t>(instruction.operand);
      // Consume optional prompt argument from stack
      for (uint32_t i = 0; i < argc; ++i) {
        if (stack_.empty()) {
          return runtime_error("Stack underflow for io::in.");
        }
        Value prompt = pop();
        if (prompt.type == ValueType::String) {
          std::cout << prompt.string_storage << std::flush;
        }
      }
      std::string line;
      if (!std::getline(std::cin, line)) {
        push(Value::null_value());
      } else {
        push(Value::string_value(std::move(line)));
      }
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

} // namespace kinglet
