#pragma once

#include <cstddef>

#include "value.hpp"

namespace rill {

struct Obj;

// Called before every object allocation. Under RILL_GC_STRESS it collects
// unconditionally; otherwise it collects once allocation passes a threshold
// that doubles after each collection.
void maybeCollect(size_t newBytes);

void collectGarbage();

// Accounting, so the collector knows when to run and benchmarks can report it.
void recordAllocation(size_t bytes);
void recordFree(size_t bytes);
size_t bytesAllocated();
size_t collectionCount();

// --- Marking --------------------------------------------------------------

void markObject(Obj* object);
void markValue(Value value);

// --- Temporary roots ------------------------------------------------------
//
// An object that has been allocated but not yet stored anywhere reachable is
// invisible to the collector and will be freed out from under its creator.
// Any code that allocates twice before linking the first result into the
// object graph must root the first one across the second allocation.
void pushTempRoot(Value value);
void popTempRoot();

// An optional extra root enumerator. Unit tests use this to root objects they
// hold in plain C++ locals, which the collector otherwise cannot see.
using RootMarkerFn = void (*)();
void setExtraRootMarker(RootMarkerFn fn);

// Implemented by vm.cpp and compiler.cpp respectively: the collector cannot
// see their internals, so each enumerates its own roots.
void markVMRoots();
void markCompilerRoots();

}  // namespace rill
