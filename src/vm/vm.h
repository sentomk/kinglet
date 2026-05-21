#pragma once

#include "vm/chunk.h"
#include "vm/value.h"

#include <string>
#include <vector>

namespace kinglet {

struct CallFrame {
  const Chunk *chunk = nullptr;
  std::size_t ip = 0;
  std::vector<Value> locals;
};

struct VmResult {
  bool ok = false;
  Value value = Value::null_value();
  std::string error;
};

class Vm {
public:
  VmResult run(const Chunk &chunk);

private:
  bool push(Value value);
  Value pop();
  VmResult runtime_error(std::string message) const;
  bool binary_numeric(OpCode op, std::string *error);

  std::vector<Value> stack_;
  std::vector<CallFrame> frames_;
};

} // namespace kinglet
