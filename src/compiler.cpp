#include "compiler.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gc.hpp"
#include "parser.hpp"

namespace rill {

Parser parser;
Compiler* current = nullptr;

namespace {

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
    case OpCode::GetLocal0:
    case OpCode::GetLocal1:
    case OpCode::GetLocal2:
    case OpCode::GetLocal3:
    case OpCode::GetUpvalue:
    case OpCode::Closure:
    case OpCode::Dup:
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
    case OpCode::SetProperty:
      return -1;

    // Assignment yields the assigned value, so the operand it consumed is
    // replaced rather than removed. Print likewise yields nil.
    case OpCode::AddConst:
    case OpCode::SetGlobal:
    case OpCode::SetLocal:
    case OpCode::SetUpvalue:
    case OpCode::Swap:
    case OpCode::GetProperty:
    case OpCode::Negate:
    case OpCode::Not:
      return 0;

    // Variable or externally-managed effects: handled at the emission site.
    case OpCode::PopN:
    case OpCode::MakeMap:
    case OpCode::CloseUpvalue:
    case OpCode::CloseScope:
    case OpCode::Jump:
    case OpCode::JumpIfFalse:
    case OpCode::JumpIfTrue:
    case OpCode::Loop:
    case OpCode::Call:
    case OpCode::Return:
      return 0;
  }
  return 0;
}

#ifdef RILL_FOLD
// True for the binary operators whose result is fully determined by two
// numeric constants. Every one of these is exact under IEEE-754 semantics,
// including division and modulo by zero, which produce the same infinity or
// NaN whether computed now or at run time.
bool isFoldableArith(OpCode op) {
  switch (op) {
    case OpCode::Add:
    case OpCode::Subtract:
    case OpCode::Multiply:
    case OpCode::Divide:
    case OpCode::Modulo:
      return true;
    default:
      return false;
  }
}

double applyArith(OpCode op, double a, double b) {
  switch (op) {
    case OpCode::Add:      return a + b;
    case OpCode::Subtract: return a - b;
    case OpCode::Multiply: return a * b;
    case OpCode::Divide:   return a / b;
    case OpCode::Modulo:   return std::fmod(a, b);
    default:               return 0;
  }
}
#endif

bool identifiersEqual(const Token& a, const Token& b) {
  if (a.length != b.length) return false;
  return std::memcmp(a.start, b.start, static_cast<size_t>(a.length)) == 0;
}

}  // namespace

Chunk* currentChunk() { return &current->function->chunk; }

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

Token peekAfterCurrent() {
  // A Lexer is just two pointers and a line counter, so saving and restoring
  // it is a cheap way to get a second token of lookahead without buffering.
  Lexer saved = *parser.lexer;
  Token next = parser.lexer->next();
  *parser.lexer = saved;
  return next;
}

// --- Emission -------------------------------------------------------------

void emitByte(uint8_t byte) {
  currentChunk()->write(byte, parser.previous.line);
}

void emitBytes(uint8_t a, uint8_t b) {
  emitByte(a);
  emitByte(b);
}

#ifdef RILL_FOLD
// Replaces `Constant a; Constant b; <op>` with a single `Constant (a op b)`.
// Returns false if the window does not hold two numeric constants.
bool tryFold(OpCode op) {
  if (current->pendingConsts != 2 || !isFoldableArith(op)) return false;

  Chunk* chunk = currentChunk();
  Value va = chunk->constants[current->constIndex[0]];
  Value vb = chunk->constants[current->constIndex[1]];
  if (!isNumber(va) || !isNumber(vb)) return false;

  double folded = applyArith(op, asNumber(va), asNumber(vb));

  // Un-emit both constant loads. The second constant was appended to the pool
  // most recently, so it is safe to drop; the first may be shared with
  // earlier code and is left in place.
  chunk->rewind(current->constCodeStart);
  if (current->constIndex[1] == chunk->constants.size() - 1) {
    chunk->popConstant();
  }

  current->pendingConsts = 0;
  current->stackDepth -= 2;
  emitConstant(numberValue(folded));
  return true;
}
#endif

