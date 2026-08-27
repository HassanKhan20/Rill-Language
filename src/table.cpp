#include "table.hpp"

#include <cstdlib>
#include <cstring>

#include "object.hpp"

namespace rill {

namespace {

constexpr double kMaxLoad = 0.75;

// Returns the slot for `key`: either the entry holding it, or the slot where
// it belongs. Reuses the first tombstone seen so deletions do not permanently
// consume capacity.
Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
  uint32_t index = key->hash & static_cast<uint32_t>(capacity - 1);
  Entry* tombstone = nullptr;

  for (;;) {
    Entry* entry = &entries[index];
    if (entry->key == nullptr) {
      if (isNil(entry->value)) {
        // A genuinely empty slot ends the probe.
        return tombstone != nullptr ? tombstone : entry;
      }
      if (tombstone == nullptr) tombstone = entry;
    } else if (entry->key == key) {
      return entry;
    }
    index = (index + 1) & static_cast<uint32_t>(capacity - 1);
  }
}

}  // namespace

Table::~Table() {
  std::free(entries);
  entries = nullptr;
  capacity = 0;
  count = 0;
}

void Table::adjustCapacity(int newCapacity) {
  auto* fresh = static_cast<Entry*>(
      std::malloc(sizeof(Entry) * static_cast<size_t>(newCapacity)));
  for (int i = 0; i < newCapacity; i++) {
    fresh[i].key = nullptr;
    fresh[i].value = nilValue();
  }

  // Reinsert live entries. Tombstones are dropped, so count is rebuilt.
  count = 0;
  for (int i = 0; i < capacity; i++) {
    Entry* src = &entries[i];
    if (src->key == nullptr) continue;
    Entry* dest = findEntry(fresh, newCapacity, src->key);
    dest->key = src->key;
    dest->value = src->value;
    count++;
  }

  std::free(entries);
  entries = fresh;
  capacity = newCapacity;
}

bool Table::get(ObjString* key, Value* out) const {
  if (count == 0) return false;
  Entry* entry = findEntry(entries, capacity, key);
  if (entry->key == nullptr) return false;
  *out = entry->value;
  return true;
}

bool Table::set(ObjString* key, Value value) {
  if (static_cast<double>(count + 1) > static_cast<double>(capacity) * kMaxLoad) {
    adjustCapacity(capacity < 8 ? 8 : capacity * 2);
  }

  Entry* entry = findEntry(entries, capacity, key);
  bool isNewKey = entry->key == nullptr;
  // A tombstone already counts toward the load factor, so only a genuinely
  // empty slot increments the count.
  if (isNewKey && isNil(entry->value)) count++;

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool Table::remove(ObjString* key) {
  if (count == 0) return false;
  Entry* entry = findEntry(entries, capacity, key);
  if (entry->key == nullptr) return false;

  entry->key = nullptr;
  entry->value = boolValue(true);  // Tombstone.
  return true;
}

ObjString* Table::findString(const char* chars, int length,
                             uint32_t hash) const {
  if (count == 0) return nullptr;

  uint32_t index = hash & static_cast<uint32_t>(capacity - 1);
  for (;;) {
    Entry* entry = &entries[index];
    if (entry->key == nullptr) {
      if (isNil(entry->value)) return nullptr;
    } else if (entry->key->length == length && entry->key->hash == hash &&
               std::memcmp(entry->key->chars, chars,
                           static_cast<size_t>(length)) == 0) {
      return entry->key;
    }
    index = (index + 1) & static_cast<uint32_t>(capacity - 1);
  }
}

void Table::removeWhite() {
  for (int i = 0; i < capacity; i++) {
    Entry* entry = &entries[i];
    if (entry->key != nullptr && !entry->key->isMarked) {
      remove(entry->key);
    }
  }
}

}  // namespace rill
