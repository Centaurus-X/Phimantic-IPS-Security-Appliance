#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DPHIMANTEC_ASAN=ON
cmake --build build-asan -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
ctest --test-dir build-asan --output-on-failure
