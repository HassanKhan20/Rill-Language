FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ clang cmake ninja-build valgrind python3 git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
CMD ["/bin/bash"]
