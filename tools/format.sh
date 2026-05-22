#!/usr/bin/env bash
# Run clang-format over all C sources and headers in-place.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

find "$ROOT/src" "$ROOT/include" "$ROOT/tests" "$ROOT/bench" \
    \( -name '*.c' -o -name '*.h' \) \
    ! -path '*/vendor/*' \
    ! -path '*/build*' \
    -print0 \
  | xargs -0 clang-format --style=file --fallback-style=LLVM -i

echo "clang-format done."
