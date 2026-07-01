#pragma once

// Compile-time board profile selector.
// Select one board profile via build flag in platformio.ini.
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
#include "boards/lilygo_t_display_s3.h"
#elif defined(BOARD_PROFILE_CYD_2432S028)
#include "boards/cyd_2432s028.h"
#else
#error "No board profile selected. Define BOARD_PROFILE_LILYGO_T_DISPLAY_S3 or BOARD_PROFILE_CYD_2432S028 in build_flags."
#endif
