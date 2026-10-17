#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-build/debug}"
SRC_DIR="${2:-shaders}"
DST_DIR="${3:-${BUILD_DIR}/bin/shaders}"

# ---------- shaderc ----------
SHADERC="$(command -v shaderc 2>/dev/null || true)"
if [ -z "$SHADERC" ]; then
    for p in \
        /usr/local/bin/shaderc \
        /usr/bin/shaderc; do
        if [ -x "$p" ]; then SHADERC="$p"; break; fi
    done
fi
[ -z "$SHADERC" ] && { echo "[shaders] shaderc not found"; exit 1; }
echo "[shaders] using shaderc: $SHADERC"

# ---------- bgfx include ----------
BGFX_SRC=""
for p in \
    "$BUILD_DIR"/_deps/bgfx-src/bgfx/src \
    "$BUILD_DIR"/_deps/bgfx-src/src \
    "$BUILD_DIR"/_deps/bgfx.cmake-src/bgfx/src; do
    if [ -f "$p/bgfx_shader.sh" ]; then BGFX_SRC="$p"; break; fi
done
[ -z "$BGFX_SRC" ] && { echo "[shaders] bgfx_shader.sh not found"; exit 1; }
echo "[shaders] bgfx src: $BGFX_SRC"

mkdir -p "$DST_DIR"

case "$(uname -s)" in
    Linux*)  PLATFORM=linux   ;;
    Darwin*) PLATFORM=osx     ;;
    *)       PLATFORM=windows ;;
esac

# Какой .def.sc использовать для конкретного шейдера
varying_for() {
    case "$1" in
        vs_pbr|fs_pbr)     echo "$SRC_DIR/varying_pbr.def.sc"   ;;
        vs_line|fs_line)   echo "$SRC_DIR/varying_line.def.sc"  ;;
        vs_imgui|fs_imgui) echo "$SRC_DIR/varying_imgui.def.sc" ;;
        *)                 echo "" ;;
    esac
}

compiled=0
for shader in "$SRC_DIR"/vs_*.sc "$SRC_DIR"/fs_*.sc "$SRC_DIR"/cs_*.sc; do
    [ -f "$shader" ] || continue
    name="$(basename "$shader" .sc)"

    case "$name" in
        vs_*) type=vertex   ;;
        fs_*) type=fragment ;;
        cs_*) type=compute  ;;
    esac

    VDEF="$(varying_for "$name")"
    if [ -z "$VDEF" ] || [ ! -f "$VDEF" ]; then
        echo "[shaders] SKIP $name (нет def-файла)"
        continue
    fi

    echo "[shaders] $name ($type) using $(basename "$VDEF")"

    "$SHADERC" \
        -f "$shader" \
        -o "$DST_DIR/$name.bin" \
        --type "$type" \
        --platform "$PLATFORM" \
        --profile spirv \
        -i "$BGFX_SRC" \
        --varyingdef "$VDEF"
    compiled=$((compiled + 1))
done

echo "[shaders] $compiled shader(s) -> $DST_DIR"
