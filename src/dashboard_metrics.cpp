// Metrics page layout with enlarged CPU/RAM/TEMP and latency/loss values.
#include "lvgl_screens.h"

void createScreenMetrics(lv_obj_t *page, LvglScreenRefs &refs) {
  lvglCreateValueLabel(page, "CPU", 10, 8, lv_color_hex(0x8E9BAC));
  refs.bigCpuBar = lv_bar_create(page);
  lv_obj_set_size(refs.bigCpuBar, 220, 14);
  lv_obj_align(refs.bigCpuBar, LV_ALIGN_TOP_LEFT, 58, 10);
  lv_bar_set_range(refs.bigCpuBar, 0, 100);
  lv_obj_set_style_bg_color(refs.bigCpuBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(refs.bigCpuBar, lv_color_hex(0x2AC7D8), LV_PART_INDICATOR);
  refs.bigCpuVal = lvglCreateValueLabel(page, "0%", 286, 6, lv_color_hex(0xDDE7F2));
  lv_obj_set_style_text_font(refs.bigCpuVal, &lv_font_montserrat_14, 0);

  lvglCreateValueLabel(page, "RAM", 10, 36, lv_color_hex(0x8E9BAC));
  refs.bigRamBar = lv_bar_create(page);
  lv_obj_set_size(refs.bigRamBar, 220, 14);
  lv_obj_align(refs.bigRamBar, LV_ALIGN_TOP_LEFT, 58, 38);
  lv_bar_set_range(refs.bigRamBar, 0, 100);
  lv_obj_set_style_bg_color(refs.bigRamBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(refs.bigRamBar, lv_color_hex(0xF5B942), LV_PART_INDICATOR);
  refs.bigRamVal = lvglCreateValueLabel(page, "0%", 286, 34, lv_color_hex(0xDDE7F2));
  lv_obj_set_style_text_font(refs.bigRamVal, &lv_font_montserrat_14, 0);

  lvglCreateValueLabel(page, "TEMP", 10, 64, lv_color_hex(0x8E9BAC));
  refs.bigTempBar = lv_bar_create(page);
  lv_obj_set_size(refs.bigTempBar, 220, 14);
  lv_obj_align(refs.bigTempBar, LV_ALIGN_TOP_LEFT, 58, 66);
  lv_bar_set_range(refs.bigTempBar, 0, 100);
  lv_obj_set_style_bg_color(refs.bigTempBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(refs.bigTempBar, lv_color_hex(0xE66565), LV_PART_INDICATOR);
  refs.bigTempVal = lvglCreateValueLabel(page, "-", 286, 62, lv_color_hex(0xDDE7F2));
  lv_obj_set_style_text_font(refs.bigTempVal, &lv_font_montserrat_14, 0);

  lvglCreateValueLabel(page, "RTT", 10, 108, lv_color_hex(0x8E9BAC));
  refs.bigRttVal = lvglCreateValueLabel(page, "-", 40, 108, lv_color_hex(0xF5B942));
  lv_obj_set_style_text_font(refs.bigRttVal, &lv_font_montserrat_14, 0);

  lvglCreateValueLabel(page, "RTTsd", 86, 108, lv_color_hex(0x8E9BAC));
  refs.bigRttSdVal = lvglCreateValueLabel(page, "-", 138, 108, lv_color_hex(0xF5B942));
  lv_obj_set_style_text_font(refs.bigRttSdVal, &lv_font_montserrat_14, 0);

  lvglCreateValueLabel(page, "Loss", 190, 108, lv_color_hex(0x8E9BAC));
  refs.bigLossVal = lvglCreateValueLabel(page, "-", 228, 108, lv_color_hex(0xF5B942));
  lv_obj_set_style_text_font(refs.bigLossVal, &lv_font_montserrat_14, 0);
}
