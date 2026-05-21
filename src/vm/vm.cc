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
    case OpCode::Divide: {
      std::string error;
      if (!binary_numeric(instruction.op, &error)) {
        return runtime_error(std::move(error));
      }
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
      // For now, just pop the arguments and return null
      // This will be implemented properly when multi-function support is added
      for (uint32_t i = 0; i < arg_count; ++i) {
        pop();
      }
      pop(); // pop the callee
      push(Value::null_value());
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
        result = left.type == right.type && left.int_value_storage == right.int_value_storage &&
                 left.double_value_storage == right.double_value_storage &&
                 left.bool_value_storage == right.bool_value_storage;
      } else if (instruction.op == OpCode::Neq) {
        result = !(left.type == right.type && left.int_value_storage == right.int_value_storage &&
                   left.double_value_storage == right.double_value_storage &&
                   left.bool_value_storage == right.bool_value_storage);
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
    case OpCode::NativePrint: {
      const uint32_t arg_count = static_cast<uint32_t>(instruction.operand);
      for (uint32_t i = 0; i < arg_count; ++i) {
        if (stack_.empty()) {
          return runtime_error("Stack underflow for print.");
        }
        Value value = pop();
        std::cout << value;
      }
      std::cout << std::flush;
      push(Value::null_value());
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
  default:
    *error = "Invalid numeric opcode.";
    return false;
  }
}

} // namespace kinglet
