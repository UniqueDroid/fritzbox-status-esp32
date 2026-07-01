#pragma once

#include <cstdlib>  // malloc() - normally pulled in transitively via Arduino.h

// Minimal stand-in for the real include/globals.h. The screen-layout files
// (dashboard_main.cpp, dashboard_graph.cpp) only need the display size
// constants from it; everything else in the real globals.h (Arduino, TFT_eSPI,
// WiFiManager) is ESP32-only and not needed to render the same LVGL widgets
// natively for screenshots. This header shadows the real one by coming first
// on the include path.
#define DASHBOARD_WIDTH 320
#define DASHBOARD_HEIGHT 170
