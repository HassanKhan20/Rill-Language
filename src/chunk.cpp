#include "chunk.hpp"

namespace rill {

void Chunk::write(uint8_t byte, int line) {
  code.push_back(byte);
  if (!lines_.empty() && lines_.back().first == line) {
    lines_.back().second++;
  } else {
    lines_.emplace_back(line, 1);
  }
}

int Chunk::addConstant(Value v) {
  constants.push_back(v);
  return static_cast<int>(constants.size()) - 1;
}

int Chunk::lineAt(size_t offset) const {
  size_t seen = 0;
  for (const auto& run : lines_) {
    seen += static_cast<size_t>(run.second);
    if (offset < seen) return run.first;
  }
  return -1;
}

const char* opCodeName(OpCode op) {
  switch (op) {
    case OpCode::Constant: return "Constant";
    case OpCode::Nil:      return "Nil";
    case OpCode::True:     return "True";
    case OpCode::False:    return "False";
    case OpCode::Pop:      return "Pop";
    case OpCode::PopN:         return "PopN";
    case OpCode::CloseScope:   return "CloseScope";
    case OpCode::DefineGlobal: return "DefineGlobal";
    case OpCode::GetGlobal:    return "GetGlobal";
    case OpCode::SetGlobal:    return "SetGlobal";
    case OpCode::GetLocal:     return "GetLocal";
    case OpCode::SetLocal:     return "SetLocal";
    case OpCode::Negate:   return "Negate";
    case OpCode::Not:      return "Not";
    case OpCode::Add:      return "Add";
    case OpCode::Subtract: return "Subtract";
    case OpCode::Multiply: return "Multiply";
    case OpCode::Divide:   return "Divide";
    case OpCode::Modulo:   return "Modulo";
    case OpCode::Equal:    return "Equal";
    case OpCode::Greater:  return "Greater";
    case OpCode::Less:     return "Less";
    case OpCode::Jump:        return "Jump";
    case OpCode::JumpIfFalse: return "JumpIfFalse";
    case OpCode::Loop:        return "Loop";
    case OpCode::Print:    return "Print";
    case OpCode::Return:   return "Return";
  }
  return "Unknown";
}

}  // namespace rill
