#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${GALESHI_BUILD_DIR:-$ROOT/build/release}"
OUTPUT="${GALESHI_APPIMAGE_OUTPUT:-$ROOT/dist/GaleshiEngine-x86_64.AppImage}"
TOOLS_DIR="${GALESHI_APPIMAGE_TOOLS:-$ROOT/.tools}"
WORK_DIR="${GALESHI_APPIMAGE_WORK_DIR:-$ROOT/build/appimage}"
APPDIR="$WORK_DIR/GaleshiEngine.AppDir"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"

# Keep extraction and appimagetool temporary files on the workspace filesystem;
# /tmp is often a small tmpfs.
export TMPDIR="$WORK_DIR/tmp"
rm -rf "$TMPDIR"
mkdir -p "$TMPDIR"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    cmake --preset release
    cmake --build "$BUILD_DIR" --target GaleshiEditor -j"$(nproc)"
fi

if [[ ! -x "$BUILD_DIR/bin/GaleshiEditor" ]]; then
    echo "GaleshiEditor not found in $BUILD_DIR/bin" >&2
    exit 1
fi

for tool in curl patchelf mksquashfs strip; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required packaging tool: $tool" >&2
        exit 1
    fi
done

mkdir -p "$TOOLS_DIR"
if [[ ! -x "$APPIMAGETOOL" ]]; then
    curl -fL --retry 3 -o "$APPIMAGETOOL" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$APPIMAGETOOL"
fi
if [[ ! -x "$LINUXDEPLOY" ]]; then
    curl -fL --retry 3 -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/512x512/apps"

install -Dm755 "$BUILD_DIR/bin/GaleshiEditor" "$APPDIR/usr/bin/GaleshiEditor"
cp -a "$BUILD_DIR/bin/assets" "$APPDIR/usr/bin/assets"
cp -a "$BUILD_DIR/bin/shaders" "$APPDIR/usr/bin/shaders"

# The editor prefers the relative font path. Make sure it is present even when
# an existing build predates the bundled font.
install -Dm644 "$ROOT/engine/assets/fonts/DejaVuSans.ttf" \
    "$APPDIR/usr/bin/assets/fonts/DejaVuSans.ttf"
install -Dm644 "$ROOT/LICENSE" \
    "$APPDIR/usr/share/doc/galeshi-engine/LICENSE"

install -Dm644 "$ROOT/packaging/galeshi-engine.desktop" \
    "$APPDIR/usr/share/applications/galeshi-engine.desktop"
install -Dm644 "$ROOT/engine/assets/icons/galeshi_logo_round.png" \
    "$APPDIR/usr/share/icons/hicolor/512x512/apps/galeshi-engine.png"
cp "$APPDIR/usr/share/icons/hicolor/512x512/apps/galeshi-engine.png" \
    "$APPDIR/galeshi-engine.png"

# linuxdeploy bundles the non-system shared libraries and fixes their rpaths.
APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/GaleshiEditor" \
    --desktop-file "$APPDIR/usr/share/applications/galeshi-engine.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/galeshi-engine.png"

# Install our launcher after linuxdeploy so it is always the one used by the AppImage.
install -Dm755 "$ROOT/packaging/AppRun" "$APPDIR/AppRun"

mkdir -p "$(dirname -- "$OUTPUT")"
rm -f "$OUTPUT"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" \
    --no-appstream "$APPDIR" "$OUTPUT"

chmod +x "$OUTPUT"
echo "Created $OUTPUT"
