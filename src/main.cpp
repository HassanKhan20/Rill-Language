#include <cstdio>
#include <cstring>

#include "common.hpp"

int main(int argc, char* argv[]) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("rill %s\n", rill::kVersion);
    return 0;
  }
  std::fprintf(stderr, "usage: rill [script.rl]\n");
  return 64;
}
