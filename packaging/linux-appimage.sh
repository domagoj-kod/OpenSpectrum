#!/usr/bin/env bash
# Build a self-contained Linux AppImage: a single executable file that bundles
# SDL2/librtlsdr/libusb so it runs on any modern x86-64 distro with no install
# (the user just needs RTL-SDR udev permissions, same as any SDR tool).
#
# Run on Linux (locally or in CI). Requires: build deps, curl, imagemagick.
#   Usage: packaging/linux-appimage.sh [VERSION]
set -euo pipefail

VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo ">> Building release (clean)…"
make clean
make release

APPDIR="$ROOT/dist/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp openspectrum "$APPDIR/usr/bin/"

# Desktop entry (required by linuxdeploy). Terminal=true: the app logs to stdout.
DESKTOP="$APPDIR/usr/share/applications/openspectrum.desktop"
cat > "$DESKTOP" <<'EOF'
[Desktop Entry]
Type=Application
Name=OpenSpectrum
Comment=Real-time RTL-SDR spectrum analyzer and waterfall
Exec=openspectrum
Icon=openspectrum
Categories=HamRadio;Utility;
Terminal=true
EOF

# Icon (generated so we don't commit a binary). 256x256 keeps appimagetool happy.
ICON="$APPDIR/usr/share/icons/hicolor/256x256/apps/openspectrum.png"
if command -v convert >/dev/null 2>&1; then
  convert -size 256x256 gradient:'#0b1f3a'-'#000000' \
          -gravity center -pointsize 96 -fill '#39c5ff' -annotate 0 'OS' "$ICON"
else
  echo "!! ImageMagick 'convert' not found; writing a 1x1 placeholder icon" >&2
  base64 -d > "$ICON" <<'B64'
iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M8AAAMBAQDJ/pLvAAAAAElFTkSuQmCC
B64
fi

echo ">> Fetching linuxdeploy…"
TOOLDIR="$ROOT/dist/tools"
mkdir -p "$TOOLDIR"
LD="$TOOLDIR/linuxdeploy-x86_64.AppImage"
if [ ! -x "$LD" ]; then
  curl -fsSL -o "$LD" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  chmod +x "$LD"
fi

# GitHub runners usually lack FUSE; run the AppImage tools by self-extracting.
export APPIMAGE_EXTRACT_AND_RUN=1
export OUTPUT="OpenSpectrum-${VERSION}-x86_64.AppImage"

echo ">> Bundling dependencies + building AppImage…"
"$LD" --appdir "$APPDIR" \
      --executable "$APPDIR/usr/bin/openspectrum" \
      --desktop-file "$DESKTOP" \
      --icon-file "$ICON" \
      --output appimage

mkdir -p "$ROOT/dist"
mv "$OUTPUT" "$ROOT/dist/"
echo ">> Built dist/${OUTPUT}"
ls -la "$ROOT/dist/${OUTPUT}"
