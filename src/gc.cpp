#include "gc.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "common.hpp"
#include "object.hpp"
#include "table.hpp"

namespace rill {

namespace {

size_t g_bytesAllocated = 0;
size_t g_nextGC = 1024 * 1024;
size_t g_collections = 0;

// The gray worklist: objects that are marked but whose references have not
// been traced yet. Kept outside the collected heap so that tracing never
// allocates.
std::vector<Obj*> g_grayStack;

// Objects allocated but not yet reachable from any other root.
Value g_tempRoots[64];
int g_tempRootCount = 0;

RootMarkerFn g_extraRootMarker = nullptr;

constexpr size_t kHeapGrowthFactor = 2;

void markTable(Table& table) {
  for (int i = 0; i < table.capacity; i++) {
    Entry* entry = &table.entries[i];
    if (entry->key != nullptr) markObject(reinterpret_cast<Obj*>(entry->key));
    markValue(entry->value);
  }
}

void markArray(std::vector<Value>& array) {
  for (Value v : array) markValue(v);
}

// Traces an object's references, turning it from gray to black.
void blackenObject(Obj* object) {
  switch (object->type) {
    // No outgoing references.
    case ObjType::String:
    case ObjType::Native:
      break;

    case ObjType::Upvalue:
      markValue(reinterpret_cast<ObjUpvalue*>(object)->closed);
      break;

    case ObjType::Function: {
      auto* function = reinterpret_cast<ObjFunction*>(object);
      markObject(reinterpret_cast<Obj*>(function->name));
      markArray(function->chunk.constants);
      break;
    }

    case ObjType::Closure: {
      auto* closure = reinterpret_cast<ObjClosure*>(object);
      markObject(reinterpret_cast<Obj*>(closure->function));
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject(reinterpret_cast<Obj*>(closure->upvalues[i]));
      }
      break;
    }

    case ObjType::Map: {
      auto* map = reinterpret_cast<ObjMap*>(object);
      markObject(reinterpret_cast<Obj*>(map->prototype));
      markTable(map->fields);
      break;
    }
  }
}

void markRoots() {
  markVMRoots();
  markCompilerRoots();
  for (int i = 0; i < g_tempRootCount; i++) markValue(g_tempRoots[i]);
  if (g_extraRootMarker != nullptr) g_extraRootMarker();
}

void traceReferences() {
  while (!g_grayStack.empty()) {
    Obj* object = g_grayStack.back();
    g_grayStack.pop_back();
    blackenObject(object);
  }
}

void sweep() {
  Obj* previous = nullptr;
  Obj* object = g_objects;
  while (object != nullptr) {
    if (object->isMarked) {
      object->isMarked = false;  // Reset for the next collection.
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous == nullptr) {
        g_objects = object;
      } else {
        previous->next = object;
      }
      freeObject(unreached);
    }
  }
}

}  // namespace

void setExtraRootMarker(RootMarkerFn fn) { g_extraRootMarker = fn; }

void recordAllocation(size_t bytes) { g_bytesAllocated += bytes; }

void recordFree(size_t bytes) {
  // Guard against underflow if accounting ever drifts.
  g_bytesAllocated = bytes > g_bytesAllocated ? 0 : g_bytesAllocated - bytes;
}

size_t bytesAllocated() { return g_bytesAllocated; }
size_t collectionCount() { return g_collections; }

void markObject(Obj* object) {
  if (object == nullptr || object->isMarked) return;
  object->isMarked = true;
  g_grayStack.push_back(object);
}

void markValue(Value value) {
  if (isObj(value)) markObject(asObj(value));
}

void pushTempRoot(Value value) {
  if (g_tempRootCount < static_cast<int>(sizeof(g_tempRoots) / sizeof(Value))) {
    g_tempRoots[g_tempRootCount++] = value;
  }
}

void popTempRoot() {
  if (g_tempRootCount > 0) g_tempRootCount--;
}

void maybeCollect(size_t newBytes) {
#ifdef RILL_GC_STRESS
  (void)newBytes;
  collectGarbage();
#else
  if (g_bytesAllocated + newBytes > g_nextGC) collectGarbage();
#endif
}

void collectGarbage() {
  g_collections++;

  markRoots();
  traceReferences();

  // The intern table holds weak references. Its unmarked entries must be
  // removed after marking and before sweeping, or the table would be left
  // pointing at freed strings.
  g_strings.removeWhite();

  sweep();

  g_nextGC = g_bytesAllocated * kHeapGrowthFactor;
  if (g_nextGC < 1024 * 1024) g_nextGC = 1024 * 1024;
}

}  // namespace rill
