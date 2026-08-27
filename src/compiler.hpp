#pragma once

#include <cstdint>

#include "chunk.hpp"
#include "common.hpp"
#include "lexer.hpp"
#include "object.hpp"
#include "value.hpp"

namespace rill {

// Parser state. There is exactly one of these per compilation; parser.cpp
// reads and writes it through the extern below rather than threading it
// through every parse function.
struct Parser {
  Token current;
  Token previous;
  bool hadError = false;
  bool panicMode = false;
  Lexer* lexer = nullptr;
};

extern Parser parser;

// One per enclosing `while`. `break` and `continue` unwind the stack to
// `baseDepth` before jumping, since the loop's entry and exit points both
// expect exactly that depth.
struct LoopContext {
  LoopContext* enclosing = nullptr;
  int startOffset = 0;   // Where `continue` jumps back to.
  int baseDepth = 0;     // Stack depth at loop entry.
  int breakJumps[kUint8Count];
  int breakCount = 0;
};

// A compile-time upvalue: where the enclosing function keeps the captured
// variable. `isLocal` distinguishes a local of the immediately enclosing
// function from an upvalue it in turn captured.
struct Upvalue {
  uint8_t index;
  bool isLocal;
};

struct Local {
  Token name;
  int depth;       // Scope depth, or -1 while the initializer is compiling.
  int slot;        // Absolute stack slot, relative to the frame base.
  bool isCaptured;
  bool isMutable;
};

enum class FunctionType { Script, Function };

// Because blocks are expressions, a block can begin with temporaries already
// on the stack, so a local's slot is NOT its index in `locals`. The compiler
// therefore tracks the stack depth it is emitting at, and a local's slot is
// the depth its initializer landed on.
struct Compiler {
  Compiler* enclosing = nullptr;
  ObjFunction* function = nullptr;
  FunctionType type = FunctionType::Script;
  Local locals[kUint8Count];
  int localCount = 0;
  Upvalue upvalues[kUint8Count];
  int scopeDepth = 0;
  int stackDepth = 0;
  LoopContext* loop = nullptr;

  // Peephole window for constant folding. A single-pass compiler has no tree
  // to fold over, so it instead remembers whether the last one or two
  // instructions it emitted were constant loads, and rewrites them in place
  // when an arithmetic operator immediately follows.
  int pendingConsts = 0;       // 0, 1, or 2
  uint8_t constIndex[2] = {0, 0};
  size_t constCodeStart = 0;   // Offset of the first of the pending loads.
};

extern Compiler* current;

// Compiles a whole program into a synthetic top-level function. Returns
// nullptr if any compile error was reported.
ObjFunction* compile(const char* source);

// Pushes a new function compiler. Slot 0 of every frame is reserved for the
// callee itself, so locals start at slot 1.
void initCompiler(Compiler* compiler, FunctionType type);

// Emits the implicit return and pops the compiler, returning its function.
ObjFunction* endCompiler();

// --- Token stream ---------------------------------------------------------

void advance();
void consume(TokenType type, const char* message);
bool check(TokenType type);
bool match(TokenType type);

// Returns the token after parser.current without consuming anything. `{`
// begins both a block and a map literal, and one extra token is exactly what
// distinguishes them.
Token peekAfterCurrent();

// --- Error reporting ------------------------------------------------------

void errorAt(const Token& token, const char* message);
void error(const char* message);
void errorAtCurrent(const char* message);

// --- Emission -------------------------------------------------------------

Chunk* currentChunk();

// Raw byte emission. These do NOT adjust the tracked stack depth; use the
// opcode-aware helpers below unless you are writing an operand.
void emitByte(uint8_t byte);
void emitBytes(uint8_t a, uint8_t b);

// Emits an opcode and applies its stack effect to the tracked depth.
void emitOp(OpCode op);
void emitOps(OpCode a, OpCode b);

// Emits an opcode plus a one-byte operand, applying the stack effect.
void emitOpArg(OpCode op, uint8_t arg);

void emitConstant(Value value);
uint8_t makeConstant(Value value);

// The compile-time stack depth. Control-flow constructs must save and restore
// this across branches, since the compiler emits linearly but the VM does not
// execute linearly.
int stackDepth();
void setStackDepth(int depth);

// Discards `count` values from the top of the stack.
void emitPopN(int count);

// Emits a call, accounting for the arguments it consumes.
void emitCall(uint8_t argCount);

// Declares a parameter, which arrives on the stack already.
void declareParam(Token name);

// --- Jumps ----------------------------------------------------------------

// Emits a jump with a placeholder offset and returns the offset of that
// placeholder, to be handed to patchJump once the target is known.
int emitJump(OpCode op);
void patchJump(int offset);

// Emits a backward jump to an already-known offset.
void emitLoop(int loopStart);

// --- Scopes and locals ----------------------------------------------------

void beginScope();

// Pops the scope's locals while preserving the value on top of the stack,
// which is the block's result.
void endScope();

// Declares a local in the current scope. Errors on a duplicate name in the
// same scope. The local's slot is the current stack depth minus one, i.e. the
// slot its already-emitted initializer occupies.
void declareLocal(Token name, bool isMutable);

// Declares an immutable local that aliases an existing stack slot rather than
// claiming a new one. Used by match binding patterns, which name the subject
// already sitting on the stack.
void declareLocalAtSlot(Token name, int slot);

// Drops the most recently declared local without emitting anything. The caller
// is responsible for the stack slot it named.
void removeInnermostLocal();

// Returns the slot of a local with this name, or -1. Reports an error if the
// name is found but is still being initialized.
int resolveLocal(Compiler* compiler, Token name, bool* isMutableOut);

// Resolves a name to an upvalue index, adding upvalue entries to every
// compiler between here and the one that owns the local. Returns -1 if the
// name is not found in any enclosing function.
int resolveUpvalue(Compiler* compiler, Token name, bool* isMutableOut);

}  // namespace rill
