#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${VERSION:-1.8.0}"
DLL="${1:-$ROOT/build-win/MysticCrafting.dll}"

if [[ ! -f "$DLL" ]]; then
	echo "Missing $DLL. Build first with scripts/build-cross.sh or copy your release DLL here." >&2
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
