#pragma once

#include "chunk.hpp"
#include "common.hpp"
#include "object.hpp"
#include "table.hpp"
#include "value.hpp"

namespace rill {

enum class InterpretResult { Ok, CompileError, RuntimeError };

struct CallFrame {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;  // Points at the callee's slot 0 within the value stack.
};

class VM {
 public:
  void init();
  void free();

  InterpretResult interpret(const char* source);

  void push(Value value);
  Value pop();

  // Binds a native function to a global name. Used by builtins.cpp.
  void defineNative(const char* name, NativeFn function, int arity);

  // The collector needs to walk the stack, frames, globals, and open
  // upvalues; markVMRoots is the only outside code that may.
  friend void markVMRoots();

 private:
  InterpretResult run();
  Value peek(int distance) const;
  void resetStack();
  void runtimeError(const char* format, ...);
  bool callValue(Value callee, int argCount);
  bool call(ObjClosure* closure, int argCount);

  // Returns the upvalue for `local`, reusing an already-open one for that
  // slot so that closures over the same variable share it.
  ObjUpvalue* captureUpvalue(Value* local);

  // Closes every open upvalue at or above `last`.
  void closeUpvalues(Value* last);

  Table globals_;
  CallFrame frames_[kFramesMax];
  int frameCount_ = 0;
  ObjUpvalue* openUpvalues_ = nullptr;

  // A fixed array, not a vector: open upvalues will hold raw pointers into
  // this storage and must not be invalidated by reallocation.
  Value stack_[kStackMax];
  Value* stackTop_ = stack_;
};

extern VM vm;

// Enumerates the VM's roots for the collector.
void markVMRoots();

}  // namespace rill
