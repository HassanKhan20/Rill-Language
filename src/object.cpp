#include "object.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "table.hpp"

namespace rill {

namespace {

// The allocation list and the intern table. Phase 2 moves both onto the VM so
// the collector can reach them; keeping them file-scoped here means Phase 1
// needs no VM instance to allocate a string.
Obj* g_objects = nullptr;
Table g_strings;

Obj* allocateObject(size_t size, ObjType type) {
  auto* object = static_cast<Obj*>(std::malloc(size));
  object->type = type;
  object->isMarked = false;
  object->next = g_objects;
  g_objects = object;
  return object;
}

ObjString* allocateString(char* chars, int length, uint32_t hash) {
  auto* string = reinterpret_cast<ObjString*>(
      allocateObject(sizeof(ObjString), ObjType::String));
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  g_strings.set(string, nilValue());
  return string;
}

void freeObject(Obj* object) {
  switch (object->type) {
    case ObjType::String: {
      auto* string = reinterpret_cast<ObjString*>(object);
      std::free(string->chars);
      std::free(string);
      break;
    }
    case ObjType::Function:
    case ObjType::Closure:
    case ObjType::Upvalue:
    case ObjType::Native:
    case ObjType::Map:
      std::free(object);
      break;
  }
}

}  // namespace

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

void printObject(Value v) {
  switch (asObj(v)->type) {
    case ObjType::String:
      std::printf("%s", asCString(v));
      break;
    case ObjType::Function:
    case ObjType::Closure:
    case ObjType::Upvalue:
    case ObjType::Native:
    case ObjType::Map:
      std::printf("<object>");
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
