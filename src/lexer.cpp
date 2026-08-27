#include "lexer.hpp"

#include <cstring>

namespace rill {

namespace {

bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

bool Lexer::match(char expected) {
  if (isAtEnd() || *current_ != expected) return false;
  current_++;
  return true;
}

void Lexer::skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        line_++;
        advance();
        break;
      case '#':
        while (peek() != '\n' && !isAtEnd()) advance();
        break;
      default:
        return;
    }
  }
}

Token Lexer::make(TokenType type) const {
  return Token{type, start_, static_cast<int>(current_ - start_), line_};
}

Token Lexer::errorToken(const char* message) const {
  return Token{TokenType::Error, message,
               static_cast<int>(std::strlen(message)), line_};
}

Token Lexer::string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') line_++;
    advance();
  }
  if (isAtEnd()) return errorToken("unterminated string");
  advance();  // The closing quote.
  return make(TokenType::String);
}

Token Lexer::number() {
  while (isDigit(peek())) advance();
  // A dot only belongs to the number when a digit follows it, so `1.foo`
  // lexes as `1` `.` `foo` rather than a malformed literal.
  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek())) advance();
  }
  return make(TokenType::Number);
}

TokenType Lexer::identifierType() const {
  const int len = static_cast<int>(current_ - start_);

  struct Keyword {
    const char* text;
    int length;
    TokenType type;
  };
  static const Keyword kKeywords[] = {
      {"let", 3, TokenType::Let},           {"var", 3, TokenType::Var},
      {"fn", 2, TokenType::Fn},             {"if", 2, TokenType::If},
      {"else", 4, TokenType::Else},         {"while", 5, TokenType::While},
      {"match", 5, TokenType::Match},       {"and", 3, TokenType::And},
      {"or", 2, TokenType::Or},             {"true", 4, TokenType::True},
      {"false", 5, TokenType::False},       {"nil", 3, TokenType::Nil},
      {"return", 6, TokenType::Return},     {"break", 5, TokenType::Break},
      {"continue", 8, TokenType::Continue},
  };

  for (const Keyword& kw : kKeywords) {
    if (kw.length == len && std::memcmp(start_, kw.text, static_cast<size_t>(len)) == 0) {
      return kw.type;
    }
  }

  // A lone `_` is the wildcard pattern; `_foo` is an ordinary name.
  if (len == 1 && start_[0] == '_') return TokenType::Underscore;
  return TokenType::Identifier;
}

Token Lexer::identifier() {
  while (isAlpha(peek()) || isDigit(peek())) advance();
  return make(identifierType());
}

Token Lexer::next() {
  skipWhitespace();
  start_ = current_;
  if (isAtEnd()) return make(TokenType::Eof);

  char c = advance();
  if (isAlpha(c)) return identifier();
  if (isDigit(c)) return number();

  switch (c) {
    case '(': return make(TokenType::LeftParen);
    case ')': return make(TokenType::RightParen);
    case '{': return make(TokenType::LeftBrace);
    case '}': return make(TokenType::RightBrace);
    case ',': return make(TokenType::Comma);
    case '.': return make(TokenType::Dot);
    case ';': return make(TokenType::Semicolon);
    case ':': return make(TokenType::Colon);
    case '|': return make(TokenType::Pipe);
    case '+': return make(TokenType::Plus);
    case '*': return make(TokenType::Star);
    case '/': return make(TokenType::Slash);
    case '%': return make(TokenType::Percent);
    case '-': return make(match('>') ? TokenType::Arrow : TokenType::Minus);
    case '!': return make(match('=') ? TokenType::BangEqual : TokenType::Bang);
    case '=': return make(match('=') ? TokenType::EqualEqual : TokenType::Equal);
    case '<': return make(match('=') ? TokenType::LessEqual : TokenType::Less);
    case '>':
      return make(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
    case '"': return string();
    default: return errorToken("unexpected character");
  }
}

const char* tokenTypeName(TokenType t) {
  switch (t) {
    case TokenType::LeftParen: return "LeftParen";
    case TokenType::RightParen: return "RightParen";
    case TokenType::LeftBrace: return "LeftBrace";
    case TokenType::RightBrace: return "RightBrace";
    case TokenType::Comma: return "Comma";
    case TokenType::Dot: return "Dot";
    case TokenType::Semicolon: return "Semicolon";
    case TokenType::Colon: return "Colon";
    case TokenType::Pipe: return "Pipe";
    case TokenType::Plus: return "Plus";
    case TokenType::Minus: return "Minus";
    case TokenType::Star: return "Star";
    case TokenType::Slash: return "Slash";
    case TokenType::Percent: return "Percent";
    case TokenType::Bang: return "Bang";
    case TokenType::BangEqual: return "BangEqual";
    case TokenType::Equal: return "Equal";
    case TokenType::EqualEqual: return "EqualEqual";
    case TokenType::Less: return "Less";
    case TokenType::LessEqual: return "LessEqual";
    case TokenType::Greater: return "Greater";
    case TokenType::GreaterEqual: return "GreaterEqual";
    case TokenType::Arrow: return "Arrow";
    case TokenType::Identifier: return "Identifier";
    case TokenType::String: return "String";
    case TokenType::Number: return "Number";
    case TokenType::Underscore: return "Underscore";
    case TokenType::Let: return "Let";
    case TokenType::Var: return "Var";
    case TokenType::Fn: return "Fn";
    case TokenType::If: return "If";
    case TokenType::Else: return "Else";
    case TokenType::While: return "While";
    case TokenType::Match: return "Match";
    case TokenType::And: return "And";
    case TokenType::Or: return "Or";
    case TokenType::True: return "True";
    case TokenType::False: return "False";
    case TokenType::Nil: return "Nil";
    case TokenType::Return: return "Return";
    case TokenType::Break: return "Break";
    case TokenType::Continue: return "Continue";
    case TokenType::Error: return "Error";
    case TokenType::Eof: return "Eof";
  }
  return "?";
}

}  // namespace rill
