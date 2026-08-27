#!/usr/bin/env bash
set -euo pipefail

CMD="${1:-build}"
shift || true

case "$CMD" in
  build)
    cmake -S . -B build -G Ninja "$@"
    cmake --build build
    ;;
  test)
    ctest --test-dir build --output-on-failure "$@"
    ;;
  all)
    cmake -S . -B build -G Ninja
    cmake --build build
    ctest --test-dir build --output-on-failure
    ;;
  shell)
    exec /bin/bash
    ;;
  *)
    echo "usage: dev.sh {build|test|all|shell}" >&2
    exit 64
    ;;
esac
