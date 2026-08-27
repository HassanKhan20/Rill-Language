#include "value.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "object.hpp"

namespace rill {

bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case ValueType::Nil:    return true;
    case ValueType::Bool:   return asBool(a) == asBool(b);
    case ValueType::Number: return asNumber(a) == asNumber(b);
    // Pointer identity. For strings this is also value equality, because
    // every string is interned (see object.cpp) — there is exactly one
    // ObjString for any given character sequence.
    case ValueType::Obj:    return asObj(a) == asObj(b);
  }
  return false;
}

int formatNumber(double value, char* buffer, size_t size) {
  if (std::isnan(value)) return std::snprintf(buffer, size, "nan");
  if (std::isinf(value)) {
    return std::snprintf(buffer, size, value < 0 ? "-inf" : "inf");
  }

  // Integral values print as integers. 2^53 is where doubles stop being able
  // to represent consecutive integers, so beyond it %.0f would be misleading
  // precision and the general path is used instead.
  if (value == std::floor(value) && std::fabs(value) < 9007199254740992.0) {
    return std::snprintf(buffer, size, "%.0f", value);
  }

  // Shortest round-tripping form: try increasing precision until the text
  // reads back as exactly this double.
  for (int precision = 15; precision <= 17; precision++) {
    int n = std::snprintf(buffer, size, "%.*g", precision, value);
    if (std::strtod(buffer, nullptr) == value) return n;
  }
  return std::snprintf(buffer, size, "%.17g", value);
}

void printValue(Value v) {
  switch (v.type) {
    case ValueType::Nil:
      std::printf("nil");
      break;
    case ValueType::Bool:
      std::printf(asBool(v) ? "true" : "false");
      break;
    case ValueType::Number: {
      char buffer[32];
      formatNumber(asNumber(v), buffer, sizeof(buffer));
      std::printf("%s", buffer);
      break;
    }
    case ValueType::Obj:
      printObject(v);
      break;
  }
}

}  // namespace rill
