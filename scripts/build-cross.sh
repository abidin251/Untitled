#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${LLVM_MINGW_ROOT:?Set LLVM_MINGW_ROOT to your llvm-mingw installation}"

cmake -S "$ROOT" -B "$ROOT/build-win" \
	-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-llvm-mingw.cmake" \
	-DCMAKE_BUILD_TYPE=Release

cmake --build "$ROOT/build-win" -j "${JOBS:-4}"

echo "Built: $ROOT/build-win/MysticCrafting.dll"
