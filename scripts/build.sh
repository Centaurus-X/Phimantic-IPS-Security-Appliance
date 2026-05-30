#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPHIMANTEC_ASAN=OFF
cmake --build build -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
ctest --test-dir build --output-on-failure
