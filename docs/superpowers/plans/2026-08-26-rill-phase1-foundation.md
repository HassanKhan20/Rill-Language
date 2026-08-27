# Rill Phase 1 (M0–M5) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Rill from an empty repository to a working expression-oriented language with variables, control flow, functions, and closures, running on a stack VM with CI.

**Architecture:** Four-stage pipeline with no AST. The lexer produces tokens on demand; a Pratt parser drives a single-pass compiler that emits bytecode directly into a `Chunk`; a stack VM executes that chunk. Heap objects share a common `Obj` header and are threaded onto an intrusive list so Phase 2 can add a collector without touching allocation sites.

**Tech Stack:** C++17, CMake + Ninja, GCC and Clang, Docker (`debian:bookworm-slim`), Python 3 for the test runner, GitHub Actions. No third-party C++ libraries.

**Spec:** `docs/superpowers/specs/2026-08-26-rill-language-design.md`

## Global Constraints

- **C++17 only.** No third-party C++ libraries. No gtest, no Catch2, no fmt. Standard library only.
- **No AST.** The parser calls compiler emit functions directly. Any task that introduces a tree-shaped intermediate representation violates the spec.
- **Every construct leaves exactly one value on the stack.** This is the compiler's central invariant (spec §4.1). Every expression form must be checked against it.
- **All builds and tests run inside Docker.** Host is Windows; valgrind is Linux-only. Never assume a host toolchain.
- **Line endings:** LF in the repository. A `.gitattributes` enforces this — CRLF in a shell script breaks the Linux container.
- **Warnings are errors:** `-Wall -Wextra -Werror`.
- **Every heap allocation goes through `allocateObject`** in `object.cpp` and is threaded onto `vm.objects`. Phase 2's collector depends on this with no exceptions.
- **Numbers are `double` only.** No integer type (spec §4.4).
- **`nil` and `false` are the only falsey values** (spec §4.4).
- **File extension is `.rl`.**

---

### Task 1: Docker, CMake, CI skeleton

**Files:**
- Create: `.gitattributes`, `.gitignore`, `Dockerfile`, `docker-compose.yml`, `CMakeLists.txt`, `src/main.cpp`, `src/common.hpp`, `.github/workflows/ci.yml`, `scripts/dev.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: a `rill` binary at `build/rill`; `scripts/dev.sh build` and `scripts/dev.sh test` entry points used by every later task.

- [ ] **Step 1: Write `.gitattributes` so line endings survive the Windows/Linux boundary**

```gitattributes
* text=auto eol=lf
*.sh text eol=lf
*.rl text eol=lf
*.png binary
```

- [ ] **Step 2: Write `.gitignore`**

```gitignore
build/
*.o
.cache/
compile_commands.json
bench/results/
```

- [ ] **Step 3: Write the `Dockerfile`**

```dockerfile
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ clang cmake ninja-build valgrind python3 git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
```

- [ ] **Step 4: Write `docker-compose.yml`**

```yaml
services:
  dev:
    build: .
    image: rill-dev
    volumes:
      - .:/work
    working_dir: /work
```

- [ ] **Step 5: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(rill CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RILL_GC_STRESS       "Collect before every allocation" OFF)
option(RILL_NANBOX          "Pack values into 8 bytes"        OFF)
option(RILL_COMPUTED_GOTO   "Use computed-goto dispatch"      OFF)
option(RILL_FOLD            "Constant-fold at emit time"      ON)
option(RILL_SUPEROPS        "Specialized opcodes"             ON)
option(RILL_TRACE           "Trace execution"                 OFF)

add_library(rill_core
  src/common.hpp
)
set_target_properties(rill_core PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(rill_core PUBLIC src)
target_compile_options(rill_core PUBLIC -Wall -Wextra -Werror)

foreach(opt RILL_GC_STRESS RILL_NANBOX RILL_COMPUTED_GOTO RILL_FOLD RILL_SUPEROPS RILL_TRACE)
  if(${opt})
    target_compile_definitions(rill_core PUBLIC ${opt}=1)
  endif()
endforeach()

add_executable(rill src/main.cpp)
target_link_libraries(rill PRIVATE rill_core)

enable_testing()
```

Note: `rill_core` starts as a header-only stub. Each later task appends its `.cpp` to the `add_library` list.

- [ ] **Step 6: Write `src/common.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace rill {
constexpr int kStackMax  = 256 * 64;
constexpr int kFramesMax = 64;
constexpr int kUint8Count = 256;
}  // namespace rill
```

- [ ] **Step 7: Write `src/main.cpp` as a version stub**

```cpp
#include <cstdio>
#include "common.hpp"

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::printf("rill 0.1.0\n");
    return 0;
  }
  std::printf("usage: rill [script.rl]\n");
  return 64;
}
```

Add `#include <string>` at the top.

- [ ] **Step 8: Write `scripts/dev.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail

CMD="${1:-build}"
shift || true

case "$CMD" in
  build)
    cmake -S . -B build -G Ninja "$@"
    cmake --build build
    ;;
  test)
    ctest --test-dir build --output-on-failure "$@"
    ;;
  shell) exec /bin/bash ;;
  *) echo "usage: dev.sh {build|test|shell}" >&2; exit 64 ;;
esac
```

- [ ] **Step 9: Build the image and verify the binary runs**

Run:
```bash
docker compose build
docker compose run --rm dev bash scripts/dev.sh build
docker compose run --rm dev ./build/rill --version
```
Expected: `rill 0.1.0`

- [ ] **Step 10: Write `.github/workflows/ci.yml`**

```yaml
name: CI
on:
  push:
    branches: [main]
  pull_request:

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix:
        compiler: [g++, clang++]
        buildtype: [Debug, Release]
    steps:
      - uses: actions/checkout@v4
      - name: Install toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y g++ clang cmake ninja-build valgrind python3
      - name: Build
        run: |
          cmake -S . -B build -G Ninja \
            -DCMAKE_CXX_COMPILER=${{ matrix.compiler }} \
            -DCMAKE_BUILD_TYPE=${{ matrix.buildtype }}
          cmake --build build
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 11: Commit**

```bash
git add .gitattributes .gitignore Dockerfile docker-compose.yml CMakeLists.txt src scripts .github
git commit -m "build: Docker dev environment, CMake skeleton, CI matrix"
```

---

### Task 2: Test harness

**Files:**
- Create: `tests/unit/test_harness.hpp`, `tests/unit/main.cpp`, `tests/run_behavior_tests.py`, `tests/lang/smoke.rl`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `TEST(name)` / `CHECK(cond)` / `CHECK_EQ(a, b)` macros; a `rill_tests` binary; `tests/run_behavior_tests.py` which runs every `tests/lang/**/*.rl` and diffs against `# expect:` annotations.

- [ ] **Step 1: Write `tests/unit/test_harness.hpp`**

