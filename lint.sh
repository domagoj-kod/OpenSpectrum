#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Format and clang-tidy the whole tree: src/ (third_party/ is vendored and
# left alone). The font table in text_renderer.cpp is fenced with
# clang-format off; everything else follows .clang-format.
#
# Usage:
#   ./lint.sh            format in place, then run clang-tidy
#   ./lint.sh --check    change nothing; exit 1 if formatting or tidy flags
#                        anything (run it before tagging)
#   UHD=1 ./lint.sh      also tidy uhd_device.cpp (needs the UHD headers),
#                        as `make UHD=1` also compiles it
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

CHECK=0
case "${1:-}" in
  --check) CHECK=1 ;;
  -h | --help) sed -n '2,13p' "$0"; exit 0 ;;
  "") ;;
  *) echo "lint.sh: unknown argument: $1" >&2; exit 2 ;;
esac

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
# A missing tool must fail loudly: a silent skip reads as "clean".
for tool in "$CLANG_FORMAT" "$CLANG_TIDY"; do
  command -v "$tool" >/dev/null || { echo "lint.sh: $tool not found" >&2; exit 2; }
done

# Mirrors the Makefile's INCLUDES (per-module dirs are required — e.g.
# control_state.h does #include "signal_processor.h", which lives in src/signal).
FLAGS=(
  -std=c++20
  -isystem third_party -isystem third_party/pocketfft -isystem third_party/stb
  -Isrc -Isrc/hardware -Isrc/signal -Isrc/fft -Isrc/visualization
  -Isrc/utils -Isrc/gui
)
read -ra SDL_FLAGS <<<"$(pkg-config --cflags sdl3 2>/dev/null || true)"
FLAGS+=("${SDL_FLAGS[@]}")

mapfile -t FILES < <(git ls-files 'src/*.cpp' 'src/*.h')
mapfile -t UNITS < <(git ls-files 'src/*.cpp')
if [ "${UHD:-}" = 1 ]; then
  FLAGS+=(-DOPENSPECTRUM_UHD)
else
  mapfile -t UNITS < <(printf '%s\n' "${UNITS[@]}" | grep -v 'uhd_device\.cpp$')
fi

status=0

echo ">> clang-format (${#FILES[@]} files)"
if [ "$CHECK" -eq 1 ]; then
  "$CLANG_FORMAT" --dry-run --Werror "${FILES[@]}" || status=1
else
  "$CLANG_FORMAT" -i "${FILES[@]}"
fi

echo ">> clang-tidy (${#UNITS[@]} translation units)"
# One process per translation unit, in parallel; xargs fails if any does.
tidy_out="$(printf '%s\0' "${UNITS[@]}" |
  xargs -0 -P "$(nproc)" -I{} "$CLANG_TIDY" -quiet --warnings-as-errors='*' {} \
    -- "${FLAGS[@]}" 2>&1)" || status=1
grep -vE '^[0-9]+ (warning|error)s? (generated|treated)|^Suppressed' \
  <<<"$tidy_out" || true

[ "$status" -eq 0 ] && echo "   clean"
exit "$status"
