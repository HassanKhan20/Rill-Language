#pragma once

namespace rill {

// Runs the time-travel debugger REPL over a source file. Returns a process
// exit status.
int runDebugger(const char* path, const char* source);

// Runs a program to completion with recording enabled and reports what the
// recording cost. Used to measure snapshot overhead without a human at a
// prompt.
int runRecorded(const char* source);

}  // namespace rill
