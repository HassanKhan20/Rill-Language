#include "object.hpp"
#include "test_harness.hpp"

int main() {
  int status = ::rilltest::runAll();
  // Tests allocate interned strings; releasing them here keeps valgrind
  // output limited to genuine defects.
  rill::freeObjects();
  return status;
}
