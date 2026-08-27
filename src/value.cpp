#include "value.hpp"

#include <cstdio>

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

void printValue(Value v) {
  switch (v.type) {
    case ValueType::Nil:
      std::printf("nil");
      break;
    case ValueType::Bool:
      std::printf(asBool(v) ? "true" : "false");
      break;
    case ValueType::Number:
      std::printf("%g", asNumber(v));
      break;
    case ValueType::Obj:
      // Objects gain their own printing when the object model lands; nothing
      // can construct one yet.
      std::printf("<object>");
      break;
  }
}

}  // namespace rill
