#pragma once

#include "chunk.hpp"
#include "common.hpp"
#include "object.hpp"
#include "recorder.hpp"
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

  // --- Time-travel debugging ---------------------------------------------

  Recorder& recorder() { return recorder_; }

  // Loads a program and runs it to completion with recording on, so the
  // debugger can then walk backwards through it.
  InterpretResult recordProgram(const char* source);

  // Undoes the most recently executed instruction. Returns false at the
  // beginning of the recording.
  bool stepBack();

  // Re-executes exactly one instruction from the current position.
  InterpretResult stepForward();

  // The source line the VM is currently positioned at, or -1.
  int currentLine() const;

  // Prints the current call stack, innermost first.
  void printBacktrace() const;

  // Reads a global by name for the debugger's `print` command.
  bool lookupGlobal(const char* name, Value* out) const;

  // The collector needs to walk the stack, frames, globals, and open
  // upvalues; markVMRoots is the only outside code that may.
  friend void markVMRoots();

 private:
  InterpretResult run(bool singleStep = false);
  Value peek(int distance) const;
  void resetStack();
  void runtimeError(const char* format, ...);
  bool callValue(Value callee, int argCount);
  bool call(ObjClosure* closure, int argCount);
  void noteSlotWrite(Value* target);
  void noteTableWrite(Table* table, ObjString* key);

  // Returns the upvalue for `local`, reusing an already-open one for that
  // slot so that closures over the same variable share it.
  ObjUpvalue* captureUpvalue(Value* local);

  // Closes every open upvalue at or above `last`.
  void closeUpvalues(Value* last);

  Table globals_;
  CallFrame frames_[kFramesMax];
  int frameCount_ = 0;
  ObjUpvalue* openUpvalues_ = nullptr;
  Recorder recorder_;

  // A fixed array, not a vector: open upvalues will hold raw pointers into
  // this storage and must not be invalidated by reallocation.
  Value stack_[kStackMax];
  Value* stackTop_ = stack_;
};

extern VM vm;

// Enumerates the VM's roots for the collector.
void markVMRoots();

}  // namespace rill
