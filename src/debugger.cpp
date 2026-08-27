#include "debugger.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "object.hpp"
#include "value.hpp"
#include "vm.hpp"

namespace rill {

namespace {

void printHelp() {
  std::printf(
      "commands:\n"
      "  step [n]     execute n instructions forward (default 1)\n"
      "  back [n]     undo n instructions\n"
      "  line         run forward until the source line changes\n"
      "  rewind       return to the start of the recording\n"
      "  where        print the call stack\n"
      "  print <name> show the value of a global\n"
      "  stats        recorded steps and log size\n"
      "  help         this list\n"
      "  quit         leave the debugger\n");
}

void printPosition(VM& vm) {
  int line = vm.currentLine();
  if (line < 0) {
    std::printf("[end of program] %zu steps recorded\n", vm.recorder().steps());
  } else {
    std::printf("[line %d] step %zu\n", line, vm.recorder().steps());
  }
}

std::vector<std::string> splitWords(const char* line) {
  std::vector<std::string> words;
  std::string word;
  for (const char* c = line; *c != '\0'; c++) {
    if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
      if (!word.empty()) {
        words.push_back(word);
        word.clear();
      }
    } else {
      word += *c;
    }
  }
  if (!word.empty()) words.push_back(word);
  return words;
}

int parseCount(const std::vector<std::string>& words) {
  if (words.size() < 2) return 1;
  int n = std::atoi(words[1].c_str());
  return n < 1 ? 1 : n;
}

}  // namespace

int runRecorded(const char* source) {
  if (vm.recordProgram(source) != InterpretResult::Ok) return 65;

  while (vm.currentLine() >= 0) {
    if (vm.stepForward() != InterpretResult::Ok) return 70;
  }

  std::fprintf(stderr, "steps=%zu log_bytes=%zu bytes_per_step=%zu\n",
               vm.recorder().steps(), vm.recorder().bytes(),
               sizeof(UndoRecord));
  return 0;
}

int runDebugger(const char* path, const char* source) {
  std::printf("rill time-travel debugger — %s\n", path);
  std::printf("Recording is on; every instruction is reversible. "
              "Type 'help' for commands.\n\n");

  if (vm.recordProgram(source) != InterpretResult::Ok) return 65;
  printPosition(vm);

  char line[1024];
  for (;;) {
    std::printf("(rill-dbg) ");
    std::fflush(stdout);
    if (!std::fgets(line, sizeof(line), stdin)) {
      std::printf("\n");
      break;
    }

    std::vector<std::string> words = splitWords(line);
    if (words.empty()) continue;
    const std::string& cmd = words[0];

    if (cmd == "quit" || cmd == "q") {
      break;
    } else if (cmd == "help" || cmd == "h") {
      printHelp();
    } else if (cmd == "step" || cmd == "s") {
      int n = parseCount(words);
      for (int i = 0; i < n; i++) {
        if (vm.currentLine() < 0) {
          std::printf("already at the end of the program\n");
          break;
        }
        if (vm.stepForward() != InterpretResult::Ok) {
          std::printf("runtime error; the recording stops here\n");
          break;
        }
      }
      printPosition(vm);
    } else if (cmd == "back" || cmd == "b") {
      int n = parseCount(words);
      int undone = 0;
      for (int i = 0; i < n; i++) {
        if (!vm.stepBack()) break;
        undone++;
      }
      if (undone < n) std::printf("reached the start of the recording\n");
      printPosition(vm);
    } else if (cmd == "line") {
      // Step until the reported source line changes, which is a more useful
      // unit than a single bytecode instruction.
      int start = vm.currentLine();
      while (vm.currentLine() == start && vm.currentLine() >= 0) {
        if (vm.stepForward() != InterpretResult::Ok) break;
      }
      printPosition(vm);
    } else if (cmd == "rewind") {
      while (vm.stepBack()) {
      }
      printPosition(vm);
    } else if (cmd == "where" || cmd == "w") {
      vm.printBacktrace();
    } else if (cmd == "print" || cmd == "p") {
      if (words.size() < 2) {
        std::printf("usage: print <global-name>\n");
        continue;
      }
      Value value;
      if (vm.lookupGlobal(words[1].c_str(), &value)) {
        std::printf("%s = ", words[1].c_str());
        printValue(value);
        std::printf("\n");
      } else {
        std::printf("no global named '%s' at this point\n", words[1].c_str());
      }
    } else if (cmd == "stats") {
      std::printf("steps recorded: %zu\nundo log:       %zu bytes (%zu per step)\n",
                  vm.recorder().steps(), vm.recorder().bytes(),
                  sizeof(UndoRecord));
    } else {
      std::printf("unknown command '%s'; type 'help'\n", cmd.c_str());
    }
  }

  return 0;
}

}  // namespace rill
