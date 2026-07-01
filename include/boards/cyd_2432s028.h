#pragma once

// Hardware/profile defines for CYD 2.8" (2432S028).
// App layout
#define DASHBOARD_WIDTH 320
#define DASHBOARD_HEIGHT 480
#define DASHBOARD_ROTATION 0

// TFT_eSPI profile from Bruce firmware (CYD-2432S028)
#define USER_SETUP_LOADED 1
#define ST7796_DRIVER 1
#define TFT_WIDTH 320
#define TFT_HEIGHT 480
#define TFT_INVERSION_ON 1

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON 1

// Fonts
#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define LOAD_FONT4 1
#define LOAD_FONT6 1
#define LOAD_FONT7 1
#define LOAD_FONT8 1
#define LOAD_GFXFF 1
#define SMOOTH_FONT 1
