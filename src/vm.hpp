#pragma once

#include "chunk.hpp"
#include "common.hpp"
#include "value.hpp"

namespace rill {

enum class InterpretResult { Ok, CompileError, RuntimeError };

class VM {
 public:
  void init();
  void free();

  InterpretResult interpret(const char* source);

  void push(Value value);
  Value pop();

 private:
  InterpretResult run();
  Value peek(int distance) const;
  void resetStack();
  void runtimeError(const char* format, ...);

  Chunk* chunk_ = nullptr;
  uint8_t* ip_ = nullptr;

  // A fixed array, not a vector: open upvalues will hold raw pointers into
  // this storage and must not be invalidated by reallocation.
  Value stack_[kStackMax];
  Value* stackTop_ = stack_;
};

extern VM vm;

}  // namespace rill
