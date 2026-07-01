# UI simulator

Renders the device's three dashboard screens natively on a PC with SDL2 instead of TFT_eSPI, so they can be captured as PNGs for the main [README](../../README.md) without needing physical hardware.

It reuses the real screen-layout code unmodified (`src/dashboard_main.cpp`, `src/dashboard_graph.cpp`, `src/dashboard_metrics.cpp`); only the display driver (SDL instead of TFT_eSPI) and the input data (fixed mock values instead of live Wi-Fi/FRITZ!Box data) are replaced. This is not part of the firmware build.

## Regenerating the screenshots

```sh
pio run -e lilygo-t-display-s3   # once, to vendor lvgl into .pio/libdeps/
./tools/ui_simulator/generate_screenshots.sh
```

This writes `dashboard.png`, `graph.png` and `metrics.png` to `docs/screenshots/`.

Requires SDL2 dev headers, `gcc`/`g++`, and ImageMagick's `convert` (only used to upscale the native 320x170 render 2x for a crisper image in the README).
