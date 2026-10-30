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
        vs_fullscreen|fs_passthrough|fs_invert|fs_fxaa|fs_lighting|fs_sky|fs_shadow|fs_ibl_gen|fs_irradiance_conv|fs_prefilter|fs_brdf|fs_tonemap|fs_bloom_extract|fs_bloom_blur|fs_bloom_combine|fs_ssr|fs_ssao|fs_ssao_blur|fs_ao_sample|fs_ao_upsample|fs_ao_detail|fs_ao_blur|fs_hbao|fs_gtao|fs_taa|fs_smaa_edge|fs_godrays|fs_atmosphere|fs_smaa_weight|fs_smaa_blend|fs_hbao_blur|fs_vignette|fs_color_correction|fs_chromatic|fs_dof_coc|fs_dof_blur|fs_dof_composite|fs_motion_blur|fs_motion_blur_velocity) echo "$SRC_DIR/varying_fullscreen.def.sc" ;;
        vs_water|fs_water)                                   echo "$SRC_DIR/varying_water.def.sc" ;;
        vs_hair|fs_hair)                                     echo "$SRC_DIR/varying_hair.def.sc" ;;
        vs_pbr|fs_pbr|fs_gbuffer|fs_glass|fs_shadow_point)     echo "$SRC_DIR/varying_pbr.def.sc"   ;;
        vs_particles|fs_particles)             echo "$SRC_DIR/varying_particles.def.sc" ;;
        vs_mesh_instanced)                     echo "$SRC_DIR/varying_mesh_instanced.def.sc" ;;
        vs_mesh_shadow_instanced)              echo "$SRC_DIR/varying_mesh_shadow_instanced.def.sc" ;;
        vs_line|fs_line)                       echo "$SRC_DIR/varying_line.def.sc"  ;;
        vs_shadow|fs_shadow)                   echo "$SRC_DIR/varying_shadow.def.sc";;
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

    if [ "$type" == "compute" ]; then
        echo "[shaders] $name (compute)"
        "$SHADERC" -f "$shader" -o "$DST_DIR/$name.bin" --type compute --platform "$PLATFORM" --profile spirv -i "$BGFX_SRC"
        compiled=$((compiled + 1))
        continue
    fi


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
