#pragma once

// Hardware/profile defines for CYD 2.8" (2432S028).
// Previous revisions of this profile guessed ST7796 at 320x480, which was
// wrong for this exact board and caused garbled/incomplete rendering. The
// authoritative pin/driver config below is taken from the Bruce firmware's
// [env:CYD-2432S028] (extends CYD_base), which targets this same hardware:
// https://github.com/BruceDevices/firmware/blob/main/boards/CYD-2432S028/CYD-2432S028.ini
// That board is a 240x320 ILI9341-family panel, not 320x480.

// App layout
// Rotations 1 (MV), 5 (MV+MX) and 7 (MV+MY) all came out mirrored on clean
// builds. Rotation 3 (MV+MX+MY) is the last untested option in that family.
#define DASHBOARD_WIDTH 320
#define DASHBOARD_HEIGHT 240
#define DASHBOARD_ROTATION 3

// TFT_eSPI profile (from Bruce firmware's CYD-2432S028 board definition)
#define USER_SETUP_LOADED 1
#define ILI9341_2_DRIVER 1
#define USE_HSPI_PORT 1
#define TFT_WIDTH 240
#define TFT_HEIGHT 320
// Black showing as white is the classic sign this panel needs the inversion
// command sent (Bruce's CYD-2432S028 base doesn't set it, but this unit
// apparently needs it - same situation as their CYD-2USB variant).
#define TFT_INVERSION_ON 1

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21
#define TFT_BACKLIGHT_ON 1

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000

// Fonts
#define LOAD_GLCD 1
#define LOAD_FONT2 1
#define LOAD_FONT4 1
#define LOAD_FONT6 1
#define LOAD_FONT7 1
#define LOAD_FONT8 1
#define LOAD_GFXFF 1
#define SMOOTH_FONT 1
