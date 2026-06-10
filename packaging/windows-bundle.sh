#!/usr/bin/env bash
# Bundle the Windows release into a self-contained zip: the .exe plus every
# non-system DLL it needs (SDL3, librtlsdr, libusb, libwinpthread, ...). This is
# what the old hand-made zips were missing, making them unrunnable.
#
# Run inside an MSYS2 MINGW64 shell (locally or in CI).
#   Usage: packaging/windows-bundle.sh [VERSION]
set -euo pipefail

VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo ">> Building release (clean)…"
make clean
make release

NAME="OpenSpectrum-${VERSION}-windows-x86_64"
STAGE="dist/${NAME}"
rm -rf "$STAGE"
mkdir -p "$STAGE"

cp openspectrum.exe "$STAGE/"
[ -f README.md ] && cp README.md "$STAGE/"
[ -f LICENSE ]   && cp LICENSE   "$STAGE/"

echo ">> Collecting MinGW runtime DLLs (ldd is recursive, so this is complete)…"
# Keep only DLLs under the MSYS2 toolchain prefix; the C:\Windows system DLLs
# (KERNEL32, etc.) are present on every Windows box and must NOT be bundled.
ldd openspectrum.exe \
  | awk '/=>/ {print $3}' \
  | grep -iE '/(mingw64|ucrt64|clang64|mingw32)/' \
  | sort -u \
  | while read -r dll; do
      [ -n "$dll" ] && cp -v "$dll" "$STAGE/"
    done

echo ">> Zipping…"
( cd dist && zip -r "${NAME}.zip" "${NAME}" >/dev/null )
echo ">> Built dist/${NAME}.zip"
ls -la "dist/${NAME}.zip"
