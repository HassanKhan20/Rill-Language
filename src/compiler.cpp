#include "compiler.hpp"

#include <cstdio>
#include <cstring>

#include "parser.hpp"

namespace rill {

Parser parser;
Compiler* current = nullptr;

namespace {

Chunk* g_compilingChunk = nullptr;

// How each opcode changes the stack depth. Opcodes carrying a variable effect
// (CloseScope, Call) are handled at their emission site instead.
int stackEffect(OpCode op) {
  switch (op) {
    case OpCode::Constant:
    case OpCode::Nil:
    case OpCode::True:
    case OpCode::False:
    case OpCode::GetGlobal:
    case OpCode::GetLocal:
      return +1;

    case OpCode::Pop:
    case OpCode::DefineGlobal:
    case OpCode::Add:
    case OpCode::Subtract:
    case OpCode::Multiply:
    case OpCode::Divide:
    case OpCode::Modulo:
    case OpCode::Equal:
    case OpCode::Greater:
    case OpCode::Less:
      return -1;

    // Assignment yields the assigned value, so the operand it consumed is
    // replaced rather than removed. Print likewise yields nil.
    case OpCode::SetGlobal:
    case OpCode::SetLocal:
    case OpCode::Negate:
    case OpCode::Not:
    case OpCode::Print:
      return 0;

    // Variable or externally-managed effects: handled at the emission site.
    case OpCode::PopN:
    case OpCode::CloseScope:
    case OpCode::Jump:
    case OpCode::JumpIfFalse:
    case OpCode::Loop:
    case OpCode::Return:
      return 0;
  }
  return 0;
}

bool identifiersEqual(const Token& a, const Token& b) {
  if (a.length != b.length) return false;
  return std::memcmp(a.start, b.start, static_cast<size_t>(a.length)) == 0;
}

}  // namespace

Chunk* currentChunk() { return g_compilingChunk; }

int stackDepth() { return current->stackDepth; }
void setStackDepth(int depth) { current->stackDepth = depth; }

// --- Error reporting ------------------------------------------------------

void errorAt(const Token& token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  std::fprintf(stderr, "[line %d] Error", token.line);
  if (token.type == TokenType::Eof) {
    std::fprintf(stderr, " at end");
  } else if (token.type == TokenType::Error) {
    // The lexeme is the message itself.
  } else {
    std::fprintf(stderr, " at '%.*s'", token.length, token.start);
  }
  std::fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

void error(const char* message) { errorAt(parser.previous, message); }

void errorAtCurrent(const char* message) { errorAt(parser.current, message); }

// --- Token stream ---------------------------------------------------------

void advance() {
  parser.previous = parser.current;
  for (;;) {
    parser.current = parser.lexer->next();
    if (parser.current.type != TokenType::Error) break;
    errorAtCurrent(parser.current.start);
  }
}

void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }
  errorAtCurrent(message);
}

bool check(TokenType type) { return parser.current.type == type; }

bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// --- Emission -------------------------------------------------------------

void emitByte(uint8_t byte) {
  currentChunk()->write(byte, parser.previous.line);
}

void emitBytes(uint8_t a, uint8_t b) {
  emitByte(a);
  emitByte(b);
}

void emitOp(OpCode op) {
  emitByte(static_cast<uint8_t>(op));
  current->stackDepth += stackEffect(op);
}

void emitOps(OpCode a, OpCode b) {
  emitOp(a);
  emitOp(b);
}

void emitOpArg(OpCode op, uint8_t arg) {
  emitByte(static_cast<uint8_t>(op));
  emitByte(arg);
  current->stackDepth += stackEffect(op);
}

uint8_t makeConstant(Value value) {
  int constant = currentChunk()->addConstant(value);
  if (constant > UINT8_MAX) {
    error("too many constants in one chunk");
    return 0;
  }
  return static_cast<uint8_t>(constant);
}

void emitConstant(Value value) {
  emitOpArg(OpCode::Constant, makeConstant(value));
}

void emitPopN(int count) {
  if (count <= 0) return;
  if (count == 1) {
    emitOp(OpCode::Pop);
    return;
  }
  emitByte(static_cast<uint8_t>(OpCode::PopN));
  emitByte(static_cast<uint8_t>(count));
  current->stackDepth -= count;
}

// --- Jumps ----------------------------------------------------------------

int emitJump(OpCode op) {
  emitByte(static_cast<uint8_t>(op));
  emitByte(0xff);
  emitByte(0xff);
  return static_cast<int>(currentChunk()->code.size()) - 2;
}

void patchJump(int offset) {
  // -2 accounts for the two offset bytes the VM has already consumed.
  int jump = static_cast<int>(currentChunk()->code.size()) - offset - 2;
  if (jump > UINT16_MAX) {
    error("too much code to jump over");
    return;
  }
  currentChunk()->code[static_cast<size_t>(offset)] =
      static_cast<uint8_t>((jump >> 8) & 0xff);
  currentChunk()->code[static_cast<size_t>(offset) + 1] =
      static_cast<uint8_t>(jump & 0xff);
}

void emitLoop(int loopStart) {
  emitByte(static_cast<uint8_t>(OpCode::Loop));
  int offset = static_cast<int>(currentChunk()->code.size()) - loopStart + 2;
  if (offset > UINT16_MAX) {
    error("loop body too large");
    return;
  }
  emitByte(static_cast<uint8_t>((offset >> 8) & 0xff));
  emitByte(static_cast<uint8_t>(offset & 0xff));
}

// --- Scopes and locals ----------------------------------------------------

void beginScope() { current->scopeDepth++; }

void endScope() {
  current->scopeDepth--;

  int popped = 0;
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    popped++;
    current->localCount--;
  }

  if (popped > 0) {
    // The block's result is on top; CloseScope removes the locals from
    // underneath it without disturbing it.
    emitOpArg(OpCode::CloseScope, static_cast<uint8_t>(popped));
    current->stackDepth -= popped;
  }
}

void declareLocal(Token name, bool isMutable) {
  if (current->localCount == kUint8Count) {
    error("too many local bindings in one function");
    return;
  }

  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) break;
    if (identifiersEqual(name, local->name)) {
      error("already a binding with this name in this scope");
      return;
    }
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = current->scopeDepth;
  // The initializer has already been emitted, so it occupies the slot just
  // below the current depth.
  local->slot = current->stackDepth - 1;
  local->isCaptured = false;
  local->isMutable = isMutable;
}

int resolveLocal(Compiler* compiler, Token name, bool* isMutableOut) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, local->name)) {
      if (local->depth == -1) {
        error("cannot read a binding in its own initializer");
      }
      if (isMutableOut != nullptr) *isMutableOut = local->isMutable;
      return local->slot;
    }
  }
  return -1;
}

// --- Entry point ----------------------------------------------------------

bool compile(const char* source, Chunk* chunk) {
  Lexer lexer(source);
  Compiler compiler;

  parser.lexer = &lexer;
  parser.hadError = false;
  parser.panicMode = false;
  current = &compiler;
  g_compilingChunk = chunk;

  advance();
  exprList(TokenType::Eof);
  consume(TokenType::Eof, "expected end of input");
  // The program's own value is not observable, so discard it.
  emitOp(OpCode::Pop);
  emitOp(OpCode::Return);

  parser.lexer = nullptr;
  current = nullptr;
  g_compilingChunk = nullptr;
  return !parser.hadError;
}

}  // namespace rill
