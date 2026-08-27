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

  OpCode getOp, setOp;
  uint8_t arg;
  bool isMutable;
  if (slot != -1) {
    getOp = OpCode::GetLocal;
    setOp = OpCode::SetLocal;
    arg = static_cast<uint8_t>(slot);
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

// Temporary: `print` is recognised syntactically until native functions land,
// at which point this becomes an ordinary call and OpCode::Print disappears.
void identifier(bool canAssign) {
  if (parser.previous.length == 5 &&
      std::memcmp(parser.previous.start, "print", 5) == 0) {
    consume(TokenType::LeftParen, "expected '(' after 'print'");
    expression();
    consume(TokenType::RightParen, "expected ')' after argument");
    emitOp(OpCode::Print);
    return;
  }
  namedVariable(parser.previous, canAssign);
}

// `let` and `var` are prefix expressions, not statements: a declaration is an
// expression that yields nil.
void declaration(bool) {
  bool isMutable = parser.previous.type == TokenType::Var;

  consume(TokenType::Identifier, "expected a name after 'let' or 'var'");
  Token name = parser.previous;

  consume(TokenType::Equal, "expected '=' in binding");
  expression();

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
// expression.
void block(bool) {
  beginScope();
  exprList(TokenType::RightBrace);
  consume(TokenType::RightBrace, "expected '}' after block");
  endScope();
}

// clang-format off
const ParseRule kRules[] = {
    /* LeftParen    */ {grouping, nullptr, Prec::None},
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
    /* Fn           */ {nullptr,  nullptr, Prec::None},
    /* If           */ {nullptr,  nullptr, Prec::None},
    /* Else         */ {nullptr,  nullptr, Prec::None},
    /* While        */ {nullptr,  nullptr, Prec::None},
    /* Match        */ {nullptr,  nullptr, Prec::None},
    /* And          */ {nullptr,  nullptr, Prec::None},
    /* Or           */ {nullptr,  nullptr, Prec::None},
    /* True         */ {literal,  nullptr, Prec::None},
    /* False        */ {literal,  nullptr, Prec::None},
    /* Nil          */ {literal,  nullptr, Prec::None},
    /* Return       */ {nullptr,  nullptr, Prec::None},
    /* Break        */ {nullptr,  nullptr, Prec::None},
    /* Continue     */ {nullptr,  nullptr, Prec::None},
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
      if (discarded) emitOp(OpCode::Nil);
      return;
    }

    // Another expression follows, so this one's value is thrown away whether
    // or not a ';' said so explicitly.
    emitOp(OpCode::Pop);

    if (parser.hadError && parser.panicMode) return;
  }
}

}  // namespace rill