#ifdef RILL_SUPEROPS
// Fuses `Constant c; Add` into a single AddConst. Adding a literal is by far
// the most common shape in loop bodies (`i = i + 1`), so it is worth its own
// instruction.
bool tryFuseAddConst(OpCode op) {
  if (op != OpCode::Add || current->pendingConsts != 1) return false;

  Chunk* chunk = currentChunk();
  // The pending load must be the immediately preceding instruction.
  if (chunk->code.size() != current->constCodeStart + 2) return false;

  uint8_t index = current->constIndex[0];
  // `+` also concatenates strings. AddConst only does arithmetic, so a
  // non-numeric constant has to fall through to the general Add.
  if (!isNumber(chunk->constants[index])) return false;

  chunk->rewind(current->constCodeStart);
  current->pendingConsts = 0;
  // Constant pushed one value and Add would have popped one; AddConst does
  // neither, so the net effect on depth is what Add alone would have been.
  current->stackDepth -= 1;
  emitByte(static_cast<uint8_t>(OpCode::AddConst));
  emitByte(index);
  return true;
}
#endif

void emitOp(OpCode op) {
#ifdef RILL_FOLD
  if (tryFold(op)) return;
#endif
#ifdef RILL_SUPEROPS
  if (tryFuseAddConst(op)) return;
#endif
  current->pendingConsts = 0;
  emitByte(static_cast<uint8_t>(op));
  current->stackDepth += stackEffect(op);
}

void emitOps(OpCode a, OpCode b) {
  emitOp(a);
  emitOp(b);
}

void emitOpArg(OpCode op, uint8_t arg) {
  if (op != OpCode::Constant) current->pendingConsts = 0;

#ifdef RILL_SUPEROPS
  if (op == OpCode::GetLocal && arg < 4) {
    emitByte(static_cast<uint8_t>(static_cast<int>(OpCode::GetLocal0) + arg));
    current->stackDepth += 1;
    return;
  }
#endif

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
  uint8_t index = makeConstant(value);

  // Slide the peephole window: two consecutive constant loads are what the
  // folder rewrites.
  if (current->pendingConsts == 0) {
    current->constCodeStart = currentChunk()->code.size();
    current->constIndex[0] = index;
    current->pendingConsts = 1;
  } else if (current->pendingConsts == 1) {
    current->constIndex[1] = index;
    current->pendingConsts = 2;
  } else {
    current->constCodeStart = currentChunk()->code.size();
    current->constIndex[0] = index;
    current->pendingConsts = 1;
  }

  emitByte(static_cast<uint8_t>(OpCode::Constant));
  emitByte(index);
  current->stackDepth += stackEffect(OpCode::Constant);
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
  // A jump target may land between the pending constants, so the window can
  // no longer be assumed contiguous.
  current->pendingConsts = 0;
  emitByte(static_cast<uint8_t>(op));
  emitByte(0xff);
  emitByte(0xff);
  return static_cast<int>(currentChunk()->code.size()) - 2;
}

void patchJump(int offset) {
  current->pendingConsts = 0;
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
  current->pendingConsts = 0;
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
  int lowestCapturedSlot = -1;
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    const Local& local = current->locals[current->localCount - 1];
    if (local.isCaptured) lowestCapturedSlot = local.slot;
    popped++;
    current->localCount--;
  }

  // Any local a closure captured must be lifted off the stack before its slot
  // is reused, or the closure would read a stale or unrelated value.
  if (lowestCapturedSlot != -1) {
    emitOpArg(OpCode::CloseUpvalue,
              static_cast<uint8_t>(lowestCapturedSlot));
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

namespace {

// Adds an upvalue to `compiler`, reusing an existing entry for the same
// source. The reuse matters: two references to the same variable must share
// one upvalue, or assignments through one would be invisible to the other.
int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
  int count = compiler->function->upvalueCount;
  for (int i = 0; i < count; i++) {
    Upvalue* existing = &compiler->upvalues[i];
    if (existing->index == index && existing->isLocal == isLocal) return i;
  }
  if (count == kUint8Count) {
    error("too many captured variables in one function");
    return 0;
  }
  compiler->upvalues[count].isLocal = isLocal;
  compiler->upvalues[count].index = index;
  return compiler->function->upvalueCount++;
}

}  // namespace

