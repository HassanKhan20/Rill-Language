#include <cstdio>

#include "object.hpp"
#include "table.hpp"
#include "test_harness.hpp"
#include "value.hpp"

using namespace rill;

TEST(table_set_then_get) {
  Table t;
  ObjString* k = rilltest::keepString(copyString("key", 3));
  CHECK(t.set(k, numberValue(7)));
  Value out;
  CHECK(t.get(k, &out));
  CHECK_EQ(asNumber(out), 7.0);
}

TEST(table_get_on_empty_table_misses) {
  Table t;
  Value out;
  CHECK(!t.get(copyString("absent", 6), &out));
}

TEST(table_set_existing_key_returns_false_and_overwrites) {
  Table t;
  ObjString* k = rilltest::keepString(copyString("dup", 3));
  t.set(k, numberValue(1));
  CHECK(!t.set(k, numberValue(2)));
  Value out;
  t.get(k, &out);
  CHECK_EQ(asNumber(out), 2.0);
  CHECK_EQ(t.count, 1);
}

TEST(table_remove_reports_whether_it_removed_anything) {
  Table t;
  ObjString* k = rilltest::keepString(copyString("gone", 4));
  CHECK(!t.remove(k));
  t.set(k, numberValue(1));
  CHECK(t.remove(k));
  Value out;
  CHECK(!t.get(k, &out));
}

TEST(table_tombstone_keeps_the_probe_chain_intact) {
  // a and b may collide; removing a must not orphan b.
  Table t;
  ObjString* a = rilltest::keepString(copyString("a", 1));
  ObjString* b = rilltest::keepString(copyString("b", 1));
  t.set(a, numberValue(1));
  t.set(b, numberValue(2));
  CHECK(t.remove(a));
  Value out;
  CHECK(!t.get(a, &out));
  CHECK(t.get(b, &out));
  CHECK_EQ(asNumber(out), 2.0);
}

TEST(table_grows_and_keeps_every_entry) {
  Table t;
  for (int i = 0; i < 64; i++) {
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "k%d", i);
    t.set(rilltest::keepString(copyString(buf, n)), numberValue(i));
  }
  CHECK_EQ(t.count, 64);
  for (int i = 0; i < 64; i++) {
    char buf[16];
    int n = std::snprintf(buf, sizeof(buf), "k%d", i);
    Value out;
    CHECK(t.get(copyString(buf, n), &out));
    CHECK_EQ(asNumber(out), static_cast<double>(i));
  }
}

TEST(table_reuses_tombstones_rather_than_growing_forever) {
  Table t;
  ObjString* k = rilltest::keepString(copyString("churn", 5));
  for (int i = 0; i < 200; i++) {
    t.set(k, numberValue(i));
    t.remove(k);
  }
  CHECK(t.capacity <= 8);
}

TEST(table_find_string_matches_by_content) {
  Table t;
  ObjString* k = rilltest::keepString(copyString("needle", 6));
  t.set(k, nilValue());
  uint32_t h = hashString("needle", 6);
  CHECK(t.findString("needle", 6, h) == k);
  CHECK(t.findString("needl", 5, hashString("needl", 5)) == nullptr);
}
