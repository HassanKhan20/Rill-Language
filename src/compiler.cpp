#include "compiler.hpp"

#include <cstdio>

#include "parser.hpp"

namespace rill {

Parser parser;

namespace {

Chunk* g_compilingChunk = nullptr;

}  // namespace

Chunk* currentChunk() { return g_compilingChunk; }

// --- Error reporting ------------------------------------------------------

void errorAt(const Token& token, const char* message) {
  // Only the first error in a burst is useful; the rest are usually noise
  // from a parser that has lost its place.
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

void emitOp(OpCode op) { emitByte(static_cast<uint8_t>(op)); }

void emitOps(OpCode a, OpCode b) {
  emitOp(a);
  emitOp(b);
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
  emitBytes(static_cast<uint8_t>(OpCode::Constant), makeConstant(value));
}

// --- Entry point ----------------------------------------------------------

bool compile(const char* source, Chunk* chunk) {
  Lexer lexer(source);
  parser.lexer = &lexer;
  parser.hadError = false;
  parser.panicMode = false;
  g_compilingChunk = chunk;

  advance();
  exprList(TokenType::Eof);
  consume(TokenType::Eof, "expected end of input");
  // The program's own value is not observable, so discard it.
  emitOp(OpCode::Pop);
  emitOp(OpCode::Return);

  parser.lexer = nullptr;
  g_compilingChunk = nullptr;
  return !parser.hadError;
}

}  // namespace rill