int resolveUpvalue(Compiler* compiler, Token name, bool* isMutableOut) {
  if (compiler->enclosing == nullptr) return -1;

  int local = resolveLocal(compiler->enclosing, name, isMutableOut);
  if (local != -1) {
    // Mark the local so the enclosing scope closes it rather than discarding
    // it when the scope ends.
    for (int i = compiler->enclosing->localCount - 1; i >= 0; i--) {
      if (compiler->enclosing->locals[i].slot == local) {
        compiler->enclosing->locals[i].isCaptured = true;
        break;
      }
    }
    return addUpvalue(compiler, static_cast<uint8_t>(local), true);
  }

  // Not a local of the immediate parent: recurse, and thread the capture back
  // down through every intervening function as the recursion unwinds.
  int upvalue = resolveUpvalue(compiler->enclosing, name, isMutableOut);
  if (upvalue != -1) {
    return addUpvalue(compiler, static_cast<uint8_t>(upvalue), false);
  }
  return -1;
}

void declareLocalAtSlot(Token name, int slot) {
  if (current->localCount == kUint8Count) {
    error("too many local bindings in one function");
    return;
  }
  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = current->scopeDepth;
  local->slot = slot;
  local->isCaptured = false;
  local->isMutable = false;
}

void removeInnermostLocal() {
  if (current->localCount > 0) current->localCount--;
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

void declareParam(Token name) {
  // Parameters are already on the stack when the frame starts, so account for
  // the slot before declaring the local that names it.
  current->stackDepth++;
  declareLocal(name, /*isMutable=*/true);
}

void emitCall(uint8_t argCount) {
  emitByte(static_cast<uint8_t>(OpCode::Call));
  emitByte(argCount);
  // The callee and its arguments are replaced by a single result.
  current->stackDepth -= argCount;
}

// --- Function compilers ---------------------------------------------------

void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = newFunction();
  compiler->type = type;
  compiler->localCount = 0;
  compiler->loop = nullptr;
  // The body of a function is already a scope, so `let` inside it makes a
  // local; at the top level scope depth 0 makes `let` define a global.
  compiler->scopeDepth = type == FunctionType::Script ? 0 : 1;

  current = compiler;

  // Slot 0 holds the callee itself and is not addressable by name.
  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->slot = 0;
  local->isCaptured = false;
  local->isMutable = false;
  local->name.start = "";
  local->name.length = 0;
  current->stackDepth = 1;
}

ObjFunction* endCompiler() {
  emitOp(OpCode::Return);
  ObjFunction* function = current->function;
  current = current->enclosing;
  return function;
}

// A function under construction is reachable from nothing but its compiler,
// and compiling it allocates constantly, so the whole compiler chain is a
// root. Missing this is the classic from-scratch-collector bug.
void markCompilerRoots() {
  for (Compiler* compiler = current; compiler != nullptr;
       compiler = compiler->enclosing) {
    markObject(reinterpret_cast<Obj*>(compiler->function));
  }
}

// --- Entry point ----------------------------------------------------------

ObjFunction* compile(const char* source) {
  Lexer lexer(source);
  Compiler compiler;

  parser.lexer = &lexer;
  parser.hadError = false;
  parser.panicMode = false;
  current = nullptr;
  initCompiler(&compiler, FunctionType::Script);

  advance();
  exprList(TokenType::Eof);
  consume(TokenType::Eof, "expected end of input");

  ObjFunction* function = endCompiler();
  parser.lexer = nullptr;
  return parser.hadError ? nullptr : function;
}

}  // namespace rill
