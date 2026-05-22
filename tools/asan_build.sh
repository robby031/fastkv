#!/usr/bin/env bash
# Quick AddressSanitizer build + test run.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-asan"

cmake -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${CC:-clang}" \
    -DFASTKV_ENABLE_ASAN=ON \
    -DFASTKV_BUILD_TESTS=ON

cmake --build "$BUILD" --parallel

cd "$BUILD"
ASAN_OPTIONS=detect_leaks=1 ctest --output-on-failure
