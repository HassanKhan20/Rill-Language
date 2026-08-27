#pragma once

#include "chunk.hpp"

namespace rill {

// Prints one instruction and returns the offset of the next one. Instructions
// have variable width, so this is the only correct way to walk a chunk.
int disassembleInstruction(const Chunk& chunk, int offset);

void disassembleChunk(const Chunk& chunk, const char* name);

}  // namespace rill
