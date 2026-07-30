#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Compatibility wrapper: build via CMake when available, else direct clang++.
if command -v cmake >/dev/null 2>&1; then
  cmake -S . -B build -DBUILD_GUI=ON -DBUILD_TESTS=ON
  cmake --build build -j
  if [[ "${1:-}" == "--run" ]] || [[ "${1:-}" == "" ]]; then
    if [[ -x build/diffusion_sim ]]; then
      ./build/diffusion_sim
    else
      echo "GUI binary not built (raylib missing?). Headless: ./build/headless_runner --help"
    fi
  fi
else
  clang++ -std=c++17 main.cpp Simulator1D.cpp Simulator2D.cpp Simulator2DRenderer.cpp DataParser.cpp \
    -o diffusion_sim \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lraylib \
    -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
  clang++ -std=c++17 headless_runner.cpp Simulator2D.cpp DataParser.cpp -o headless_runner -pthread
  ./diffusion_sim
fi
