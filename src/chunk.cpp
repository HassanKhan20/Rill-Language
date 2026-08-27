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

void Chunk::rewind(size_t newSize) {
  if (newSize >= code.size()) return;
  size_t toRemove = code.size() - newSize;
  code.resize(newSize);

  while (toRemove > 0 && !lines_.empty()) {
    auto& run = lines_.back();
    if (static_cast<size_t>(run.second) > toRemove) {
      run.second -= static_cast<int>(toRemove);
      toRemove = 0;
    } else {
      toRemove -= static_cast<size_t>(run.second);
      lines_.pop_back();
    }
  }
}

void Chunk::popConstant() {
  if (!constants.empty()) constants.pop_back();
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
    case OpCode::Dup:          return "Dup";
    case OpCode::Swap:         return "Swap";
    case OpCode::PopN:         return "PopN";
    case OpCode::CloseScope:   return "CloseScope";
    case OpCode::DefineGlobal: return "DefineGlobal";
    case OpCode::GetGlobal:    return "GetGlobal";
    case OpCode::SetGlobal:    return "SetGlobal";
    case OpCode::GetLocal:     return "GetLocal";
    case OpCode::GetLocal0:    return "GetLocal0";
    case OpCode::GetLocal1:    return "GetLocal1";
    case OpCode::GetLocal2:    return "GetLocal2";
    case OpCode::GetLocal3:    return "GetLocal3";
    case OpCode::SetLocal:     return "SetLocal";
    case OpCode::GetUpvalue:   return "GetUpvalue";
    case OpCode::SetUpvalue:   return "SetUpvalue";
    case OpCode::CloseUpvalue: return "CloseUpvalue";
    case OpCode::Closure:      return "Closure";
    case OpCode::Negate:   return "Negate";
    case OpCode::Not:      return "Not";
    case OpCode::Add:      return "Add";
    case OpCode::Subtract: return "Subtract";
    case OpCode::Multiply: return "Multiply";
    case OpCode::Divide:   return "Divide";
    case OpCode::Modulo:   return "Modulo";
    case OpCode::AddConst: return "AddConst";
    case OpCode::Equal:    return "Equal";
    case OpCode::Greater:  return "Greater";
    case OpCode::Less:     return "Less";
    case OpCode::Jump:        return "Jump";
    case OpCode::JumpIfFalse: return "JumpIfFalse";
    case OpCode::JumpIfTrue:  return "JumpIfTrue";
    case OpCode::Loop:        return "Loop";
    case OpCode::MakeMap:     return "MakeMap";
    case OpCode::GetProperty: return "GetProperty";
    case OpCode::SetProperty: return "SetProperty";
    case OpCode::Call:     return "Call";
    case OpCode::Return:   return "Return";
  }
  return "Unknown";
}

}  // namespace rill
