#include "debug.hpp"

#include <cstdio>

namespace rill {

namespace {

int simpleInstruction(const char* name, int offset) {
  std::printf("%s\n", name);
  return offset + 1;
}

int constantInstruction(const char* name, const Chunk& chunk, int offset) {
  uint8_t index = chunk.code[static_cast<size_t>(offset) + 1];
  std::printf("%-16s %4d '", name, index);
  printValue(chunk.constants[index]);
  std::printf("'\n");
  return offset + 2;
}

}  // namespace

int disassembleInstruction(const Chunk& chunk, int offset) {
  std::printf("%04d ", offset);

  int line = chunk.lineAt(static_cast<size_t>(offset));
  if (offset > 0 && line == chunk.lineAt(static_cast<size_t>(offset) - 1)) {
    std::printf("   | ");
  } else {
    std::printf("%4d ", line);
  }

  auto op = static_cast<OpCode>(chunk.code[static_cast<size_t>(offset)]);
  switch (op) {
    case OpCode::Constant:
      return constantInstruction("Constant", chunk, offset);
    case OpCode::Nil:
    case OpCode::True:
    case OpCode::False:
    case OpCode::Pop:
    case OpCode::Negate:
    case OpCode::Not:
    case OpCode::Add:
    case OpCode::Subtract:
    case OpCode::Multiply:
    case OpCode::Divide:
    case OpCode::Modulo:
    case OpCode::Equal:
    case OpCode::Greater:
    case OpCode::Less:
    case OpCode::Print:
    case OpCode::Return:
      return simpleInstruction(opCodeName(op), offset);
  }

  std::printf("Unknown opcode %d\n", chunk.code[static_cast<size_t>(offset)]);
  return offset + 1;
}

void disassembleChunk(const Chunk& chunk, const char* name) {
  std::printf("== %s ==\n", name);
  for (int offset = 0; offset < static_cast<int>(chunk.code.size());) {
    offset = disassembleInstruction(chunk, offset);
  }
}

}  // namespace rill
