#pragma once

namespace rill {

enum class TokenType {
  // Single character.
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  Comma,
  Dot,
  Semicolon,
  Colon,
  Pipe,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  // One or two characters.
  Bang,
  BangEqual,
  Equal,
  EqualEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Arrow,
  // Literals.
  Identifier,
  String,
  Number,
  Underscore,
  // Keywords.
  Let,
  Var,
  Fn,
  If,
  Else,
  While,
  Match,
  And,
  Or,
  True,
  False,
  Nil,
  Return,
  Break,
  Continue,
  // Bookkeeping.
  Error,
  Eof,
};

// A token borrows its lexeme from the source buffer, which the caller owns and
// must keep alive for as long as any token derived from it is in use. Error
// tokens are the exception: `start` points at a static message instead.
struct Token {
  TokenType type = TokenType::Eof;
  const char* start = nullptr;
  int length = 0;
  int line = 1;
};

const char* tokenTypeName(TokenType t);

class Lexer {
 public:
  explicit Lexer(const char* source)
      : start_(source), current_(source), line_(1) {}

  Token next();

 private:
  bool isAtEnd() const { return *current_ == '\0'; }
  char advance() { return *current_++; }
  char peek() const { return *current_; }
  char peekNext() const { return isAtEnd() ? '\0' : current_[1]; }
  bool match(char expected);
  void skipWhitespace();
  Token make(TokenType type) const;
  Token errorToken(const char* message) const;
  Token string();
  Token number();
  Token identifier();
  TokenType identifierType() const;

  const char* start_;
  const char* current_;
  int line_;
};

}  // namespace rill
