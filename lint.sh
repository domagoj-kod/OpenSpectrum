#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gradual lint: format ONLY the lines changed since a base ref and run
# clang-tidy on that same diff. This is deliberately incremental — a one-time
# full clang-format/clang-tidy pass would reflow the hand-tuned AVX2 blocks
# (fft_analyzer, signal_processor) and the 8x16 font bitmap table
# (text_renderer) for no benefit, so we only ever touch changed lines.
#
# Usage:
#   ./lint.sh [base]            format changed lines (in place) + run clang-tidy
#   ./lint.sh --check [base]    non-mutating; exit 1 if formatting or tidy flags
#                               anything (use in CI / before tagging)
#
#   base defaults to "main". Examples:
#     ./lint.sh                 # vs main, apply formatting
#     ./lint.sh --check         # vs main, report only
#     ./lint.sh HEAD~3          # vs an arbitrary ref
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

CHECK=0
BASE="main"
for arg in "$@"; do
  case "$arg" in
    --check) CHECK=1 ;;
    -h | --help) sed -n '2,20p' "$0"; exit 0 ;;
    *) BASE="$arg" ;;
  esac
done

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
CLANG_TIDY_DIFF="${CLANG_TIDY_DIFF:-clang-tidy-diff}"

# Mirrors the Makefile's INCLUDES (per-module dirs are required — e.g.
# control_state.h does #include "signal_processor.h", which lives in src/signal).
INCLUDES=(
  -Ithird_party -Ithird_party/pocketfft -Ithird_party/stb
  -Iinclude -Isrc -Isrc/hardware -Isrc/signal -Isrc/fft
  -Isrc/visualization -Isrc/utils -Isrc/gui
)
SDL_CFLAGS="$(pkg-config --cflags sdl3 2>/dev/null || true)"

status=0

echo ">> clang-format (changed lines vs ${BASE})"
if [ "$CHECK" -eq 1 ]; then
  fmt_diff="$(git clang-format --binary "$CLANG_FORMAT" --diff "$BASE" 2>/dev/null || true)"
  case "$fmt_diff" in
    "" | "no modified files to format" | "clang-format did not modify any files")
      echo "   formatting clean" ;;
    *)
      echo "$fmt_diff"
      echo "   formatting needed — run ./lint.sh to apply"
      status=1 ;;
  esac
else
  git clang-format --binary "$CLANG_FORMAT" "$BASE" | sed 's/^/   /' || true
fi

echo ">> clang-tidy (changed lines vs ${BASE})"
tidy_out="$(git diff -U0 "$BASE" -- 'src/*.cpp' \
  | "$CLANG_TIDY_DIFF" -p1 -quiet \
      -- "${INCLUDES[@]}" ${SDL_CFLAGS} -std=c++20 2>/dev/null || true)"

if echo "$tidy_out" | grep -qE "warning:|error:"; then
  echo "$tidy_out" | grep -vE "^[0-9]+ (warning|error)s? generated|^Suppressed"
  status=1
else
  echo "   no findings on changed lines"
fi

exit "$status"
