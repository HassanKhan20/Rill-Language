#pragma once

#include <cstdint>

#include "value.hpp"

namespace rill {

struct ObjString;  // Defined in object.hpp.

struct Entry {
  ObjString* key;
  Value value;
};

// Open addressing with linear probing. A deleted slot leaves a tombstone
// (key == nullptr, value == true) so that probe chains running through it stay
// intact; an empty slot is (nullptr, nil) and terminates a probe.
class Table {
 public:
  Table() = default;
  ~Table();

  Table(const Table&) = delete;
  Table& operator=(const Table&) = delete;

  bool get(ObjString* key, Value* out) const;

  // Returns true when the key is newly inserted, false when it replaced an
  // existing entry.
  bool set(ObjString* key, Value value);

  bool remove(ObjString* key);

  // Finds an interned string by content. This is what makes interning
  // possible: it compares hash, then length, then bytes, rather than by
  // pointer.
  ObjString* findString(const char* chars, int length, uint32_t hash) const;

  // Drops every entry whose key survived no mark. The intern table holds weak
  // references, so the collector calls this after marking and before sweeping.
  void removeWhite();

  int count = 0;
  int capacity = 0;
  Entry* entries = nullptr;

 private:
  void adjustCapacity(int newCapacity);
};

}  // namespace rill
