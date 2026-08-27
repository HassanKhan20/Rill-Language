#include "parser.hpp"

#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "compiler.hpp"
#include "object.hpp"

namespace rill {

namespace {

using ParseFn = void (*)(bool canAssign);

struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  Prec precedence;
};

const ParseRule* getRule(TokenType type);

void number(bool) {
  double value = std::strtod(parser.previous.start, nullptr);
  emitConstant(numberValue(value));
}

void string(bool) {
  // Trim the surrounding quotes.
  emitConstant(objValue(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

void literal(bool) {
  switch (parser.previous.type) {
    case TokenType::False: emitOp(OpCode::False); break;
    case TokenType::True:  emitOp(OpCode::True); break;
    case TokenType::Nil:   emitOp(OpCode::Nil); break;
    default: return;  // Unreachable.
  }
}

void grouping(bool) {
  expression();
  consume(TokenType::RightParen, "expected ')' after expression");
}

void unary(bool) {
  TokenType op = parser.previous.type;
  parsePrecedence(Prec::Unary);
  switch (op) {
    case TokenType::Minus: emitOp(OpCode::Negate); break;
    case TokenType::Bang:  emitOp(OpCode::Not); break;
    default: return;  // Unreachable.
  }
}

void binary(bool) {
  TokenType op = parser.previous.type;
  const ParseRule* rule = getRule(op);
  // Recursing one level tighter makes binary operators left-associative.
  parsePrecedence(static_cast<Prec>(static_cast<int>(rule->precedence) + 1));

  switch (op) {
    case TokenType::Plus:    emitOp(OpCode::Add); break;
    case TokenType::Minus:   emitOp(OpCode::Subtract); break;
    case TokenType::Star:    emitOp(OpCode::Multiply); break;
    case TokenType::Slash:   emitOp(OpCode::Divide); break;
    case TokenType::Percent: emitOp(OpCode::Modulo); break;

    case TokenType::EqualEqual: emitOp(OpCode::Equal); break;
    case TokenType::Greater:    emitOp(OpCode::Greater); break;
    case TokenType::Less:       emitOp(OpCode::Less); break;
    // The negated forms reuse their positive counterpart plus Not, which
    // keeps the opcode set smaller at the cost of one extra instruction.
    case TokenType::BangEqual:    emitOps(OpCode::Equal, OpCode::Not); break;
    case TokenType::GreaterEqual: emitOps(OpCode::Less, OpCode::Not); break;
    case TokenType::LessEqual:    emitOps(OpCode::Greater, OpCode::Not); break;
    default: return;  // Unreachable.
  }
}

// The set of names bound with `let`. Immutability is enforced here, at
// compile time, rather than by a runtime flag on the value.
std::set<std::string> g_immutableGlobals;

std::string lexemeOf(const Token& t) {
  return std::string(t.start, static_cast<size_t>(t.length));
}

uint8_t identifierConstant(Token name) {
  return makeConstant(objValue(copyString(name.start, name.length)));
}

// `name` is deliberately by value: it usually aliases parser.previous, and
// match() below advances the token stream, which would overwrite a reference.
void namedVariable(Token name, bool canAssign) {
  bool localIsMutable = false;
  int slot = resolveLocal(current, name, &localIsMutable);
  int upvalue = slot == -1 ? resolveUpvalue(current, name, &localIsMutable) : -1;

  OpCode getOp, setOp;
  uint8_t arg;
  bool isMutable;
  if (slot != -1) {
    getOp = OpCode::GetLocal;
    setOp = OpCode::SetLocal;
    arg = static_cast<uint8_t>(slot);
    isMutable = localIsMutable;
  } else if (upvalue != -1) {
    getOp = OpCode::GetUpvalue;
    setOp = OpCode::SetUpvalue;
    arg = static_cast<uint8_t>(upvalue);
    isMutable = localIsMutable;
  } else {
    getOp = OpCode::GetGlobal;
    setOp = OpCode::SetGlobal;
    arg = identifierConstant(name);
    isMutable = g_immutableGlobals.count(lexemeOf(name)) == 0;
  }

  if (canAssign && match(TokenType::Equal)) {
    if (!isMutable) {
      std::string msg =
          "cannot assign to immutable binding '" + lexemeOf(name) + "'";
      error(msg.c_str());
      return;
    }
    expression();
    emitOpArg(setOp, arg);
  } else {
    emitOpArg(getOp, arg);
  }
}

void identifier(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

// Function literals are anonymous, but a function bound straight to a name is
// almost always thought of by that name, so `declaration` leaves the binding
// name here for the literal to adopt. It exists purely for error messages.
const Token* g_pendingFnName = nullptr;

// A function literal. The body is a block, so its final expression is the
// return value; `return` exists for early exit.
void fnExpr(bool) {
  const Token* inferredName = g_pendingFnName;
  g_pendingFnName = nullptr;

  Compiler compiler;
  initCompiler(&compiler, FunctionType::Function);

  if (inferredName != nullptr) {
    current->function->name =
        copyString(inferredName->start, inferredName->length);
  } else {
    current->function->name = copyString("<anonymous>", 11);
  }

  consume(TokenType::LeftParen, "expected '(' after 'fn'");
  if (!check(TokenType::RightParen)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("cannot have more than 255 parameters");
      }
      consume(TokenType::Identifier, "expected a parameter name");
      declareParam(parser.previous);
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightParen, "expected ')' after parameters");

  consume(TokenType::LeftBrace, "expected '{' before function body");
  exprList(TokenType::RightBrace);
  consume(TokenType::RightBrace, "expected '}' after function body");

  ObjFunction* function = endCompiler();

  // A Closure instruction carries the function constant followed by two bytes
  // per upvalue describing where to capture each one from.
  emitOpArg(OpCode::Closure, makeConstant(objValue(function)));
  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

// `return <expr>` or a bare `return` yielding nil.
void returnExpr(bool) {
  int before = stackDepth();
  if (check(TokenType::Semicolon) || check(TokenType::RightBrace)) {
    emitOp(OpCode::Nil);
  } else {
    expression();
  }
  emitOp(OpCode::Return);
  // Control never falls through, but `return` is an expression and the
  // surrounding sequence accounts for a value here.
  setStackDepth(before + 1);
}

uint8_t argumentList() {
  uint8_t count = 0;
  if (!check(TokenType::RightParen)) {
    do {
      expression();
      if (count == 255) error("cannot have more than 255 arguments");
      count++;
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightParen, "expected ')' after arguments");
  return count;
}

void call(bool) { emitCall(argumentList()); }

// `let` and `var` are prefix expressions, not statements: a declaration is an
// expression that yields nil.
void declaration(bool) {
  bool isMutable = parser.previous.type == TokenType::Var;

  consume(TokenType::Identifier, "expected a name after 'let' or 'var'");
  Token name = parser.previous;

  consume(TokenType::Equal, "expected '=' in binding");
  g_pendingFnName = &name;
  expression();
  g_pendingFnName = nullptr;

  if (current->scopeDepth > 0) {
    // A local keeps the slot its initializer just landed on.
    declareLocal(name, isMutable);
  } else {
    if (isMutable) {
      g_immutableGlobals.erase(lexemeOf(name));
    } else {
      g_immutableGlobals.insert(lexemeOf(name));
    }
    emitOpArg(OpCode::DefineGlobal, identifierConstant(name));
  }

  // The declaration's own value.
  emitOp(OpCode::Nil);
}

// A brace-delimited block is an expression whose value is its final
// expression. Assumes the opening brace has already been consumed.
void blockBody() {
  beginScope();
  exprList(TokenType::RightBrace);
  consume(TokenType::RightBrace, "expected '}' after block");
  endScope();
}

void block(bool) { blockBody(); }

// if/else is an expression. Both arms must leave exactly one value, and the
// compiler must reset its depth tracking at the branch point because it emits
// linearly while the VM does not execute linearly.
void ifExpr(bool) {
  int base = stackDepth();

  expression();  // Condition; depth is base + 1.
  int thenJump = emitJump(OpCode::JumpIfFalse);

  emitOp(OpCode::Pop);  // Discard the condition on the taken path.
  consume(TokenType::LeftBrace, "expected '{' after if condition");
  blockBody();  // depth == base + 1
  int elseJump = emitJump(OpCode::Jump);

  patchJump(thenJump);
  // The untaken path arrives here with the condition still on the stack.
  setStackDepth(base + 1);
  emitOp(OpCode::Pop);

  if (match(TokenType::Else)) {
    if (match(TokenType::If)) {
      ifExpr(false);
    } else {
      consume(TokenType::LeftBrace, "expected '{' or 'if' after 'else'");
      blockBody();
    }
  } else {
    // An if without an else yields nil.
    emitOp(OpCode::Nil);
  }

  patchJump(elseJump);
}

// `while` always yields nil. Its body's value is discarded each iteration.
void whileExpr(bool) {
  int base = stackDepth();
  int loopStart = static_cast<int>(currentChunk()->code.size());

  LoopContext loop;
  loop.enclosing = current->loop;
  loop.startOffset = loopStart;
  loop.baseDepth = base;
  current->loop = &loop;

  expression();  // Condition; depth base + 1.
  int exitJump = emitJump(OpCode::JumpIfFalse);
  emitOp(OpCode::Pop);

  consume(TokenType::LeftBrace, "expected '{' after while condition");
  blockBody();          // depth base + 1
  emitOp(OpCode::Pop);  // The body's value is discarded.
  emitLoop(loopStart);

  patchJump(exitJump);
  setStackDepth(base + 1);
  emitOp(OpCode::Pop);  // Discard the condition. depth == base

  // `break` jumps here, with the stack already unwound to base.
  for (int i = 0; i < loop.breakCount; i++) patchJump(loop.breakJumps[i]);

  emitOp(OpCode::Nil);
  current->loop = loop.enclosing;
}

void breakExpr(bool) {
  LoopContext* loop = current->loop;
  if (loop == nullptr) {
    error("'break' outside of a loop");
    return;
  }
  if (loop->breakCount == kUint8Count) {
    error("too many 'break' expressions in one loop");
    return;
  }

  int before = stackDepth();
  emitPopN(before - loop->baseDepth);
  loop->breakJumps[loop->breakCount++] = emitJump(OpCode::Jump);
  // Control never reaches the following code, but `break` is an expression
  // and the surrounding sequence accounts for a value here.
  setStackDepth(before + 1);
}

void continueExpr(bool) {
  LoopContext* loop = current->loop;
  if (loop == nullptr) {
    error("'continue' outside of a loop");
    return;
  }

  int before = stackDepth();
  emitPopN(before - loop->baseDepth);
  emitLoop(loop->startOffset);
  setStackDepth(before + 1);
}

// `and` and `or` yield one of their operands rather than a coerced boolean,
// so the short-circuit path simply leaves the left operand in place.
void andExpr(bool) {
  int endJump = emitJump(OpCode::JumpIfFalse);
  emitOp(OpCode::Pop);
  parsePrecedence(Prec::And);
  patchJump(endJump);
}

void orExpr(bool) {
  int elseJump = emitJump(OpCode::JumpIfFalse);
  int endJump = emitJump(OpCode::Jump);

  patchJump(elseJump);
  emitOp(OpCode::Pop);
  parsePrecedence(Prec::Or);
  patchJump(endJump);
}

// clang-format off
const ParseRule kRules[] = {
    /* LeftParen    */ {grouping, call,    Prec::Call},
    /* RightParen   */ {nullptr,  nullptr, Prec::None},
    /* LeftBrace    */ {block,    nullptr, Prec::None},
    /* RightBrace   */ {nullptr,  nullptr, Prec::None},
    /* Comma        */ {nullptr,  nullptr, Prec::None},
    /* Dot          */ {nullptr,  nullptr, Prec::None},
    /* Semicolon    */ {nullptr,  nullptr, Prec::None},
    /* Colon        */ {nullptr,  nullptr, Prec::None},
    /* Pipe         */ {nullptr,  nullptr, Prec::None},
    /* Plus         */ {nullptr,  binary,  Prec::Term},
    /* Minus        */ {unary,    binary,  Prec::Term},
    /* Star         */ {nullptr,  binary,  Prec::Factor},
    /* Slash        */ {nullptr,  binary,  Prec::Factor},
    /* Percent      */ {nullptr,  binary,  Prec::Factor},
    /* Bang         */ {unary,    nullptr, Prec::None},
    /* BangEqual    */ {nullptr,  binary,  Prec::Equality},
    /* Equal        */ {nullptr,  nullptr, Prec::None},
    /* EqualEqual   */ {nullptr,  binary,  Prec::Equality},
    /* Less         */ {nullptr,  binary,  Prec::Comparison},
    /* LessEqual    */ {nullptr,  binary,  Prec::Comparison},
    /* Greater      */ {nullptr,  binary,  Prec::Comparison},
    /* GreaterEqual */ {nullptr,  binary,  Prec::Comparison},
    /* Arrow        */ {nullptr,  nullptr, Prec::None},
    /* Identifier   */ {identifier, nullptr, Prec::None},
    /* String       */ {string,   nullptr, Prec::None},
    /* Number       */ {number,   nullptr, Prec::None},
    /* Underscore   */ {nullptr,  nullptr, Prec::None},
    /* Let          */ {declaration, nullptr, Prec::None},
    /* Var          */ {declaration, nullptr, Prec::None},
    /* Fn           */ {fnExpr,   nullptr, Prec::None},
    /* If           */ {ifExpr,   nullptr, Prec::None},
    /* Else         */ {nullptr,  nullptr, Prec::None},
    /* While        */ {whileExpr, nullptr, Prec::None},
    /* Match        */ {nullptr,  nullptr, Prec::None},
    /* And          */ {nullptr,  andExpr, Prec::And},
    /* Or           */ {nullptr,  orExpr,  Prec::Or},
    /* True         */ {literal,  nullptr, Prec::None},
    /* False        */ {literal,  nullptr, Prec::None},
    /* Nil          */ {literal,  nullptr, Prec::None},
    /* Return       */ {returnExpr, nullptr, Prec::None},
    /* Break        */ {breakExpr, nullptr, Prec::None},
    /* Continue     */ {continueExpr, nullptr, Prec::None},
    /* Error        */ {nullptr,  nullptr, Prec::None},
    /* Eof          */ {nullptr,  nullptr, Prec::None},
};
// clang-format on

static_assert(sizeof(kRules) / sizeof(kRules[0]) ==
                  static_cast<size_t>(TokenType::Eof) + 1,
              "parse rule table must have one entry per TokenType");

const ParseRule* getRule(TokenType type) {
  return &kRules[static_cast<size_t>(type)];
}

}  // namespace

void parsePrecedence(Prec precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == nullptr) {
    error("expected expression");
    return;
  }

  bool canAssign = precedence <= Prec::Assignment;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TokenType::Equal)) {
    error("invalid assignment target");
  }
}

void expression() { parsePrecedence(Prec::Assignment); }

void exprList(TokenType terminator) {
  // An empty sequence still has to leave a value.
  if (check(terminator) || check(TokenType::Eof)) {
    emitOp(OpCode::Nil);
    return;
  }

  for (;;) {
    expression();
    bool discarded = match(TokenType::Semicolon);

    if (check(terminator) || check(TokenType::Eof)) {
      // A trailing ';' discarded the last value, so the sequence yields nil.
      // The discarded value must actually be popped: leaving it behind strands
      // a temporary between the scope's locals and the block result, which
      // makes CloseScope remove the wrong slots.
      if (discarded) {
        emitOp(OpCode::Pop);
        emitOp(OpCode::Nil);
      }
      return;
    }

    // Another expression follows, so this one's value is thrown away whether
    // or not a ';' said so explicitly.
    emitOp(OpCode::Pop);

    if (parser.hadError && parser.panicMode) return;
  }
}

}  // namespace rill