```cpp
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

namespace rilltest {

struct Test { const char* name; std::function<void()> fn; };
inline std::vector<Test>& registry() { static std::vector<Test> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline const char*& currentTest() { static const char* t = ""; return t; }

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

inline void reportFailure(const char* file, int line, const std::string& msg) {
  std::printf("  FAIL %s\n    %s:%d: %s\n", currentTest(), file, line, msg.c_str());
  failures()++;
}

inline int runAll() {
  int passed = 0;
  for (auto& t : registry()) {
    currentTest() = t.name;
    int before = failures();
    t.fn();
    if (failures() == before) passed++;
  }
  std::printf("%d/%zu unit tests passed\n", passed, registry().size());
  return failures() == 0 ? 0 : 1;
}

}  // namespace rilltest

#define TEST(name)                                                        \
  static void name();                                                     \
  static ::rilltest::Registrar reg_##name(#name, name);                   \
  static void name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) ::rilltest::reportFailure(__FILE__, __LINE__,            \
        "CHECK failed: " #cond);                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                    \
  do {                                                                    \
    auto va_ = (a); auto vb_ = (b);                                       \
    if (!(va_ == vb_)) ::rilltest::reportFailure(__FILE__, __LINE__,      \
        "CHECK_EQ failed: " #a " != " #b);                                \
  } while (0)
```

- [ ] **Step 2: Write `tests/unit/main.cpp`**

```cpp
#include "test_harness.hpp"
int main() { return ::rilltest::runAll(); }
```

- [ ] **Step 3: Write `tests/run_behavior_tests.py`**

The runner scans each `.rl` file for `# expect: <line>` and `# expect error: <substring>` annotations, runs the binary, and compares.

```python
#!/usr/bin/env python3
"""Run every tests/lang/**/*.rl and check output against inline annotations."""
import pathlib
import re
import subprocess
import sys

EXPECT = re.compile(r"#\s*expect:\s?(.*)$")
EXPECT_ERROR = re.compile(r"#\s*expect error:\s?(.*)$")


def parse(path):
    out, errs = [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = EXPECT.search(line)
        if m:
            out.append(m.group(1).rstrip())
        m = EXPECT_ERROR.search(line)
        if m:
            errs.append(m.group(1).strip())
    return out, errs


def run_one(binary, path, runner):
    want_out, want_errs = parse(path)
    cmd = runner + [str(binary), str(path)]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    got_out = [ln.rstrip() for ln in proc.stdout.splitlines()]
    combined = proc.stdout + proc.stderr

    problems = []
    if want_errs:
        for want in want_errs:
            if want not in combined:
                problems.append(f"expected error containing {want!r}")
        if proc.returncode == 0:
            problems.append("expected nonzero exit status")
    else:
        if proc.returncode != 0:
            problems.append(f"exit status {proc.returncode}\n{proc.stderr.strip()}")
        if got_out != want_out:
            problems.append(f"stdout mismatch\n  want: {want_out}\n  got:  {got_out}")
    return problems


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    root = pathlib.Path(sys.argv[2]).resolve()
    runner = sys.argv[3:]

    files = sorted(root.rglob("*.rl"))
    if not files:
        print(f"no .rl tests found under {root}", file=sys.stderr)
        return 1

    failed = 0
    for path in files:
        problems = run_one(binary, path, runner)
        if problems:
            failed += 1
            print(f"FAIL {path.relative_to(root)}")
            for p in problems:
                print(f"  {p}")
    print(f"{len(files) - failed}/{len(files)} behavior tests passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Write `tests/lang/smoke.rl`**

The language does not run yet, so this file only has to exist and produce no output until Task 8.

```rill
# smoke test placeholder — replaced with real expectations in Task 8
```

- [ ] **Step 5: Wire tests into `CMakeLists.txt`**

Append:

```cmake
add_executable(rill_tests tests/unit/main.cpp)
target_link_libraries(rill_tests PRIVATE rill_core)
target_include_directories(rill_tests PRIVATE tests/unit)
add_test(NAME unit COMMAND rill_tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME behavior
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tests/run_behavior_tests.py
          $<TARGET_FILE:rill>
          ${CMAKE_SOURCE_DIR}/tests/lang)
```

- [ ] **Step 6: Verify both test targets run**

Run: `docker compose run --rm dev bash -c "bash scripts/dev.sh build && bash scripts/dev.sh test"`
Expected: both `unit` and `behavior` tests pass (0 unit tests registered, 1 trivially-passing behavior test).

- [ ] **Step 7: Commit**

```bash
git add tests CMakeLists.txt
git commit -m "test: unit harness and behavior test runner"
```

---

### Task 3: Lexer

**Files:**
- Create: `src/lexer.hpp`, `src/lexer.cpp`, `tests/unit/test_lexer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  enum class TokenType { /* see step 1 */ };
  struct Token { TokenType type; const char* start; int length; int line; };
  class Lexer {
   public:
    explicit Lexer(const char* source);
    Token next();
  };
  const char* tokenTypeName(TokenType t);
  ```
  `Token::start` points into the source buffer; the caller owns the source and must outlive every token.

- [ ] **Step 1: Write the failing test `tests/unit/test_lexer.cpp`**

```cpp
#include "test_harness.hpp"
#include "lexer.hpp"

using namespace rill;

static std::vector<TokenType> lexAll(const char* src) {
  Lexer lx(src);
  std::vector<TokenType> out;
  for (;;) {
    Token t = lx.next();
    out.push_back(t.type);
    if (t.type == TokenType::Eof || t.type == TokenType::Error) break;
  }
  return out;
}

