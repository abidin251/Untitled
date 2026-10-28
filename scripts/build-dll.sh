#!/usr/bin/env bash
# Builds MysticCrafting.dll for Windows with LLVM-MinGW, without CMake.
# Set LLVM_MINGW_ROOT to the llvm-mingw installation; scripts/build-cross.sh is
# the CMake route to the same DLL.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${LLVM_MINGW_ROOT:?Set LLVM_MINGW_ROOT to your llvm-mingw installation}"

OUT="${1:-$ROOT/build-win-release}"
CXX="$LLVM_MINGW_ROOT/bin/x86_64-w64-mingw32-clang++"
WINDRES="$LLVM_MINGW_ROOT/bin/x86_64-w64-mingw32-windres"

mkdir -p "$OUT"

# The icon resource the DLL embeds for the Quick Access bar.
sed -e "s|@MC_ICON_PATH@|$ROOT/assets/icon.png|" \
	-e "s|@MC_ICON_HOVER_PATH@|$ROOT/assets/icon_hover.png|" \
	"$ROOT/assets/resources.rc.in" > "$OUT/resources.rc"
"$WINDRES" "$OUT/resources.rc" -O coff -o "$OUT/resources.o"

# Self-contained: the C++ runtime is linked in, so the addon needs nothing but
# the system libraries Nexus already loads.
"$CXX" -std=c++17 -O2 -shared \
	-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS \
	-I"$ROOT/src" -I"$ROOT/deps" -I"$ROOT/deps/imgui" -I"$ROOT/deps/nexus" \
	"$ROOT/src/Addon.cpp" \
	"$ROOT/src/Core.cpp" \
	"$ROOT/src/Http.cpp" \
	"$ROOT/src/Parse.cpp" \
	"$ROOT/src/Planner.cpp" \
	"$ROOT/src/Store.cpp" \
	"$ROOT/src/Ui.cpp" \
	"$ROOT/deps/imgui/imgui.cpp" \
	"$ROOT/deps/imgui/imgui_draw.cpp" \
	"$ROOT/deps/imgui/imgui_tables.cpp" \
	"$ROOT/deps/imgui/imgui_widgets.cpp" \
	"$OUT/resources.o" \
	-o "$OUT/MysticCrafting.dll" \
	-lwinhttp -lcrypt32 -static-libgcc -static-libstdc++

echo "Built: $OUT/MysticCrafting.dll"
