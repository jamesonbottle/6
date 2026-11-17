#!/usr/bin/env bash
# Formats the C++ sources with clang-format (config: .clang-format).
set -eu
cd "$(dirname "$0")/.."
CF=${CLANG_FORMAT:-clang-format}
files=$(ls src/*.h src/*.cpp tools/*.cpp)
if [ "${1:-}" = "--check" ]; then
  # shellcheck disable=SC2086
  $CF --dry-run --Werror $files
else
  # shellcheck disable=SC2086
  $CF -i $files
fi
