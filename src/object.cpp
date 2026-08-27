#include "object.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "gc.hpp"
#include "table.hpp"

namespace rill {

Obj* g_objects = nullptr;
Table g_strings;

namespace {

// The single allocation choke point. Every heap object passes through here,
// which is what lets the collector sweep a complete list.
Obj* allocateObject(size_t size, ObjType type) {
  maybeCollect(size);

  auto* object = static_cast<Obj*>(std::malloc(size));
  object->type = type;
  object->isMarked = false;
  object->next = g_objects;
  g_objects = object;

  recordAllocation(size);
  return object;
}

ObjString* allocateString(char* chars, int length, uint32_t hash) {
  auto* string = reinterpret_cast<ObjString*>(
      allocateObject(sizeof(ObjString), ObjType::String));
  string->length = length;
  string->chars = chars;
  string->hash = hash;

  // The intern table is weak, so this does not keep the string alive. set()
  // allocates only raw memory, never objects, so it cannot collect here.
  g_strings.set(string, nilValue());
  return string;
}

size_t objectSize(Obj* object) {
  switch (object->type) {
    case ObjType::String:   return sizeof(ObjString);
    case ObjType::Function: return sizeof(ObjFunction);
    case ObjType::Closure:  return sizeof(ObjClosure);
    case ObjType::Upvalue:  return sizeof(ObjUpvalue);
    case ObjType::Native:   return sizeof(ObjNative);
    case ObjType::Map:      return sizeof(ObjMap);
  }
  return 0;
}

}  // namespace

void freeObject(Obj* object) {
  recordFree(objectSize(object));

  switch (object->type) {
    case ObjType::String: {
      auto* string = reinterpret_cast<ObjString*>(object);
      std::free(string->chars);
      std::free(string);
      break;
    }
    case ObjType::Function: {
      // The Chunk owns std::vectors, so it needs its destructor run rather
      // than a bare free().
      auto* function = reinterpret_cast<ObjFunction*>(object);
      function->~ObjFunction();
      std::free(object);
      break;
    }
    case ObjType::Closure: {
      // The closure owns its upvalue array but not the upvalues themselves,
      // which may still be shared with other closures.
      auto* closure = reinterpret_cast<ObjClosure*>(object);
      std::free(closure->upvalues);
      std::free(object);
      break;
    }
    case ObjType::Map: {
      auto* map = reinterpret_cast<ObjMap*>(object);
      map->~ObjMap();
      std::free(object);
      break;
    }
    case ObjType::Upvalue:
    case ObjType::Native:
      std::free(object);
      break;
  }
}

uint32_t hashString(const char* key, int length) {
  // FNV-1a, 32-bit.
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= static_cast<uint8_t>(key[i]);
    hash *= 16777619u;
  }
  return hash;
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = g_strings.findString(chars, length, hash);
  if (interned != nullptr) return interned;

  auto* heapChars =
      static_cast<char*>(std::malloc(static_cast<size_t>(length) + 1));
  std::memcpy(heapChars, chars, static_cast<size_t>(length));
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash);
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = g_strings.findString(chars, length, hash);
  if (interned != nullptr) {
    std::free(chars);
    return interned;
  }
  return allocateString(chars, length, hash);
}

ObjFunction* newFunction() {
  auto* function = reinterpret_cast<ObjFunction*>(
      allocateObject(sizeof(ObjFunction), ObjType::Function));
  // allocateObject hands back raw memory, so the Chunk's vectors must be
  // constructed in place before anything touches them.
  new (&function->chunk) Chunk();
  function->arity = 0;
  function->upvalueCount = 0;
  function->name = nullptr;
  return function;
}

ObjNative* newNative(NativeFn fn, const char* name, int arity) {
  ObjString* nameStr = copyString(name, static_cast<int>(std::strlen(name)));
  // nameStr is not reachable from anything yet and allocateObject below can
  // collect, so it has to be rooted across that allocation.
  pushTempRoot(objValue(nameStr));
  auto* native = reinterpret_cast<ObjNative*>(
      allocateObject(sizeof(ObjNative), ObjType::Native));
  popTempRoot();

  native->function = fn;
  native->arity = arity;
  native->name = nameStr;
  return native;
}

ObjClosure* newClosure(ObjFunction* function) {
  // +1 so a zero-upvalue closure still gets a non-null pointer to free.
  auto** upvalues = static_cast<ObjUpvalue**>(std::malloc(
      sizeof(ObjUpvalue*) * (static_cast<size_t>(function->upvalueCount) + 1)));
  for (int i = 0; i < function->upvalueCount; i++) upvalues[i] = nullptr;

  // The function may be reachable only through the caller's local variable.
  pushTempRoot(objValue(function));
  auto* closure = reinterpret_cast<ObjClosure*>(
      allocateObject(sizeof(ObjClosure), ObjType::Closure));
  popTempRoot();

  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
  auto* upvalue = reinterpret_cast<ObjUpvalue*>(
      allocateObject(sizeof(ObjUpvalue), ObjType::Upvalue));
  upvalue->location = slot;
  upvalue->closed = nilValue();
  upvalue->next = nullptr;
  return upvalue;
}

ObjMap* newMap(ObjMap* prototype) {
  if (prototype != nullptr) pushTempRoot(objValue(prototype));
  auto* map =
      reinterpret_cast<ObjMap*>(allocateObject(sizeof(ObjMap), ObjType::Map));
  if (prototype != nullptr) popTempRoot();

  new (&map->fields) Table();
  map->prototype = prototype;
  return map;
}

bool mapGet(ObjMap* map, ObjString* name, Value* out) {
  for (ObjMap* m = map; m != nullptr; m = m->prototype) {
    if (m->fields.get(name, out)) return true;
  }
  return false;
}

void printObject(Value v) {
  switch (asObj(v)->type) {
    case ObjType::String:
      std::printf("%s", asCString(v));
      break;
    case ObjType::Function: {
      ObjFunction* fn = asFunction(v);
      if (fn->name == nullptr) {
        std::printf("<script>");
      } else {
        std::printf("<fn %s>", fn->name->chars);
      }
      break;
    }
    case ObjType::Native:
      std::printf("<native %s>", asNative(v)->name->chars);
      break;
    case ObjType::Closure:
      printObject(objValue(asClosure(v)->function));
      break;
    case ObjType::Map:
      std::printf("<map>");
      break;
    case ObjType::Upvalue:
      std::printf("<upvalue>");
      break;
  }
}

void freeObjects() {
  Obj* object = g_objects;
  while (object != nullptr) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
  g_objects = nullptr;

  // The intern table's keys are now dangling; drop its storage outright.
  std::free(g_strings.entries);
  g_strings.entries = nullptr;
  g_strings.count = 0;
  g_strings.capacity = 0;
}

int liveObjectCount() {
  int n = 0;
  for (Obj* o = g_objects; o != nullptr; o = o->next) n++;
  return n;
}

}  // namespace rill
