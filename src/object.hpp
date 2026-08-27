#pragma once

#include <cstdint>

#include "chunk.hpp"
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

struct ObjFunction : Obj {
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name;  // nullptr for the top-level script.
};

// A native returns false to signal a runtime error, leaving a message string
// in *result.
using NativeFn = bool (*)(int argCount, Value* args, Value* result);

struct ObjNative : Obj {
  NativeFn function;
  ObjString* name;
  int arity;  // -1 accepts any number of arguments.
};

// An upvalue points at a variable that outlives the stack slot holding it.
// While the slot is live, `location` points into the value stack ("open");
// once the slot dies the value is copied into `closed` and `location` is
// repointed there.
struct ObjUpvalue : Obj {
  Value* location;
  Value closed;
  ObjUpvalue* next;  // Open upvalues, sorted by slot, highest first.
};

struct ObjClosure : Obj {
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
};

inline bool isObjType(Value v, ObjType t) {
  return isObj(v) && asObj(v)->type == t;
}

inline bool isString(Value v) { return isObjType(v, ObjType::String); }
inline ObjString* asString(Value v) {
  return reinterpret_cast<ObjString*>(asObj(v));
}
inline const char* asCString(Value v) { return asString(v)->chars; }

inline bool isFunction(Value v) { return isObjType(v, ObjType::Function); }
inline ObjFunction* asFunction(Value v) {
  return reinterpret_cast<ObjFunction*>(asObj(v));
}

inline bool isNative(Value v) { return isObjType(v, ObjType::Native); }
inline ObjNative* asNative(Value v) {
  return reinterpret_cast<ObjNative*>(asObj(v));
}

inline bool isClosure(Value v) { return isObjType(v, ObjType::Closure); }
inline ObjClosure* asClosure(Value v) {
  return reinterpret_cast<ObjClosure*>(asObj(v));
}

ObjFunction* newFunction();
ObjNative* newNative(NativeFn function, const char* name, int arity);
ObjClosure* newClosure(ObjFunction* function);
ObjUpvalue* newUpvalue(Value* slot);

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