TEST(lexer_single_char_tokens) {
  auto ts = lexAll("(){},.;+-*/%");
  std::vector<TokenType> want = {
      TokenType::LeftParen,  TokenType::RightParen, TokenType::LeftBrace,
      TokenType::RightBrace, TokenType::Comma,      TokenType::Dot,
      TokenType::Semicolon,  TokenType::Plus,       TokenType::Minus,
      TokenType::Star,       TokenType::Slash,      TokenType::Percent,
      TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_two_char_tokens) {
  auto ts = lexAll("== != <= >= ->");
  std::vector<TokenType> want = {TokenType::EqualEqual, TokenType::BangEqual,
                                 TokenType::LessEqual, TokenType::GreaterEqual,
                                 TokenType::Arrow, TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_keywords) {
  auto ts = lexAll("let var fn if else while match and or true false nil "
                   "return break continue");
  std::vector<TokenType> want = {
      TokenType::Let,   TokenType::Var,      TokenType::Fn,
      TokenType::If,    TokenType::Else,     TokenType::While,
      TokenType::Match, TokenType::And,      TokenType::Or,
      TokenType::True,  TokenType::False,    TokenType::Nil,
      TokenType::Return, TokenType::Break,   TokenType::Continue,
      TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_identifier_not_keyword_prefix) {
  auto ts = lexAll("lettuce iffy formula");
  std::vector<TokenType> want = {TokenType::Identifier, TokenType::Identifier,
                                 TokenType::Identifier, TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_numbers_and_strings) {
  auto ts = lexAll("1 2.5 \"hi\"");
  std::vector<TokenType> want = {TokenType::Number, TokenType::Number,
                                 TokenType::String, TokenType::Eof};
  CHECK(ts == want);
}

TEST(lexer_comments_and_lines) {
  Lexer lx("1 # trailing comment\n2");
  Token a = lx.next();
  Token b = lx.next();
  CHECK_EQ(a.line, 1);
  CHECK_EQ(b.line, 2);
}

TEST(lexer_unterminated_string_is_error) {
  auto ts = lexAll("\"oops");
  CHECK(ts.back() == TokenType::Error);
}

TEST(lexer_underscore_is_its_own_token) {
  auto ts = lexAll("_");
  CHECK(ts.front() == TokenType::Underscore);
}
```

- [ ] **Step 2: Run it and confirm it fails to compile**

Run: `docker compose run --rm dev bash scripts/dev.sh build`
Expected: FAIL — `lexer.hpp` does not exist.

- [ ] **Step 3: Write `src/lexer.hpp`**

```cpp
#pragma once
#include <vector>

namespace rill {

enum class TokenType {
  // Single character
  LeftParen, RightParen, LeftBrace, RightBrace,
  Comma, Dot, Semicolon, Colon, Pipe,
  Plus, Minus, Star, Slash, Percent,
  // One or two characters
  Bang, BangEqual, Equal, EqualEqual,
  Less, LessEqual, Greater, GreaterEqual, Arrow,
  // Literals
  Identifier, String, Number, Underscore,
  // Keywords
  Let, Var, Fn, If, Else, While, Match, And, Or,
  True, False, Nil, Return, Break, Continue,
  // Bookkeeping
  Error, Eof,
};

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
  char peekNext() const { return current_[0] == '\0' ? '\0' : current_[1]; }
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
```

- [ ] **Step 4: Write `src/lexer.cpp`**

Key points: `skipWhitespace` also consumes `#` comments to end of line; `identifierType` uses a small trie-style keyword check; a bare `_` returns `Underscore`, but `_foo` is an `Identifier`.

```cpp
#include "lexer.hpp"
#include <cstring>

namespace rill {

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool Lexer::match(char expected) {
  if (isAtEnd() || *current_ != expected) return false;
  current_++;
  return true;
}

void Lexer::skipWhitespace() {
  for (;;) {
    char c = peek();
    if (c == ' ' || c == '\r' || c == '\t') {
      advance();
    } else if (c == '\n') {
      line_++;
      advance();
    } else if (c == '#') {
      while (peek() != '\n' && !isAtEnd()) advance();
    } else {
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
  advance();  // closing quote
  return make(TokenType::String);
}

Token Lexer::number() {
  while (isDigit(peek())) advance();
  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek())) advance();
  }
  return make(TokenType::Number);
}

TokenType Lexer::identifierType() const {
  const int len = static_cast<int>(current_ - start_);
  struct Kw { const char* text; int len; TokenType type; };
  static const Kw kws[] = {
      {"let", 3, TokenType::Let},         {"var", 3, TokenType::Var},
      {"fn", 2, TokenType::Fn},           {"if", 2, TokenType::If},
      {"else", 4, TokenType::Else},       {"while", 5, TokenType::While},
      {"match", 5, TokenType::Match},     {"and", 3, TokenType::And},
      {"or", 2, TokenType::Or},           {"true", 4, TokenType::True},
      {"false", 5, TokenType::False},     {"nil", 3, TokenType::Nil},
      {"return", 6, TokenType::Return},   {"break", 5, TokenType::Break},
      {"continue", 8, TokenType::Continue},
  };
  for (const Kw& kw : kws) {
    if (kw.len == len && std::memcmp(start_, kw.text, len) == 0) return kw.type;
  }
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
    case '>': return make(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
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
```

- [ ] **Step 5: Add sources to `CMakeLists.txt`**

Add `src/lexer.cpp` to `add_library(rill_core ...)` and `tests/unit/test_lexer.cpp` to `add_executable(rill_tests ...)`.

- [ ] **Step 6: Run tests and verify they pass**

Run: `docker compose run --rm dev bash -c "bash scripts/dev.sh build && ./build/rill_tests"`
Expected: `8/8 unit tests passed`

- [ ] **Step 7: Commit**

```bash
git add src/lexer.hpp src/lexer.cpp tests/unit/test_lexer.cpp CMakeLists.txt
git commit -m "feat(lexer): tokenize Rill source"
```

---

### Task 4: Value and Chunk

**Files:**
- Create: `src/value.hpp`, `src/value.cpp`, `src/chunk.hpp`, `src/chunk.cpp`, `src/debug.hpp`, `src/debug.cpp`, `tests/unit/test_chunk.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct Obj;  // forward declared; defined in Task 6
  enum class ValueType { Nil, Bool, Number, Obj };
  struct Value { ValueType type; union { bool boolean; double number; Obj* obj; } as; };
  Value nilValue();
  Value boolValue(bool b);
  Value numberValue(double d);
  Value objValue(Obj* o);
  bool isNil(Value), isBool(Value), isNumber(Value), isObj(Value);
  bool asBool(Value); double asNumber(Value); Obj* asObj(Value);
  bool valuesEqual(Value a, Value b);
  bool isFalsey(Value v);
  void printValue(Value v);

  enum class OpCode : uint8_t { /* grows each task */ };
  class Chunk {
   public:
    void write(uint8_t byte, int line);
    int addConstant(Value v);   // returns index
    int lineAt(size_t offset) const;
    std::vector<uint8_t> code;
    std::vector<Value> constants;
  };
  int disassembleInstruction(const Chunk& chunk, int offset);
  void disassembleChunk(const Chunk& chunk, const char* name);
  ```
  Line info is run-length encoded: `std::vector<std::pair<int,int>> lines_` of (line, runLength).

- [ ] **Step 1: Write the failing test `tests/unit/test_chunk.cpp`**

```cpp
#include "test_harness.hpp"
#include "chunk.hpp"
#include "value.hpp"

using namespace rill;

TEST(value_constructors_and_predicates) {
  CHECK(isNil(nilValue()));
  CHECK(isBool(boolValue(true)));
  CHECK(asBool(boolValue(true)));
  CHECK(isNumber(numberValue(1.5)));
  CHECK_EQ(asNumber(numberValue(1.5)), 1.5);
}

TEST(value_falsiness_only_nil_and_false) {
  CHECK(isFalsey(nilValue()));
  CHECK(isFalsey(boolValue(false)));
  CHECK(!isFalsey(boolValue(true)));
  CHECK(!isFalsey(numberValue(0)));
}

TEST(value_equality) {
  CHECK(valuesEqual(numberValue(3), numberValue(3)));
  CHECK(!valuesEqual(numberValue(3), boolValue(true)));
  CHECK(valuesEqual(nilValue(), nilValue()));
}

TEST(chunk_write_and_constants) {
  Chunk c;
  int idx = c.addConstant(numberValue(42));
  c.write(static_cast<uint8_t>(OpCode::Constant), 1);
  c.write(static_cast<uint8_t>(idx), 1);
  CHECK_EQ(c.code.size(), static_cast<size_t>(2));
  CHECK_EQ(asNumber(c.constants[static_cast<size_t>(idx)]), 42.0);
}

TEST(chunk_run_length_encoded_lines) {
  Chunk c;
  c.write(1, 10);
  c.write(2, 10);
  c.write(3, 12);
  CHECK_EQ(c.lineAt(0), 10);
  CHECK_EQ(c.lineAt(1), 10);
  CHECK_EQ(c.lineAt(2), 12);
}
```

- [ ] **Step 2: Run and confirm failure**

Run: `docker compose run --rm dev bash scripts/dev.sh build`
Expected: FAIL — `chunk.hpp` missing.

- [ ] **Step 3: Write `src/value.hpp` and `src/value.cpp`**

`value.cpp` implements `valuesEqual` (numbers by `==`, bools by `==`, nil always equal, objects by pointer identity for now — Task 6 keeps pointer identity valid for strings via interning) and `printValue` (numbers print with `%g`, so `5.0` prints as `5`).

- [ ] **Step 4: Write `src/chunk.hpp` and `src/chunk.cpp`**

Start `OpCode` with: `Constant, Nil, True, False, Pop, Negate, Not, Add, Subtract, Multiply, Divide, Modulo, Equal, Greater, Less, Return`.

`lineAt` walks the run-length vector accumulating run lengths until it passes `offset`.

- [ ] **Step 5: Write `src/debug.hpp` and `src/debug.cpp`**

`disassembleInstruction` switches on the opcode and prints `offset`, line (or `|` when the same as the previous offset's line), the opcode name, and any operand. Simple instructions consume 1 byte; `Constant` consumes 2 and prints the constant value.

- [ ] **Step 6: Add sources to CMake, run tests**

Run: `docker compose run --rm dev bash -c "bash scripts/dev.sh build && ./build/rill_tests"`
Expected: `13/13 unit tests passed`

- [ ] **Step 7: Commit**

```bash
git add src/value.* src/chunk.* src/debug.* tests/unit/test_chunk.cpp CMakeLists.txt
git commit -m "feat(bytecode): Value representation, Chunk, and disassembler"
```

---

### Task 5: Hash table

**Files:**
- Create: `src/table.hpp`, `src/table.cpp`, `tests/unit/test_table.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct ObjString;  // forward declared
  struct Entry { ObjString* key; Value value; };
  class Table {
   public:
    ~Table();
    bool get(ObjString* key, Value* out) const;
    bool set(ObjString* key, Value value);   // true if a NEW key
    bool remove(ObjString* key);
    ObjString* findString(const char* chars, int length, uint32_t hash) const;
    void removeWhite();                       // used by the GC in Phase 2
    int count = 0, capacity = 0;
    Entry* entries = nullptr;
  };
  ```
  Open addressing with linear probing, tombstones as `{key=nullptr, value=boolValue(true)}`, growth at 75% load.

- [ ] **Step 1: Note on test ordering**

`Table` is keyed by `ObjString*`, which Task 6 defines, so a table test cannot be written before its key type exists. This task therefore ships the implementation and Task 6 Step 1 ships the tests for it — Task 6 is not complete until `test_table.cpp` passes. Do not mark Task 5 done independently of Task 6.

- [ ] **Step 2: Write `src/table.hpp` and `src/table.cpp`**

`findEntry` probes linearly, remembering the first tombstone so a later insert reuses it. `adjustCapacity` reallocates and reinserts all live entries (tombstones are dropped, so `count` is recomputed). `findString` compares hash, then length, then bytes — this is what makes interning possible.

- [ ] **Step 3: Build to verify it compiles**

Run: `docker compose run --rm dev bash scripts/dev.sh build`
Expected: build succeeds; no new tests yet.

- [ ] **Step 4: Commit**

```bash
git add src/table.hpp src/table.cpp CMakeLists.txt
git commit -m "feat(table): open-addressing hash table"
```

---

### Task 6: Object model and string interning

**Files:**
- Create: `src/object.hpp`, `src/object.cpp`, `tests/unit/test_table.cpp`, `tests/unit/test_object.cpp`
- Modify: `src/value.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  enum class ObjType { String, Function, Closure, Upvalue, Native, Map };
  struct Obj { ObjType type; bool isMarked; Obj* next; };
  struct ObjString : Obj { int length; char* chars; uint32_t hash; };

  ObjString* copyString(const char* chars, int length);  // interned
  ObjString* takeString(char* chars, int length);        // takes ownership
  uint32_t hashString(const char* key, int length);
  void freeObjects();          // frees the whole intrusive list
  bool isObjType(Value v, ObjType t);
  ObjString* asString(Value v);
  ```
  All allocation goes through `allocateObject(size, type)`, which links onto a global object list. **This function is the single choke point Phase 2's collector hooks.**

- [ ] **Step 1: Write `tests/unit/test_object.cpp` and `tests/unit/test_table.cpp`**

```cpp
// tests/unit/test_object.cpp
#include "test_harness.hpp"
#include "object.hpp"
#include "value.hpp"

using namespace rill;

TEST(strings_are_interned) {
  ObjString* a = copyString("hello", 5);
  ObjString* b = copyString("hello", 5);
  CHECK(a == b);  // pointer identity, not just equal contents
}

TEST(distinct_strings_differ) {
  ObjString* a = copyString("hello", 5);
  ObjString* b = copyString("world", 5);
  CHECK(a != b);
}

TEST(string_equality_is_pointer_equality) {
  Value a = objValue(reinterpret_cast<Obj*>(copyString("x", 1)));
  Value b = objValue(reinterpret_cast<Obj*>(copyString("x", 1)));
  CHECK(valuesEqual(a, b));
}
```

```cpp
// tests/unit/test_table.cpp
#include "test_harness.hpp"
#include "table.hpp"
#include "object.hpp"
#include "value.hpp"

using namespace rill;

TEST(table_set_and_get) {
  Table t;
  ObjString* k = copyString("key", 3);
  CHECK(t.set(k, numberValue(7)));   // new key
  Value out;
  CHECK(t.get(k, &out));
  CHECK_EQ(asNumber(out), 7.0);
}

TEST(table_set_existing_returns_false) {
  Table t;
  ObjString* k = copyString("dup", 3);
  t.set(k, numberValue(1));
  CHECK(!t.set(k, numberValue(2)));
  Value out;
  t.get(k, &out);
  CHECK_EQ(asNumber(out), 2.0);
}

TEST(table_remove_leaves_tombstone_probing_intact) {
  Table t;
  ObjString* a = copyString("a", 1);
  ObjString* b = copyString("b", 1);
  t.set(a, numberValue(1));
  t.set(b, numberValue(2));
  CHECK(t.remove(a));
  Value out;
  CHECK(!t.get(a, &out));
  CHECK(t.get(b, &out));   // probe chain must still reach b
}

TEST(table_grows_past_initial_capacity) {
  Table t;
  for (int i = 0; i < 64; i++) {
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "k%d", i);
    t.set(copyString(buf, n), numberValue(i));
  }
  CHECK_EQ(t.count, 64);
  Value out;
  CHECK(t.get(copyString("k63", 3), &out));
  CHECK_EQ(asNumber(out), 63.0);
}
```

Add `#include <cstdio>` for `snprintf`.

- [ ] **Step 2: Run and confirm failure**

Expected: FAIL — `object.hpp` missing.

- [ ] **Step 3: Write `src/object.hpp` and `src/object.cpp`**

`hashString` is FNV-1a 32-bit. `copyString` hashes first, calls `strings.findString`, and returns the existing pointer on a hit; on a miss it allocates, copies, and interns. `takeString` does the same but frees the incoming buffer on a hit. A file-scope `Table strings;` and `Obj* objects = nullptr;` hold the intern table and the allocation list — Phase 2 moves both onto the VM.

- [ ] **Step 4: Update `src/value.cpp`**

`valuesEqual` for `ValueType::Obj` stays pointer comparison; add a comment noting interning is what makes this correct for strings.

- [ ] **Step 5: Run tests**

Expected: `20/20 unit tests passed`

- [ ] **Step 6: Commit**

```bash
git add src/object.* src/value.cpp tests/unit/test_object.cpp tests/unit/test_table.cpp CMakeLists.txt
git commit -m "feat(objects): Obj header, interned strings, allocation list"
```

---

### Task 7: Pratt parser and compiler — arithmetic

**Files:**
- Create: `src/compiler.hpp`, `src/compiler.cpp`, `src/parser.hpp`, `src/parser.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  bool compile(const char* source, ObjFunction* out);  // false on compile error
  ```
  For this task the compiler targets a bare `Chunk`; Task 13 wraps it in `ObjFunction`. Signature for now:
  ```cpp
  bool compile(const char* source, Chunk* chunk);
  ```

- [ ] **Step 1: Define the precedence ladder**

```cpp
enum class Prec {
  None, Assignment,  // =
  Or, And, Equality, Comparison, Term, Factor, Unary, Call, Primary
};
using ParseFn = void (*)(bool canAssign);
struct ParseRule { ParseFn prefix; ParseFn infix; Prec precedence; };
```

- [ ] **Step 2: Write `parsePrecedence`**

```cpp
static void parsePrecedence(Prec precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == nullptr) { error("expected expression"); return; }
  bool canAssign = precedence <= Prec::Assignment;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }
  if (canAssign && match(TokenType::Equal)) error("invalid assignment target");
}
```

- [ ] **Step 3: Implement `number`, `unary`, `binary`, `grouping`, `literal`**

`binary` reads the operator, recurses at `Prec(int(rule->precedence) + 1)` for left associativity, then emits. `!=` emits `Equal` then `Not`; `<=` emits `Greater` then `Not`; `>=` emits `Less` then `Not`.

- [ ] **Step 4: Implement error handling with panic-mode recovery**

`errorAt` prints `[line N] Error at 'lexeme': message` to stderr, sets `parser.hadError`, and sets `parser.panicMode` so cascading errors are suppressed until a synchronization point.

- [ ] **Step 5: Write a temporary main that compiles and disassembles**

Modify `main.cpp` so `rill --dump <file>` compiles and disassembles without running. This is the verification surface until Task 8.

- [ ] **Step 6: Verify by hand**

Run: `echo '1 + 2 * 3' > /tmp/t.rl && docker compose run --rm dev ./build/rill --dump /tmp/t.rl`
Expected: constants 1, 2, 3 loaded with `Multiply` before `Add`, confirming precedence.

- [ ] **Step 7: Commit**

```bash
git add src/parser.* src/compiler.* src/main.cpp CMakeLists.txt
git commit -m "feat(compiler): Pratt parser emitting arithmetic bytecode"
```

---

### Task 8: VM dispatch loop

**Files:**
- Create: `src/vm.hpp`, `src/vm.cpp`, `tests/lang/arith/*.rl`
- Modify: `src/main.cpp`, `tests/lang/smoke.rl`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  enum class InterpretResult { Ok, CompileError, RuntimeError };
  class VM {
   public:
    void init();
    void free();
    InterpretResult interpret(const char* source);
    void push(Value v);
    Value pop();
   private:
    Value stack_[kStackMax];
    Value* stackTop_;
  };
  extern VM vm;
  ```
  `main` returns 65 on `CompileError` and 70 on `RuntimeError` (spec §5.5).

- [ ] **Step 1: Write behavior tests first**

```rill
# tests/lang/arith/precedence.rl
# expect: 7
print(1 + 2 * 3)
```

```rill
# tests/lang/arith/grouping.rl
# expect: 9
print((1 + 2) * 3)
```

```rill
# tests/lang/arith/unary.rl
# expect: -5
print(-(2 + 3))
```

```rill
# tests/lang/arith/comparison.rl
# expect: true
# expect: false
# expect: true
print(1 < 2)
print(2 <= 1)
print(3 == 3)
```

```rill
# tests/lang/arith/type_error.rl
# expect error: operands must be numbers
print(1 + true)
```

Replace `tests/lang/smoke.rl` with:
```rill
# expect: 5
print(2 + 3)
```

Note: `print` is a builtin function, but builtins do not exist until Task 13. **For Tasks 8–12, treat `print` as a temporary `OpCode::Print` emitted when the parser sees the identifier `print` followed by `(`.** Task 13 deletes that special case and makes it a real native function; the tests above are written to survive that change unaltered.

- [ ] **Step 2: Run and confirm failure**

Run: `docker compose run --rm dev bash scripts/dev.sh test`
Expected: behavior tests FAIL.

- [ ] **Step 3: Write the dispatch loop**

```cpp
#define READ_BYTE()     (*ip++)
#define READ_CONSTANT() (chunk->constants[READ_BYTE()])
#define BINARY_OP(valueType, op)                                   \
  do {                                                             \
    if (!isNumber(peek(0)) || !isNumber(peek(1))) {                \
      runtimeError("operands must be numbers");                    \
      return InterpretResult::RuntimeError;                        \
    }                                                              \
    double b = asNumber(pop());                                    \
    double a = asNumber(pop());                                    \
    push(valueType(a op b));                                       \
  } while (false)
```

Handle every opcode from Task 4. `Divide` by zero yields IEEE infinity rather than an error — doubles only, per spec §4.4.

- [ ] **Step 4: Implement `runtimeError`**

Variadic, prints to stderr, then prints `[line N] in script` using `chunk->lineAt(ip - chunk->code.data() - 1)`, then resets the stack.

- [ ] **Step 5: Wire `main.cpp`**

`rill <file>` reads the file, calls `vm.interpret`, returns 0/65/70. `rill` with no args runs a REPL reading lines from stdin.

- [ ] **Step 6: Run tests**

Expected: all behavior tests pass, `20/20 unit tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/vm.* src/main.cpp tests/lang CMakeLists.txt
git commit -m "feat(vm): stack machine dispatch loop and arithmetic"
```

---

### Task 9: Globals, `let` and `var`

**Files:**
- Modify: `src/chunk.hpp`, `src/compiler.cpp`, `src/parser.cpp`, `src/vm.cpp`
- Create: `tests/lang/vars/*.rl`

**Interfaces:**
- Produces: opcodes `DefineGlobal`, `GetGlobal`, `SetGlobal`; a `Table globals` on the VM; a parallel `Table globalImmutables` recording which global names were bound with `let`.

- [ ] **Step 1: Write behavior tests**

```rill
# tests/lang/vars/declare_and_read.rl
# expect: 10
let x = 10
print(x)
```

```rill
# tests/lang/vars/var_is_mutable.rl
# expect: 1
# expect: 2
var c = 1
print(c)
c = 2
print(c)
```

```rill
# tests/lang/vars/let_is_immutable.rl
# expect error: cannot assign to immutable binding 'x'
let x = 1
x = 2
```

```rill
# tests/lang/vars/undefined_variable.rl
# expect error: undefined variable 'nope'
print(nope)
```

- [ ] **Step 2: Run, confirm failure**

- [ ] **Step 3: Implement in the compiler**

`let`/`var` are prefix parse rules on `TokenType::Let`/`Var`. Each parses a name, `=`, an initializer expression, then emits `DefineGlobal` with the name's constant index. **Because declarations are expressions, they must leave a value on the stack** (spec §4.1) — emit `Nil` after `DefineGlobal` so the invariant holds.

Immutability: keep a compile-time `std::set<std::string>` of `let`-bound global names. In `namedVariable`, if `canAssign && match(Equal)` and the name is in that set, call `error("cannot assign to immutable binding 'NAME'")`.

- [ ] **Step 4: Implement in the VM**

`GetGlobal` looks the name up in `globals` and raises `undefined variable 'NAME'` on a miss. `SetGlobal` raises the same error if the key does not already exist (assignment never creates a global).

- [ ] **Step 5: Run tests, verify pass**

- [ ] **Step 6: Commit**

```bash
git add src tests/lang/vars
git commit -m "feat(vars): global bindings with let/var immutability"
```

---

### Task 10: Locals, scopes, blocks as expressions

**Files:**
- Modify: `src/compiler.hpp`, `src/compiler.cpp`, `src/parser.cpp`, `src/vm.cpp`, `src/chunk.hpp`
- Create: `tests/lang/blocks/*.rl`

**Interfaces:**
- Produces:
  ```cpp
  struct Local { Token name; int depth; bool isCaptured; bool isMutable; };
  struct Compiler {
    Compiler* enclosing;
    Local locals[kUint8Count];
    int localCount;
    int scopeDepth;
  };
  int resolveLocal(Compiler* c, Token* name);   // -1 if not found
  ```
  Opcodes `GetLocal`, `SetLocal`.

- [ ] **Step 1: Write behavior tests**

```rill
# tests/lang/blocks/block_yields_last_expression.rl
# expect: 16
let a = { let t = 4; t * t }
print(a)
```

```rill
# tests/lang/blocks/trailing_semicolon_yields_nil.rl
# expect: nil
let b = { 1; 2; }
print(b)
```

```rill
# tests/lang/blocks/shadowing_allowed_in_inner_scope.rl
# expect: 2
# expect: 1
let x = 1
print({ let x = 2; x })
print(x)
```

```rill
# tests/lang/blocks/redeclare_in_same_scope_is_error.rl
# expect error: already a binding named 'x' in this scope
{ let x = 1; let x = 2; x }
```

```rill
# tests/lang/blocks/local_let_is_immutable.rl
# expect error: cannot assign to immutable binding 'y'
{ let y = 1; y = 2 }
```

- [ ] **Step 2: Run, confirm failure**

- [ ] **Step 3: Implement block compilation**

This is the task where the one-value invariant is easiest to break. The rule:

```
beginScope()
loop:
  if next token is '}': emit Nil; break            # empty or all-discarded
  compile expression
  if next token is ';':
      consume ';'
      if next token is '}': emit Nil; break        # trailing ; -> nil
      else: emit Pop; continue                     # discard, keep going
  else:
      expect '}'; break                            # this expression is the value
endScope()
```

`endScope` must pop locals *without* discarding the block's result value. Emit the local pops **before** the result is pushed is impossible — instead, keep the result on top and emit `Pop` for each local using a `PopN`-style sequence that pops below the top. The simplest correct approach: emit a new opcode `CloseScope <n>` that removes `n` slots from *under* the top-of-stack value.

- [ ] **Step 4: Add `OpCode::CloseScope` to the VM**

```cpp
case OpCode::CloseScope: {
  uint8_t n = READ_BYTE();
  Value result = pop();
  stackTop_ -= n;
  push(result);
  break;
}
```

- [ ] **Step 5: Implement local resolution and immutability**

`resolveLocal` scans `locals` backward; a local with `depth == -1` (declared but not yet initialized) is an error: `cannot read binding 'x' in its own initializer`. `declareVariable` scans the current scope depth for a duplicate name and errors.

- [ ] **Step 6: Run tests, verify pass**

- [ ] **Step 7: Commit**

```bash
git add src tests/lang/blocks
git commit -m "feat(scopes): locals, shadowing, blocks as expressions"
```

---

### Task 11: `if` expressions and logical operators

**Files:**
- Modify: `src/compiler.cpp`, `src/parser.cpp`, `src/chunk.hpp`, `src/vm.cpp`
- Create: `tests/lang/control/if_*.rl`, `tests/lang/control/logic_*.rl`

**Interfaces:**
- Produces: opcodes `Jump`, `JumpIfFalse`; helpers `int emitJump(OpCode)` and `void patchJump(int offset)`.

- [ ] **Step 1: Write behavior tests**

```rill
# tests/lang/control/if_yields_value.rl
# expect: pos
let n = 5
print(if n > 0 { "pos" } else { "neg" })
```

```rill
# tests/lang/control/if_without_else_yields_nil.rl
# expect: nil
print(if false { 1 })
```

```rill
# tests/lang/control/if_else_chain.rl
# expect: medium
let n = 5
print(if n > 10 { "big" } else if n > 1 { "medium" } else { "small" })
```

```rill
# tests/lang/control/logic_and_short_circuits.rl
# expect: false
print(false and undefined_thing)
```

```rill
# tests/lang/control/logic_yields_operand.rl
# expect: 3
# expect: nil
print(nil or 3)
print(nil and 3)
```

- [ ] **Step 2: Run, confirm failure**

- [ ] **Step 3: Implement `if`**

```
compile condition
thenJump = emitJump(JumpIfFalse)
emit Pop                      # discard condition
compile then-block            # leaves one value
elseJump = emitJump(Jump)
patchJump(thenJump)
emit Pop                      # discard condition on the else path
if 'else': compile else-branch
else:      emit Nil           # if without else yields nil
patchJump(elseJump)
```

Both paths leave exactly one value. Verify this by hand against the invariant.

- [ ] **Step 4: Implement `and` / `or` as infix rules**

`and`: `endJump = emitJump(JumpIfFalse); emit Pop; parsePrecedence(Prec::And); patchJump(endJump);` — when it short-circuits, the falsey left operand stays on the stack and *is* the result. `or` is the mirror image using a `JumpIfFalse` over a `Jump`.

- [ ] **Step 5: Implement `patchJump` with an overflow check**

```cpp
static void patchJump(int offset) {
  int jump = currentChunk()->code.size() - offset - 2;
  if (jump > UINT16_MAX) error("too much code to jump over");
  currentChunk()->code[offset]     = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}
```

- [ ] **Step 6: Run tests, verify pass**

- [ ] **Step 7: Commit**

```bash
git add src tests/lang/control
git commit -m "feat(control): if as an expression, short-circuiting and/or"
```

---

### Task 12: `while`, `break`, `continue`

**Files:**
- Modify: `src/compiler.hpp`, `src/compiler.cpp`, `src/parser.cpp`, `src/chunk.hpp`, `src/vm.cpp`
- Create: `tests/lang/control/while_*.rl`

**Interfaces:**
- Produces: opcode `Loop`; a `LoopContext` on the compiler:
  ```cpp
  struct LoopContext {
    LoopContext* enclosing;
    int startOffset;                  // where `continue` jumps to
    int scopeDepth;                   // for popping locals on break/continue
    std::vector<int> breakJumps;      // patched at loop end
  };
  ```

- [ ] **Step 1: Write behavior tests**

```rill
# tests/lang/control/while_counts.rl
# expect: 0
# expect: 1
# expect: 2
var i = 0
while i < 3 { print(i); i = i + 1; }
```

```rill
# tests/lang/control/while_yields_nil.rl
# expect: nil
var i = 0
print(while false { i })
```

```rill
# tests/lang/control/while_break.rl
# expect: 0
# expect: 1
var i = 0
while true {
  if i >= 2 { break; }
  print(i);
  i = i + 1;
}
```

```rill
# tests/lang/control/while_continue.rl
# expect: 1
# expect: 3
var i = 0
while i < 4 {
  i = i + 1;
  if i % 2 == 0 { continue; }
  print(i);
}
```

```rill
# tests/lang/control/break_outside_loop_is_error.rl
# expect error: 'break' outside of a loop
break
```

- [ ] **Step 2: Run, confirm failure**

- [ ] **Step 3: Implement `while`**

```
loopStart = currentChunk()->code.size()
push LoopContext{startOffset = loopStart, scopeDepth}
compile condition
exitJump = emitJump(JumpIfFalse)
emit Pop
compile body                  # leaves one value
emit Pop                      # while discards the body value
emitLoop(loopStart)
patchJump(exitJump)
emit Pop                      # discard condition
emit Nil                      # while yields nil
patch every breakJumps entry
pop LoopContext
```

- [ ] **Step 4: Implement `break` and `continue`**

Both are prefix parse rules. `break` pops locals down to the loop's scope depth, emits a `Jump` recorded in `breakJumps`, and then emits `Nil` so the surrounding expression still has a value. `continue` pops locals down to the loop's scope depth and emits `Loop` back to `startOffset`, then `Nil`. Both error when `currentLoop == nullptr`.

Note: because the jump is unconditional, the `Nil` emitted after it is dead code — but it keeps the compiler's stack-depth accounting honest and costs nothing at runtime.

- [ ] **Step 5: Implement `emitLoop`**

```cpp
static void emitLoop(int loopStart) {
  emitByte(static_cast<uint8_t>(OpCode::Loop));
  int offset = currentChunk()->code.size() - loopStart + 2;
  if (offset > UINT16_MAX) error("loop body too large");
  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}
```

- [ ] **Step 6: Run tests, verify pass**

- [ ] **Step 7: Commit**

```bash
git add src tests/lang/control
git commit -m "feat(control): while loops with break and continue"
```

---

### Task 13: Functions, calls, and native builtins

**Files:**
- Modify: `src/object.hpp`, `src/object.cpp`, `src/compiler.hpp`, `src/compiler.cpp`, `src/parser.cpp`, `src/vm.hpp`, `src/vm.cpp`, `src/chunk.hpp`
- Create: `src/builtins.cpp`, `tests/lang/functions/*.rl`

**Interfaces:**
- Produces:
  ```cpp
  struct ObjFunction : Obj { int arity; int upvalueCount; Chunk chunk; ObjString* name; };
  using NativeFn = bool (*)(int argCount, Value* args, Value* result);
  struct ObjNative : Obj { NativeFn function; ObjString* name; int arity; };
  struct CallFrame { ObjFunction* function; uint8_t* ip; Value* slots; };
  ObjFunction* newFunction();
  ObjNative* newNative(NativeFn fn, const char* name, int arity);
  void defineNative(const char* name, NativeFn fn, int arity);
  ```
  Opcodes `Call`, `Return`. `OpCode::Print` from Task 8 is **removed**; `print` becomes a native.

- [ ] **Step 1: Write behavior tests**

```rill
# tests/lang/functions/call_and_return.rl
# expect: 25
let square = fn(x) { x * x }
print(square(5))
```

```rill
# tests/lang/functions/implicit_return_of_last_expression.rl
# expect: 3
let add = fn(a, b) { a + b }
print(add(1, 2))
```

```rill
# tests/lang/functions/explicit_return.rl
# expect: 10
let clamp = fn(x, lo, hi) {
  if x < lo { return lo; }
  if x > hi { return hi; }
  x
}
print(clamp(99, 0, 10))
```

```rill
# tests/lang/functions/recursion.rl
# expect: 120
let fact = fn(n) { if n <= 1 { 1 } else { n * fact(n - 1) } }
print(fact(5))
```

```rill
# tests/lang/functions/arity_mismatch.rl
# expect error: expected 2 arguments but got 1
let f = fn(a, b) { a }
f(1)
```

```rill
# tests/lang/functions/call_non_function.rl
# expect error: can only call functions
let x = 1
x()
```

```rill
# tests/lang/functions/stack_trace_on_error.rl
# expect error: in fn 'inner'
let inner = fn() { undefined_global }
let outer = fn() { inner() }
outer()
```

- [ ] **Step 2: Run, confirm failure**

- [ ] **Step 3: Restructure the compiler around functions**

Every compilation now targets an `ObjFunction`, including top-level code (a synthetic function named `<script>`). `Compiler` gains `ObjFunction* function` and `FunctionType type`. `currentChunk()` returns `&current->function->chunk`.

**Critical:** `endCompiler` emits `Return` and returns the function. Local slot 0 is reserved for the function itself and must not be usable as a name.

- [ ] **Step 4: Implement `fn` as a prefix parse rule**

Parse the parameter list incrementing `function->arity` (error past 255), compile the body block as the function body, then emit a `Constant` referencing the completed function.

- [ ] **Step 5: Implement `Call` in the VM**

```cpp
case OpCode::Call: {
  int argCount = READ_BYTE();
  frame->ip = ip;                      // save before the frame changes
  if (!callValue(peek(argCount), argCount)) return InterpretResult::RuntimeError;
  frame = &frames_[frameCount_ - 1];
  ip = frame->ip;                      // reload cached ip
  break;
}
```

The cached `ip` local must be written back to the frame before any call and reloaded after — forgetting this is the most common VM bug at this stage.

- [ ] **Step 6: Implement `Return`**

Pop the result, close the frame, discard the callee's slots, push the result. If `frameCount_` hits 0, pop the script function and return `Ok`.

- [ ] **Step 7: Implement builtins in `src/builtins.cpp`**

`print` (arity 1, prints the value and a newline, returns nil), `clock` (arity 0, returns seconds as a double), `sqrt`, `floor`, `str`, `num`, `len`. A native returning `false` signals a runtime error with the message left in `*result` as a string.

Delete the `OpCode::Print` special case from Task 8.

- [ ] **Step 8: Implement the stack trace in `runtimeError`**

Walk `frames_` from the top down, printing `[line N] in fn 'NAME'` or `[line N] in script`.

- [ ] **Step 9: Run tests, verify pass**

Every test from Tasks 8–12 must still pass unchanged.

- [ ] **Step 10: Commit**

```bash
git add src tests/lang/functions
git commit -m "feat(functions): calls, frames, natives, stack traces"
```

---

### Task 14: Closures and upvalues

**Files:**
- Modify: `src/object.hpp`, `src/object.cpp`, `src/compiler.hpp`, `src/compiler.cpp`, `src/vm.hpp`, `src/vm.cpp`, `src/chunk.hpp`, `src/debug.cpp`
- Create: `tests/lang/closures/*.rl`

**Interfaces:**
- Produces:
  ```cpp
  struct ObjUpvalue : Obj { Value* location; Value closed; ObjUpvalue* next; };
  struct ObjClosure : Obj { ObjFunction* function; ObjUpvalue** upvalues; int upvalueCount; };
  struct Upvalue { uint8_t index; bool isLocal; };   // compile-time
  int resolveUpvalue(Compiler* c, Token* name);
  ```
  Opcodes `Closure`, `GetUpvalue`, `SetUpvalue`, `CloseUpvalue`. `CallFrame::function` becomes `CallFrame::closure`.

- [ ] **Step 1: Write behavior tests**

```rill
# tests/lang/closures/captures_enclosing_variable.rl
# expect: 3
let makeAdder = fn(n) { fn(x) { x + n } }
let add3 = makeAdder(3)
print(add3(0) + 3)
```

```rill
# tests/lang/closures/counter_shares_state.rl
# expect: 1
# expect: 2
let makeCounter = fn() {
  var count = 0;
  fn() { count = count + 1; count }
}
let c = makeCounter()
print(c())
print(c())
```

```rill
# tests/lang/closures/closed_upvalue_survives_scope_exit.rl
# expect: 42
let capture = fn() { let secret = 42; fn() { secret } }
let f = capture()
print(f())
```

```rill
# tests/lang/closures/two_closures_share_one_upvalue.rl
# expect: 1
# expect: 2
let make = fn() {
  var v = 0;
  let bump = fn() { v = v + 1; nil };
  fn() { bump(); v }
}
let f = make()
print(f())
print(f())
```

This is the sharing test: `bump` and the returned closure each capture `v`. If
`captureUpvalue` allocated a separate upvalue per closure instead of reusing
the one already open for that stack slot, the outer closure would not observe
`bump`'s increment and both lines would print `0`. It also exercises
`SetUpvalue`, which no other test in this task reaches.

```rill
# tests/lang/closures/independent_closures_have_independent_state.rl
# expect: 1
# expect: 1
let makeCounter = fn() { var n = 0; fn() { n = n + 1; n } }
let a = makeCounter()
let b = makeCounter()
print(a())
print(b())
```

```rill
# tests/lang/closures/deeply_nested_capture.rl
# expect: 6
let f = fn(a) { fn(b) { fn(c) { a + b + c } } }
print(f(1)(2)(3))
```

- [ ] **Step 2: Run, confirm failure**

- [ ] **Step 3: Implement `resolveUpvalue`**

```cpp
static int resolveUpvalue(Compiler* c, Token* name) {
  if (c->enclosing == nullptr) return -1;
  int local = resolveLocal(c->enclosing, name);
  if (local != -1) {
    c->enclosing->locals[local].isCaptured = true;
    return addUpvalue(c, static_cast<uint8_t>(local), true);
  }
  int upvalue = resolveUpvalue(c->enclosing, name);
  if (upvalue != -1) return addUpvalue(c, static_cast<uint8_t>(upvalue), false);
  return -1;
}
```

`addUpvalue` deduplicates by (index, isLocal) so repeated references share one upvalue slot.

- [ ] **Step 4: Emit `Closure` with its trailing operand pairs**

After the `Closure` opcode and the function's constant index, emit two bytes per upvalue: `isLocal` then `index`. The disassembler must be taught to skip these, or `--dump` desynchronizes.

- [ ] **Step 5: Implement `captureUpvalue` and `closeUpvalues` in the VM**

`openUpvalues_` is a linked list sorted by stack slot, descending. `captureUpvalue` searches it and reuses an existing upvalue for the same slot — this is what makes `two closures share one variable` work. `closeUpvalues(Value* last)` copies each open upvalue's value into its `closed` field and repoints `location` at it.

- [ ] **Step 6: Emit `CloseUpvalue` at scope exit**

`endScope` emits `CloseUpvalue` instead of a plain pop for any local with `isCaptured == true`. This interacts with the `CloseScope` opcode from Task 10 — locals must be closed individually before the scope collapses, so emit the `CloseUpvalue` instructions first, then `CloseScope` for the remainder.

- [ ] **Step 7: Run tests, verify pass**

Run: `docker compose run --rm dev bash -c "bash scripts/dev.sh build && bash scripts/dev.sh test"`
Expected: all unit and behavior tests pass.

- [ ] **Step 8: Update README with a language tour**

Document what works: bindings, blocks as expressions, `if`, `while`, functions, closures. Show the `docker compose run` commands.

- [ ] **Step 9: Commit**

```bash
git add src tests/lang/closures README.md
git commit -m "feat(closures): upvalue capture and closing"
```

---

## Phase 1 Exit Criteria

- [ ] `docker compose run --rm dev bash -c "bash scripts/dev.sh build && bash scripts/dev.sh test"` is green.
- [ ] CI passes on the {gcc, clang} x {Debug, Release} matrix.
- [ ] `-Wall -Wextra -Werror` produces no warnings.
- [ ] At least 35 behavior tests exist under `tests/lang/`.
- [ ] The REPL evaluates `let f = fn(x) { x * 2 }` then `f(21)` and prints `42`.
- [ ] No AST type exists anywhere in `src/`.

Known deferrals into Phase 2: `match`, the garbage collector (Phase 1 leaks by design — `freeObjects()` at exit is the only reclamation), map literals, prototype objects, and property access.
