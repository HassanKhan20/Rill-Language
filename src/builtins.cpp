#include "builtins.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "object.hpp"
#include "value.hpp"
#include "vm.hpp"

namespace rill {

namespace {

// Natives report errors by returning false with a message string in *result.
bool fail(Value* result, const char* message) {
  *result = objValue(copyString(message, static_cast<int>(std::strlen(message))));
  return false;
}

bool nativePrint(int, Value* args, Value* result) {
  printValue(args[0]);
  std::printf("\n");
  *result = nilValue();
  return true;
}

bool nativeClock(int, Value*, Value* result) {
  *result = numberValue(static_cast<double>(std::clock()) / CLOCKS_PER_SEC);
  return true;
}

bool nativeSqrt(int, Value* args, Value* result) {
  if (!isNumber(args[0])) return fail(result, "sqrt() expects a number");
  *result = numberValue(std::sqrt(asNumber(args[0])));
  return true;
}

bool nativeFloor(int, Value* args, Value* result) {
  if (!isNumber(args[0])) return fail(result, "floor() expects a number");
  *result = numberValue(std::floor(asNumber(args[0])));
  return true;
}

bool nativeLen(int, Value* args, Value* result) {
  if (!isString(args[0])) return fail(result, "len() expects a string");
  *result = numberValue(static_cast<double>(asString(args[0])->length));
  return true;
}

bool nativeStr(int, Value* args, Value* result) {
  Value v = args[0];
  if (isString(v)) {
    *result = v;
    return true;
  }
  char buffer[64];
  int n = 0;
  if (isNumber(v)) {
    n = std::snprintf(buffer, sizeof(buffer), "%g", asNumber(v));
  } else if (isBool(v)) {
    n = std::snprintf(buffer, sizeof(buffer), "%s",
                      asBool(v) ? "true" : "false");
  } else if (isNil(v)) {
    n = std::snprintf(buffer, sizeof(buffer), "nil");
  } else {
    return fail(result, "str() cannot convert this value");
  }
  *result = objValue(copyString(buffer, n));
  return true;
}

bool nativeNum(int, Value* args, Value* result) {
  if (isNumber(args[0])) {
    *result = args[0];
    return true;
  }
  if (!isString(args[0])) return fail(result, "num() expects a string");

  ObjString* s = asString(args[0]);
  char* end = nullptr;
  double d = std::strtod(s->chars, &end);
  if (end == s->chars || *end != '\0') {
    return fail(result, "num() could not parse this string");
  }
  *result = numberValue(d);
  return true;
}

}  // namespace

void defineBuiltins(VM& vm) {
  vm.defineNative("print", nativePrint, 1);
  vm.defineNative("clock", nativeClock, 0);
  vm.defineNative("sqrt", nativeSqrt, 1);
  vm.defineNative("floor", nativeFloor, 1);
  vm.defineNative("len", nativeLen, 1);
  vm.defineNative("str", nativeStr, 1);
  vm.defineNative("num", nativeNum, 1);
}

}  // namespace rill
