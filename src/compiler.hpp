#pragma once

#include <cstdint>

#include "chunk.hpp"
#include "common.hpp"
#include "lexer.hpp"
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

struct Local {
  Token name;
  int depth;       // Scope depth, or -1 while the initializer is compiling.
  int slot;        // Absolute stack slot, relative to the frame base.
  bool isCaptured;
  bool isMutable;
};

// Because blocks are expressions, a block can begin with temporaries already
// on the stack, so a local's slot is NOT its index in `locals`. The compiler
// therefore tracks the stack depth it is emitting at, and a local's slot is
// the depth its initializer landed on.
struct Compiler {
  Compiler* enclosing = nullptr;
  Local locals[kUint8Count];
  int localCount = 0;
  int scopeDepth = 0;
  int stackDepth = 0;
};

extern Compiler* current;

bool compile(const char* source, Chunk* chunk);

// --- Token stream ---------------------------------------------------------

void advance();
void consume(TokenType type, const char* message);
bool check(TokenType type);
bool match(TokenType type);

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

// --- Scopes and locals ----------------------------------------------------

void beginScope();

// Pops the scope's locals while preserving the value on top of the stack,
// which is the block's result.
void endScope();

// Declares a local in the current scope. Errors on a duplicate name in the
// same scope. The local's slot is the current stack depth minus one, i.e. the
// slot its already-emitted initializer occupies.
void declareLocal(Token name, bool isMutable);

// Returns the slot of a local with this name, or -1. Reports an error if the
// name is found but is still being initialized.
int resolveLocal(Compiler* compiler, Token name, bool* isMutableOut);

}  // namespace rill
