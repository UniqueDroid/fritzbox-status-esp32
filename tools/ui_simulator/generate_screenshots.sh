#!/usr/bin/env bash
# Renders the three device screens (dashboard/graph/metrics) natively on the
# host with SDL2 instead of TFT_eSPI, and saves them as PNGs under
# docs/screenshots/ for the README. Reuses the real layout code from
# src/dashboard_main.cpp, src/dashboard_graph.cpp and src/dashboard_metrics.cpp
# unmodified - only the display driver and input data are mocked.
#
# Requires: a prior `pio run -e lilygo-t-display-s3` (to vendor lvgl into
# .pio/libdeps/), SDL2 dev headers, g++, and ImageMagick's `convert`.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SIM_DIR="$REPO_ROOT/tools/ui_simulator"
LVGL_DIR="$REPO_ROOT/.pio/libdeps/lilygo-t-display-s3/lvgl"
OUT_DIR="$REPO_ROOT/docs/screenshots"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

if [ ! -d "$LVGL_DIR" ]; then
  echo "error: $LVGL_DIR not found - run 'pio run -e lilygo-t-display-s3' at least once first" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

echo "Compiling simulator..."
LVGL_SOURCES=$(find "$LVGL_DIR/src" -name '*.c')

COMMON_INCLUDES=(
  -I "$SIM_DIR/stub_include"
  -I "$REPO_ROOT/include"
  -I "$LVGL_DIR"
  -D LV_CONF_INCLUDE_SIMPLE=1
  -D "LV_CONF_PATH=$REPO_ROOT/include/lv_conf.h"
)

# lvgl's C sources rely on C's implicit void*-to-T* conversions, which a C++
# compiler rejects - compile them with gcc as C, and the app/glue code
# (which does need C++ for SDL2's C++-friendly headers) with g++, then link.
OBJ_DIR="$BUILD_DIR/obj"
mkdir -p "$OBJ_DIR"

LVGL_OBJS=()
i=0
for src in $LVGL_SOURCES; do
  obj="$OBJ_DIR/lvgl_$i.o"
  gcc -std=gnu11 -O2 -fpermissive "${COMMON_INCLUDES[@]}" -c "$src" -o "$obj"
  LVGL_OBJS+=("$obj")
  i=$((i + 1))
done

g++ -std=c++17 -O2 "${COMMON_INCLUDES[@]}" $(pkg-config --cflags sdl2) \
  -c "$SIM_DIR/main.cpp" -o "$OBJ_DIR/main.o"

# PlatformIO's Arduino framework force-includes Arduino.h (which pulls in
# <cstdlib> transitively) for every .cpp file, so these files rely on malloc()
# being visible without including it themselves. Replicate that with -include.
g++ -std=c++17 -O2 -include cstdlib "${COMMON_INCLUDES[@]}" \
  -c "$REPO_ROOT/src/dashboard_main.cpp" -o "$OBJ_DIR/dashboard_main.o"
g++ -std=c++17 -O2 -include cstdlib "${COMMON_INCLUDES[@]}" \
  -c "$REPO_ROOT/src/dashboard_graph.cpp" -o "$OBJ_DIR/dashboard_graph.o"
g++ -std=c++17 -O2 -include cstdlib "${COMMON_INCLUDES[@]}" \
  -c "$REPO_ROOT/src/dashboard_metrics.cpp" -o "$OBJ_DIR/dashboard_metrics.o"

g++ "$OBJ_DIR/main.o" "$OBJ_DIR/dashboard_main.o" "$OBJ_DIR/dashboard_graph.o" \
  "$OBJ_DIR/dashboard_metrics.o" "${LVGL_OBJS[@]}" \
  -o "$BUILD_DIR/ui_simulator" \
  $(pkg-config --libs sdl2) -lm

echo "Rendering screens..."
"$BUILD_DIR/ui_simulator" "$BUILD_DIR"

for name in dashboard graph metrics; do
  convert "$BUILD_DIR/$name.bmp" -filter point -resize 200% "$OUT_DIR/$name.png"
  echo "wrote $OUT_DIR/$name.png"
done
