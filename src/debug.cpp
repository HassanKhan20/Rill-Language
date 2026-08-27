#include "debug.hpp"

#include <cstdio>

#include "object.hpp"

namespace rill {

namespace {

int byteInstruction(const char* name, const Chunk& chunk, int offset) {
  uint8_t slot = chunk.code[static_cast<size_t>(offset) + 1];
  std::printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

int jumpInstruction(const char* name, int sign, const Chunk& chunk,
                    int offset) {
  auto jump = static_cast<uint16_t>(
      (chunk.code[static_cast<size_t>(offset) + 1] << 8) |
      chunk.code[static_cast<size_t>(offset) + 2]);
  std::printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
  return offset + 3;
}

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
    case OpCode::DefineGlobal:
      return constantInstruction("DefineGlobal", chunk, offset);
    case OpCode::GetGlobal:
      return constantInstruction("GetGlobal", chunk, offset);
    case OpCode::SetGlobal:
      return constantInstruction("SetGlobal", chunk, offset);
    case OpCode::GetLocal:
      return byteInstruction("GetLocal", chunk, offset);
    case OpCode::SetLocal:
      return byteInstruction("SetLocal", chunk, offset);
    case OpCode::CloseScope:
      return byteInstruction("CloseScope", chunk, offset);
    case OpCode::PopN:
      return byteInstruction("PopN", chunk, offset);
    case OpCode::Jump:
      return jumpInstruction("Jump", 1, chunk, offset);
    case OpCode::JumpIfFalse:
      return jumpInstruction("JumpIfFalse", 1, chunk, offset);
    case OpCode::Loop:
      return jumpInstruction("Loop", -1, chunk, offset);
    case OpCode::Call:
      return byteInstruction("Call", chunk, offset);
    case OpCode::GetUpvalue:
      return byteInstruction("GetUpvalue", chunk, offset);
    case OpCode::SetUpvalue:
      return byteInstruction("SetUpvalue", chunk, offset);
    case OpCode::CloseUpvalue:
      return byteInstruction("CloseUpvalue", chunk, offset);
    case OpCode::Closure: {
      // A Closure carries two extra bytes per upvalue; skipping them is what
      // keeps the disassembler in sync with the instruction stream.
      offset++;
      uint8_t constant = chunk.code[static_cast<size_t>(offset++)];
      std::printf("%-16s %4d '", "Closure", constant);
      printValue(chunk.constants[constant]);
      std::printf("'\n");

      ObjFunction* function = asFunction(chunk.constants[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk.code[static_cast<size_t>(offset++)];
        int index = chunk.code[static_cast<size_t>(offset++)];
        std::printf("%04d      |                     %s %d\n", offset - 2,
                    isLocal ? "local" : "upvalue", index);
      }
      return offset;
    }
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
