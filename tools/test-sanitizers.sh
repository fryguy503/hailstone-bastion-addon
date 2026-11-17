#!/usr/bin/env bash
set -euo pipefail
task_source="$(cd "$(dirname "$0")/.." && pwd)"
task_sdk="${1:?Pass the patched Hailstone SDK directory}"
cmake -S "$task_source" -B "$task_source/build-linux-asan" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBASTION_SANITIZE=ON -DHAILSTONE_SDK_DIR="$task_sdk"
cmake --build "$task_source/build-linux-asan" -j 8
ctest --test-dir "$task_source/build-linux-asan" --output-on-failure
