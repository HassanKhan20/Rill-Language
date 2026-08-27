#pragma once

// A minimal test harness. The project forbids third-party libraries, so this
// stands in for gtest/Catch2: tests self-register via a static Registrar, and
// runAll() reports a pass count and returns a process exit status.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace rilltest {

struct Test {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<Test>& registry() {
  static std::vector<Test> r;
  return r;
}

inline int& failures() {
  static int f = 0;
  return f;
}

inline const char*& currentTest() {
  static const char* t = "";
  return t;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

inline void reportFailure(const char* file, int line, const std::string& msg) {
  std::printf("  FAIL %s\n    %s:%d: %s\n", currentTest(), file, line,
              msg.c_str());
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

#define TEST(name)                                        \
  static void name();                                     \
  static ::rilltest::Registrar reg_##name(#name, name);   \
  static void name()

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      ::rilltest::reportFailure(__FILE__, __LINE__,                    \
                                "CHECK failed: " #cond);               \
    }                                                                  \
  } while (false)

#define CHECK_EQ(a, b)                                                 \
  do {                                                                 \
    auto va_ = (a);                                                    \
    auto vb_ = (b);                                                    \
    if (!(va_ == vb_)) {                                               \
      ::rilltest::reportFailure(__FILE__, __LINE__,                    \
                                "CHECK_EQ failed: " #a " != " #b);     \
    }                                                                  \
  } while (false)
