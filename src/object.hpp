#pragma once

#include <cstdint>

#include "value.hpp"

namespace rill {

enum class ObjType { String, Function, Closure, Upvalue, Native, Map };

// Every heap object begins with this header and is threaded onto a global
// intrusive list at allocation. The collector added in Phase 2 walks that list
// to sweep, which is why every allocation must go through allocateObject.
struct Obj {
  ObjType type;
  bool isMarked;
  Obj* next;
};

struct ObjString : Obj {
  int length;
  char* chars;
  uint32_t hash;
};

inline bool isObjType(Value v, ObjType t) {
  return isObj(v) && asObj(v)->type == t;
}

inline bool isString(Value v) { return isObjType(v, ObjType::String); }
inline ObjString* asString(Value v) {
  return reinterpret_cast<ObjString*>(asObj(v));
}
inline const char* asCString(Value v) { return asString(v)->chars; }

uint32_t hashString(const char* key, int length);

// Interns and returns a string with a copy of the given characters.
ObjString* copyString(const char* chars, int length);

// Interns and returns a string, taking ownership of `chars` (which must have
// come from malloc). Frees `chars` if an equal string is already interned.
ObjString* takeString(char* chars, int length);

void printObject(Value v);

// Frees every object on the allocation list. Called at VM teardown so that a
// valgrind run reports only genuine leaks.
void freeObjects();

// Test-only: the number of objects currently on the allocation list.
int liveObjectCount();

}  // namespace rill
