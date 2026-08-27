#include "chunk.hpp"
#include "test_harness.hpp"
#include "value.hpp"

using namespace rill;

TEST(value_constructors_and_predicates) {
  CHECK(isNil(nilValue()));
  CHECK(isBool(boolValue(true)));
  CHECK(asBool(boolValue(true)));
  CHECK(!asBool(boolValue(false)));
  CHECK(isNumber(numberValue(1.5)));
  CHECK_EQ(asNumber(numberValue(1.5)), 1.5);
}

TEST(value_predicates_are_mutually_exclusive) {
  Value n = numberValue(1);
  CHECK(!isNil(n));
  CHECK(!isBool(n));
  CHECK(!isObj(n));
}

TEST(value_falsiness_covers_only_nil_and_false) {
  CHECK(isFalsey(nilValue()));
  CHECK(isFalsey(boolValue(false)));
  CHECK(!isFalsey(boolValue(true)));
  CHECK(!isFalsey(numberValue(0)));
  CHECK(!isFalsey(numberValue(-1)));
}

TEST(value_equality_by_type_and_payload) {
  CHECK(valuesEqual(numberValue(3), numberValue(3)));
  CHECK(!valuesEqual(numberValue(3), numberValue(4)));
  CHECK(!valuesEqual(numberValue(3), boolValue(true)));
  CHECK(valuesEqual(nilValue(), nilValue()));
  CHECK(valuesEqual(boolValue(false), boolValue(false)));
  CHECK(!valuesEqual(boolValue(false), nilValue()));
}

TEST(chunk_write_records_bytes) {
  Chunk c;
  c.write(static_cast<uint8_t>(OpCode::Nil), 1);
  c.write(static_cast<uint8_t>(OpCode::Return), 1);
  CHECK_EQ(c.code.size(), static_cast<size_t>(2));
  CHECK_EQ(c.code[0], static_cast<uint8_t>(OpCode::Nil));
}

TEST(chunk_add_constant_returns_index) {
  Chunk c;
  int a = c.addConstant(numberValue(42));
  int b = c.addConstant(numberValue(7));
  CHECK_EQ(a, 0);
  CHECK_EQ(b, 1);
  CHECK_EQ(asNumber(c.constants[static_cast<size_t>(a)]), 42.0);
  CHECK_EQ(asNumber(c.constants[static_cast<size_t>(b)]), 7.0);
}

TEST(chunk_run_length_encoded_lines) {
  Chunk c;
  c.write(1, 10);
  c.write(2, 10);
  c.write(3, 12);
  c.write(4, 12);
  c.write(5, 20);
  CHECK_EQ(c.lineAt(0), 10);
  CHECK_EQ(c.lineAt(1), 10);
  CHECK_EQ(c.lineAt(2), 12);
  CHECK_EQ(c.lineAt(3), 12);
  CHECK_EQ(c.lineAt(4), 20);
}

TEST(chunk_line_encoding_is_compact) {
  // Twenty bytes on one line must not cost twenty line entries; that
  // compactness is the whole point of the run-length encoding.
  Chunk c;
  for (int i = 0; i < 20; i++) c.write(0, 7);
  CHECK_EQ(c.lineRunCount(), 1);
  CHECK_EQ(c.lineAt(19), 7);
}
