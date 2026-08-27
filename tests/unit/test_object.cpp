#include <cstring>

#include "object.hpp"
#include "test_harness.hpp"
#include "value.hpp"

using namespace rill;

TEST(strings_with_equal_contents_are_the_same_object) {
  ObjString* a = copyString("hello", 5);
  ObjString* b = copyString("hello", 5);
  CHECK(a == b);
}

TEST(strings_with_different_contents_are_distinct) {
  ObjString* a = copyString("hello", 5);
  ObjString* b = copyString("world", 5);
  CHECK(a != b);
}

TEST(interning_makes_string_equality_pointer_equality) {
  Value a = objValue(copyString("x", 1));
  Value b = objValue(copyString("x", 1));
  CHECK(valuesEqual(a, b));
}

TEST(string_stores_length_and_nul_terminates) {
  ObjString* s = copyString("abc", 3);
  CHECK_EQ(s->length, 3);
  CHECK_EQ(std::strcmp(s->chars, "abc"), 0);
}

TEST(embedded_nul_does_not_truncate_the_string) {
  ObjString* s = copyString("a\0b", 3);
  CHECK_EQ(s->length, 3);
  CHECK_EQ(s->chars[1], '\0');
  CHECK_EQ(s->chars[2], 'b');
}

TEST(take_string_reuses_an_interned_copy) {
  ObjString* first = copyString("shared", 6);
  auto* buf = static_cast<char*>(std::malloc(7));
  std::memcpy(buf, "shared", 7);
  // takeString owns buf and must free it on an intern hit; running this under
  // valgrind is what proves it does.
  ObjString* second = takeString(buf, 6);
  CHECK(first == second);
}

TEST(hash_is_stable_for_equal_contents) {
  CHECK_EQ(hashString("abc", 3), hashString("abc", 3));
}

TEST(objects_are_threaded_onto_the_allocation_list) {
  int before = liveObjectCount();
  copyString("a-brand-new-string", 18);
  CHECK(liveObjectCount() > before);
}
