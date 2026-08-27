#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "value.hpp"

namespace rill {

enum class OpCode : uint8_t {
  Constant,
  Nil,
  True,
  False,
  Pop,
  DefineGlobal,
  GetGlobal,
  SetGlobal,
  Negate,
  Not,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  Equal,
  Greater,
  Less,
  Print,  // Temporary: replaced by a native function once calls exist.
  Return,
};

const char* opCodeName(OpCode op);

class Chunk {
 public:
  void write(uint8_t byte, int line);

  // Appends a constant and returns its index. Callers must check the index
  // fits in one byte before emitting it as an operand.
  int addConstant(Value v);

  // The source line for the instruction byte at `offset`.
  int lineAt(size_t offset) const;

  // Number of run-length entries, exposed so tests can assert the encoding
  // actually compresses.
  int lineRunCount() const { return static_cast<int>(lines_.size()); }

  std::vector<uint8_t> code;
  std::vector<Value> constants;

 private:
  // (line number, how many consecutive code bytes share it)
  std::vector<std::pair<int, int>> lines_;
};

}  // namespace rill
