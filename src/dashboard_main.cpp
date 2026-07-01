// Main dashboard page layout (status card, compact metric bars and mini traffic chart).
#include "lvgl_screens.h"

namespace {
constexpr int kMainGraphW = 114;
constexpr int kMainGraphH = 76;
static lv_color_t sMainGraphBuf[kMainGraphW * kMainGraphH];
}

void createScreenDashboard(lv_obj_t *page, LvglScreenRefs &refs, int trafficPoints) {
  lv_obj_t *cpuLabel = lv_label_create(page);
  lv_label_set_text(cpuLabel, "CPU");
  lv_obj_set_style_text_color(cpuLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(cpuLabel, LV_ALIGN_TOP_LEFT, 10, 2);

  refs.cpuBar = lv_bar_create(page);
  lv_obj_set_size(refs.cpuBar, 102, 12);
  lv_obj_align(refs.cpuBar, LV_ALIGN_TOP_LEFT, 52, 4);
  lv_bar_set_range(refs.cpuBar, 0, 100);
  lv_obj_set_style_bg_color(refs.cpuBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(refs.cpuBar, lv_color_hex(0x2AC7D8), LV_PART_INDICATOR);

  lv_obj_t *ramLabel = lv_label_create(page);
  lv_label_set_text(ramLabel, "RAM");
  lv_obj_set_style_text_color(ramLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(ramLabel, LV_ALIGN_TOP_LEFT, 10, 24);

  refs.ramBar = lv_bar_create(page);
  lv_obj_set_size(refs.ramBar, 102, 12);
  lv_obj_align(refs.ramBar, LV_ALIGN_TOP_LEFT, 52, 26);
  lv_bar_set_range(refs.ramBar, 0, 100);
  lv_obj_set_style_bg_color(refs.ramBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(refs.ramBar, lv_color_hex(0xF5B942), LV_PART_INDICATOR);

  lv_obj_t *tempLabel = lv_label_create(page);
  lv_label_set_text(tempLabel, "TEMP");
  lv_obj_set_style_text_color(tempLabel, lv_color_hex(0xA8B3C0), 0);
  lv_obj_align(tempLabel, LV_ALIGN_TOP_LEFT, 10, 46);

  refs.tempBar = lv_bar_create(page);
  lv_obj_set_size(refs.tempBar, 102, 12);
  lv_obj_align(refs.tempBar, LV_ALIGN_TOP_LEFT, 52, 48);
  lv_bar_set_range(refs.tempBar, 0, 100);
  lv_obj_set_style_bg_color(refs.tempBar, lv_color_hex(0x222833), LV_PART_MAIN);
  lv_obj_set_style_bg_color(refs.tempBar, lv_color_hex(0xE66565), LV_PART_INDICATOR);

  refs.cpuVal = lvglCreateValueLabel(page, "0%", 160, 1, lv_color_hex(0xDDE7F2));
  refs.ramVal = lvglCreateValueLabel(page, "0%", 160, 23, lv_color_hex(0xDDE7F2));
  refs.tempVal = lvglCreateValueLabel(page, "-", 160, 45, lv_color_hex(0xDDE7F2));

  lvglCreateValueLabel(page, "RTT", 10, 93, lv_color_hex(0xA8B3C0));
  refs.mainRttVal = lvglCreateValueLabel(page, "-", 42, 93, lv_color_hex(0xF5B942));
  refs.mainRttSdLabel = lvglCreateValueLabel(page, "RTTsd", 94, 93, lv_color_hex(0xDDE7F2));
  refs.mainRttSdVal = lvglCreateValueLabel(page, "-", 146, 93, lv_color_hex(0xF5B942));

  lvglCreateValueLabel(page, "Loss", 10, 107, lv_color_hex(0xA8B3C0));
  refs.mainLossVal = lvglCreateValueLabel(page, "-", 50, 107, lv_color_hex(0xF5B942));

  refs.mainStatusCard = lv_obj_create(page);
  lv_obj_remove_style_all(refs.mainStatusCard);            // strip blue LVGL default theme
  lv_obj_clear_flag(refs.mainStatusCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(refs.mainStatusCard, 114, 38);
  lv_obj_align(refs.mainStatusCard, LV_ALIGN_TOP_LEFT, 198, 2);
  lv_obj_set_style_radius(refs.mainStatusCard, 6, 0);
  lv_obj_set_style_bg_opa(refs.mainStatusCard, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(refs.mainStatusCard, lv_color_hex(0x174E2E), 0);
  lv_obj_set_style_border_width(refs.mainStatusCard, 2, 0);
  lv_obj_set_style_border_color(refs.mainStatusCard, lv_color_hex(0x4A545F), 0);
  lv_obj_set_style_pad_all(refs.mainStatusCard, 0, 0);
  lv_obj_set_style_text_font(refs.mainStatusCard, &lv_font_montserrat_14, 0);

  refs.mainStatusVal = lv_label_create(refs.mainStatusCard);
  lv_label_set_text(refs.mainStatusVal, "Online");
  lv_obj_set_style_text_color(refs.mainStatusVal, lv_color_hex(0x83F7AF), 0);
  lv_obj_center(refs.mainStatusVal);

  refs.mainTrafficChart = lv_canvas_create(page);
  lv_obj_set_size(refs.mainTrafficChart, 114, 76);
  lv_obj_align(refs.mainTrafficChart, LV_ALIGN_TOP_LEFT, 198, 48);
  lv_canvas_set_buffer(refs.mainTrafficChart, sMainGraphBuf, kMainGraphW, kMainGraphH, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(refs.mainTrafficChart, lv_color_hex(0x171C24), LV_OPA_COVER);
  lv_obj_set_style_bg_color(refs.mainTrafficChart, lv_color_hex(0x171C24), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(refs.mainTrafficChart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_line_color(refs.mainTrafficChart, lv_color_hex(0x293342), LV_PART_MAIN);
  lv_obj_set_style_border_color(refs.mainTrafficChart, lv_color_hex(0x4A545F), LV_PART_MAIN);
  lv_obj_set_style_border_width(refs.mainTrafficChart, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(refs.mainTrafficChart, 6, LV_PART_MAIN);

  refs.mainHostVal = lvglCreateValueLabel(page, "Host: -", 8, 123, lv_color_hex(0x8E9BAC));
  refs.mainIpVal = lvglCreateValueLabel(page, "IP: -", 188, 123, lv_color_hex(0x8E9BAC));
}
