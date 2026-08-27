#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

namespace {

bool readFile(const char* path, std::string* out) {
  std::FILE* file = std::fopen(path, "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "could not open file \"%s\"\n", path);
    return false;
  }
  std::fseek(file, 0L, SEEK_END);
  long size = std::ftell(file);
  std::rewind(file);

  out->resize(static_cast<size_t>(size));
  size_t read = std::fread(&(*out)[0], sizeof(char),
                           static_cast<size_t>(size), file);
  std::fclose(file);
  if (read < static_cast<size_t>(size)) {
    std::fprintf(stderr, "could not read file \"%s\"\n", path);
    return false;
  }
  return true;
}

// Compiles without running and prints the bytecode. The quickest way to see
// what an optimization actually did.
int dumpFile(const char* path) {
  std::string source;
  if (!readFile(path, &source)) return 74;
  rill::ObjFunction* function = rill::compile(source.c_str());
  if (function == nullptr) return 65;
  rill::disassembleChunk(function->chunk, "script");
  return 0;
}

int runFile(const char* path) {
  std::string source;
  if (!readFile(path, &source)) return 74;

  rill::InterpretResult result = rill::vm.interpret(source.c_str());
  switch (result) {
    case rill::InterpretResult::Ok:           return 0;
    case rill::InterpretResult::CompileError: return 65;
    case rill::InterpretResult::RuntimeError: return 70;
  }
  return 70;
}

void repl() {
  char line[1024];
  for (;;) {
    std::printf("> ");
    if (!std::fgets(line, sizeof(line), stdin)) {
      std::printf("\n");
      break;
    }
    rill::vm.interpret(line);
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("rill %s\n", rill::kVersion);
    return 0;
  }

  rill::vm.init();

  int status = 0;
  if (argc == 3 && std::strcmp(argv[1], "--dump") == 0) {
    status = dumpFile(argv[2]);
  } else if (argc == 1) {
    repl();
  } else if (argc == 2) {
    status = runFile(argv[1]);
  } else {
    std::fprintf(stderr, "usage: rill [script.rl]\n");
    status = 64;
  }

  rill::vm.free();
  return status;
}
