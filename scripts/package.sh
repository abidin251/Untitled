#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-1.9.12}"

# The DLL to ship: the path given as the first argument, otherwise the newest
# one any of the build scripts produced. A fixed path here once shipped an
# ancient binary from a build directory that had not been touched in days, so
# the newest build wins and a stale one is refused below.
DLL="${1:-}"
if [[ -z "$DLL" ]]; then
	for candidate in "$ROOT/build-win-release/MysticCrafting.dll" "$ROOT/build-win/MysticCrafting.dll" "$ROOT/build/MysticCrafting.dll"; do
		if [[ -f "$candidate" ]]; then
			if [[ -z "$DLL" || "$candidate" -nt "$DLL" ]]; then
				DLL="$candidate"
			fi
		fi
	done
fi

if [[ ! -f "$DLL" ]]; then
	echo "No MysticCrafting.dll found. Build one with scripts/build-dll.sh or scripts/build-cross.sh." >&2
	exit 1
fi

# A DLL older than the sources cannot contain the current code.
NEWEST_SOURCE="$(find "$ROOT/src" "$ROOT/CMakeLists.txt" -newer "$DLL" -type f -print -quit)"
if [[ -n "$NEWEST_SOURCE" ]]; then
	echo "Refusing to package a stale DLL: $DLL" >&2
	echo "It is older than $NEWEST_SOURCE. Rebuild first." >&2
	exit 1
fi

OUT="$ROOT/dist/MysticCrafting"
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$DLL" "$OUT/MysticCrafting.dll"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/THIRD_PARTY.md" "$OUT/"

rm -f "$ROOT/dist/MysticCrafting-$VERSION.zip"
(
	cd "$ROOT/dist"
	python3 -m zipfile -c "MysticCrafting-$VERSION.zip" MysticCrafting
)

echo "DLL: $OUT/MysticCrafting.dll"
echo "Zip: $ROOT/dist/MysticCrafting-$VERSION.zip"
