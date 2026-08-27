#pragma once

#include "lexer.hpp"

namespace rill {

// Precedence ladder, lowest binding first. parsePrecedence consumes any
// expression whose operators bind at least as tightly as the given level.
enum class Prec {
  None,
  Assignment,  // =
  Or,          // or
  And,         // and
  Equality,    // == !=
  Comparison,  // < > <= >=
  Term,        // + -
  Factor,      // * / %
  Unary,       // ! -
  Call,        // . ()
  Primary,
};

// Parses one expression at the lowest precedence, emitting as it goes.
void expression();

void parsePrecedence(Prec precedence);

// Parses a sequence of expressions up to (but not consuming) `terminator`,
// leaving exactly one value on the stack: the last expression's, or nil if the
// sequence is empty or ends with a discarding ';'.
//
// A ';' is optional punctuation that discards the preceding expression. Two
// expressions may also simply follow one another, in which case the earlier
// one is discarded. The consequence is that a line starting with '-' or '('
// continues the previous line rather than beginning a new expression.
void exprList(TokenType terminator);

}  // namespace rill
