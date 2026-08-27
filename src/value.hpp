#pragma once

#include <cstddef>
#include <vector>

namespace rill {

struct Obj;  // Defined in object.hpp.

enum class ValueType { Nil, Bool, Number, Obj };

// Phase 1 uses a readable tagged union. A NaN-boxed representation lands later
// behind RILL_NANBOX; every access in the codebase goes through the helpers
// below so that swap touches only this header and value.cpp.
struct Value {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
};

inline Value nilValue() {
  Value v;
  v.type = ValueType::Nil;
  v.as.number = 0;
  return v;
}

inline Value boolValue(bool b) {
  Value v;
  v.type = ValueType::Bool;
  v.as.boolean = b;
  return v;
}

inline Value numberValue(double d) {
  Value v;
  v.type = ValueType::Number;
  v.as.number = d;
  return v;
}

inline Value objValue(Obj* o) {
  Value v;
  v.type = ValueType::Obj;
  v.as.obj = o;
  return v;
}

inline bool isNil(Value v) { return v.type == ValueType::Nil; }
inline bool isBool(Value v) { return v.type == ValueType::Bool; }
inline bool isNumber(Value v) { return v.type == ValueType::Number; }
inline bool isObj(Value v) { return v.type == ValueType::Obj; }

inline bool asBool(Value v) { return v.as.boolean; }
inline double asNumber(Value v) { return v.as.number; }
inline Obj* asObj(Value v) { return v.as.obj; }

// Only nil and false are falsey; 0 and the empty string are truthy.
inline bool isFalsey(Value v) {
  return isNil(v) || (isBool(v) && !asBool(v));
}

bool valuesEqual(Value a, Value b);

// Writes the value in its user-facing form: strings without surrounding
// quotes, numbers via formatNumber below.
void printValue(Value v);

// Formats a double the way Rill prints it, into `buffer`, returning the
// length. Integral values print without a decimal point; everything else uses
// the shortest representation that reads back as the same double. Plain "%g"
// is not good enough: it caps at six significant digits, so 8999994000000
// would print as 8.99999e+12.
int formatNumber(double value, char* buffer, size_t size);

}  // namespace rill
