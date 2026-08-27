#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common.hpp"

namespace {

// Reads a whole file into a string. Returns false and reports on failure.
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
  size_t read = std::fread(&(*out)[0], sizeof(char), static_cast<size_t>(size),
                           file);
  std::fclose(file);
  if (read < static_cast<size_t>(size)) {
    std::fprintf(stderr, "could not read file \"%s\"\n", path);
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("rill %s\n", rill::kVersion);
    return 0;
  }

  if (argc == 2) {
    std::string source;
    if (!readFile(argv[1], &source)) return 74;
    // The interpreter is wired up in a later task; for now a readable file is
    // a successful run.
    return 0;
  }

  std::fprintf(stderr, "usage: rill [script.rl]\n");
  return 64;
}
