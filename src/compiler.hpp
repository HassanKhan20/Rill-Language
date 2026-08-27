#pragma once

#include "chunk.hpp"
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

// Compiles source into `chunk`. Returns false if any compile error was
// reported; errors go to stderr.
bool compile(const char* source, Chunk* chunk);

// --- Token stream, used by parser.cpp -------------------------------------

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
void emitByte(uint8_t byte);
void emitBytes(uint8_t a, uint8_t b);
void emitOp(OpCode op);
void emitOps(OpCode a, OpCode b);
void emitConstant(Value value);

// Adds a constant and returns its index, reporting an error if the pool
// overflows a one-byte operand.
uint8_t makeConstant(Value value);

}  // namespace rill
