#include <vector>

#include "gc.hpp"
#include "object.hpp"
#include "test_harness.hpp"

namespace rilltest {

std::vector<::rill::Value>& roots() {
  static std::vector<::rill::Value> r;
  return r;
}

}  // namespace rilltest

namespace {

void markTestRoots() {
  for (::rill::Value v : rilltest::roots()) ::rill::markValue(v);
}

}  // namespace

// The unit tests exercise the object model directly, below the VM, so nothing
// the collector normally scans can see the objects they create. Registering a
// marker here is what lets these tests run under RILL_GC_STRESS.
int main() {
  ::rill::setExtraRootMarker(markTestRoots);

  int status = ::rilltest::runAll();

  rilltest::roots().clear();
  ::rill::freeObjects();
  return status;
}
