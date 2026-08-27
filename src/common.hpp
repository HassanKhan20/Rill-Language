#pragma once

#include <cstddef>
#include <cstdint>

namespace rill {

// Fixed VM limits. These are deliberately static allocations: the value stack
// and frame array must not move, because open upvalues hold raw pointers into
// the value stack (see ObjUpvalue::location).
constexpr int kFramesMax = 64;
constexpr int kStackMax = kFramesMax * 256;

// A one-byte operand can address 256 distinct slots, which bounds locals per
// function, upvalues per closure, and constants per chunk.
constexpr int kUint8Count = 256;

extern const char* const kVersion;

}  // namespace rill
