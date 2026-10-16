#!/usr/bin/env bash
# Использование: compile_shaders.sh <build_dir> <shader_src> <shader_dst>
set -euo pipefail

BUILD_DIR="${1:-build/debug}"
SRC_DIR="${2:-shaders}"
DST_DIR="${3:-${BUILD_DIR}/bin/shaders}"

# ---------- Поиск shaderc ----------
SHADERC="$(command -v shaderc 2>/dev/null || true)"
if [ -z "$SHADERC" ]; then
    for p in \
        /usr/local/bin/shaderc \
        /usr/bin/shaderc \
        "$BUILD_DIR"/_deps/bgfx-build/cmake/bgfx/shaderc \
        "$BUILD_DIR"/_deps/bgfx-build/tools/shaderc/shaderc \
        "$BUILD_DIR"/_deps/bgfx-build/shaderc; do
        if [ -x "$p" ]; then SHADERC="$p"; break; fi
    done
fi

if [ -z "$SHADERC" ]; then
    echo "[shaders] shaderc не найден"
    exit 1
fi
echo "[shaders] using shaderc: $SHADERC"

# ---------- Поиск include-ов bgfx ----------
BGFX_SRC=""
for p in \
    "$BUILD_DIR"/_deps/bgfx-src/bgfx/src \
    "$BUILD_DIR"/_deps/bgfx-src/src \
    "$BUILD_DIR"/_deps/bgfx.cmake-src/bgfx/src; do
    if [ -f "$p/bgfx_shader.sh" ]; then BGFX_SRC="$p"; break; fi
done

if [ -z "$BGFX_SRC" ]; then
    BGFX_SRC="$(dirname "$(find "$BUILD_DIR/_deps" -name 'bgfx_shader.sh' -print -quit 2>/dev/null)")"
fi

if [ -z "$BGFX_SRC" ] || [ ! -f "$BGFX_SRC/bgfx_shader.sh" ]; then
    echo "[shaders] bgfx_shader.sh не найден"
    find "$BUILD_DIR/_deps" -name 'bgfx_shader.sh' 2>/dev/null
    exit 1
fi
echo "[shaders] bgfx src: $BGFX_SRC"

# ---------- Входные файлы ----------
VARYING_DEF="$SRC_DIR/varying.def.sc"
if [ ! -f "$VARYING_DEF" ]; then
    echo "[shaders] $VARYING_DEF не найден"
    exit 1
fi

mkdir -p "$DST_DIR"

case "$(uname -s)" in
    Linux*)  PLATFORM=linux   ;;
    Darwin*) PLATFORM=osx     ;;
    *)       PLATFORM=windows ;;
esac

# ---------- Компиляция всех *.sc ----------
shopt -s nullglob
shaders_list=("$SRC_DIR"/*.sc)
shopt -u nullglob

if [ ${#shaders_list[@]} -eq 0 ]; then
    echo "[shaders] Нет .sc файлов в $SRC_DIR"
    exit 1
fi

compiled=0
for shader in "${shaders_list[@]}"; do
    name="$(basename "$shader" .sc)"
    if [ "$name" = "varying.def" ]; then
        continue
    fi

    case "$name" in
        vs_*) type=vertex   ;;
        fs_*) type=fragment ;;
        cs_*) type=compute  ;;
        *)
            echo "[shaders] пропускаю $name (не vs_/fs_/cs_)"
            continue
            ;;
    esac

    echo "[shaders] compiling $name ($type)..."
    "$SHADERC" \
        -f "$shader" \
        -o "$DST_DIR/$name.bin" \
        --type "$type" \
        --platform "$PLATFORM" \
        --profile spirv \
        -i "$BGFX_SRC" \
        --varyingdef "$VARYING_DEF"

    compiled=$((compiled + 1))
done

echo "[shaders] $compiled shader(s) → $DST_DIR"
