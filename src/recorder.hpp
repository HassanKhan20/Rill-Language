#pragma once

#include <cstdint>
#include <vector>

#include "table.hpp"
#include "value.hpp"

namespace rill {

// Records enough per instruction to run the VM backwards.
//
// The alternative — a full heap snapshot per step — costs O(heap) per
// instruction. An undo log costs O(mutations), which for a stack machine is
// a small constant: an instruction touches at most a few stack slots plus at
// most one table entry.
//
// Each record stores the top-of-stack values the instruction was about to
// consume, the stack height and frame count before it ran, the ip it ran at,
// and the previous value of whatever deep location it wrote to.
struct UndoRecord {
  uint8_t* ip = nullptr;   // ip of the executing frame, before the instruction
  int stackHeight = 0;
  int frameCount = 0;
  int line = 0;

  // The top `savedCount` stack values as they were before the instruction.
  // Three is enough: no Rill opcode consumes more than three stack operands.
  uint8_t savedCount = 0;
  Value saved[3];

  // A write to a stack slot below the top (SetLocal, SetUpvalue).
  Value* slotTarget = nullptr;
  Value slotOld;

  // A write to a table (SetGlobal, SetProperty, DefineGlobal).
  Table* tableTarget = nullptr;
  ObjString* tableKey = nullptr;
  Value tableOld;
  bool tableHadKey = false;
};

class Recorder {
 public:
  bool enabled() const { return enabled_; }
  void setEnabled(bool on) { enabled_ = on; }

  void clear() { log_.clear(); }
  size_t steps() const { return log_.size(); }

  // Approximate bytes held, for the overhead measurement.
  size_t bytes() const { return log_.capacity() * sizeof(UndoRecord); }

  // Begins a record for the instruction about to execute. `saved` are the top
  // stack values, most recent last.
  UndoRecord& begin(uint8_t* ip, int stackHeight, int frameCount, int line,
                    const Value* top, int topCount) {
    log_.emplace_back();
    UndoRecord& rec = log_.back();
    rec.ip = ip;
    rec.stackHeight = stackHeight;
    rec.frameCount = frameCount;
    rec.line = line;
    rec.savedCount = static_cast<uint8_t>(topCount);
    for (int i = 0; i < topCount; i++) rec.saved[i] = top[i];
    return rec;
  }

  UndoRecord* current() { return log_.empty() ? nullptr : &log_.back(); }

  bool empty() const { return log_.empty(); }

  UndoRecord pop() {
    UndoRecord rec = log_.back();
    log_.pop_back();
    return rec;
  }

  const UndoRecord& back() const { return log_.back(); }

  // The log holds Values that may be unreachable from anywhere else, so it is
  // a GC root for as long as recording is on.
  const std::vector<UndoRecord>& records() const { return log_; }

 private:
  std::vector<UndoRecord> log_;
  bool enabled_ = false;
};

}  // namespace rill
