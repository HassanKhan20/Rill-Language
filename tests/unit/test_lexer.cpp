#include <string>
#include <vector>

#include "lexer.hpp"
#include "test_harness.hpp"

using namespace rill;

namespace {

std::vector<TokenType> lexAll(const char* src) {
  Lexer lx(src);
  std::vector<TokenType> out;
  for (;;) {
    Token t = lx.next();
    out.push_back(t.type);
    if (t.type == TokenType::Eof || t.type == TokenType::Error) break;
  }
  return out;
}

std::string lexemeOf(const Token& t) {
  return std::string(t.start, static_cast<size_t>(t.length));
}

}  // namespace

TEST(lexer_single_char_tokens) {
  auto ts = lexAll("(){},.;+-*/%:|");
  std::vector<TokenType> want = {
      TokenType::LeftParen, TokenType::RightParen, TokenType::LeftBrace,
      TokenType::RightBrace, TokenType::Comma,     TokenType::Dot,
      TokenType::Semicolon, TokenType::Plus,       TokenType::Minus,
      TokenType::Star,      TokenType::Slash,      TokenType::Percent,
      TokenType::Colon,     TokenType::Pipe,       TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_two_char_tokens) {
  auto ts = lexAll("== != <= >= ->");
  std::vector<TokenType> want = {TokenType::EqualEqual, TokenType::BangEqual,
                                 TokenType::LessEqual,
                                 TokenType::GreaterEqual, TokenType::Arrow,
                                 TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_single_char_prefixes_of_two_char_tokens) {
  auto ts = lexAll("= ! < > -");
  std::vector<TokenType> want = {TokenType::Equal,   TokenType::Bang,
                                 TokenType::Less,    TokenType::Greater,
                                 TokenType::Minus,   TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_keywords) {
  auto ts = lexAll(
      "let var fn if else while match and or true false nil return break "
      "continue");
  std::vector<TokenType> want = {
      TokenType::Let,    TokenType::Var,   TokenType::Fn,
      TokenType::If,     TokenType::Else,  TokenType::While,
      TokenType::Match,  TokenType::And,   TokenType::Or,
      TokenType::True,   TokenType::False, TokenType::Nil,
      TokenType::Return, TokenType::Break, TokenType::Continue,
      TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_identifier_that_merely_starts_with_a_keyword) {
  auto ts = lexAll("lettuce iffy formula nils");
  std::vector<TokenType> want = {TokenType::Identifier, TokenType::Identifier,
                                 TokenType::Identifier, TokenType::Identifier,
                                 TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_numbers_and_strings) {
  auto ts = lexAll("1 2.5 \"hi\"");
  std::vector<TokenType> want = {TokenType::Number, TokenType::Number,
                                 TokenType::String, TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_number_lexeme_excludes_trailing_dot) {
  // `1.` is a number followed by a dot, not a malformed number: property
  // access on a numeric literal must stay lexable.
  Lexer lx("1.foo");
  Token num = lx.next();
  CHECK(num.type == TokenType::Number);
  CHECK_EQ(lexemeOf(num), std::string("1"));
  CHECK(lx.next().type == TokenType::Dot);
}

TEST(lexer_string_lexeme_includes_quotes) {
  Lexer lx("\"hi\"");
  Token s = lx.next();
  CHECK_EQ(lexemeOf(s), std::string("\"hi\""));
}

TEST(lexer_comments_are_skipped_and_lines_counted) {
  Lexer lx("1 # trailing comment\n2");
  Token a = lx.next();
  Token b = lx.next();
  CHECK_EQ(a.line, 1);
  CHECK_EQ(b.line, 2);
  CHECK(b.type == TokenType::Number);
}

TEST(lexer_multiline_string_counts_lines) {
  Lexer lx("\"a\nb\" 1");
  Token s = lx.next();
  CHECK(s.type == TokenType::String);
  Token n = lx.next();
  CHECK_EQ(n.line, 2);
}

TEST(lexer_unterminated_string_is_error) {
  auto ts = lexAll("\"oops");
  CHECK(ts.back() == TokenType::Error);
}

TEST(lexer_unexpected_character_is_error) {
  auto ts = lexAll("@");
  CHECK(ts.back() == TokenType::Error);
}

TEST(lexer_bare_underscore_is_its_own_token) {
  auto ts = lexAll("_");
  CHECK(ts.front() == TokenType::Underscore);
}

TEST(lexer_underscore_prefixed_name_is_an_identifier) {
  auto ts = lexAll("_foo");
  CHECK(ts.front() == TokenType::Identifier);
}

TEST(lexer_empty_source_is_just_eof) {
  auto ts = lexAll("");
  std::vector<TokenType> want = {TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_whitespace_only_source_is_just_eof) {
  auto ts = lexAll("   \n\t\r  # comment\n");
  std::vector<TokenType> want = {TokenType::Eof};
  CHECK(ts == want);
}
