// Full-width traffic graph page layout.
#include "lvgl_screens.h"
#include "globals.h"

namespace {
constexpr int kGraphW = DASHBOARD_WIDTH - 20;
constexpr int kGraphH = 90;
// Heap-allocated (not static) so the linker doesn't have to fit it in the
// fixed DRAM segment - matters on boards with less internal RAM (e.g. CYD).
static lv_color_t *sGraphBuf = nullptr;
}

void createScreenGraph(lv_obj_t *page, LvglScreenRefs &refs, int trafficPoints) {
  sGraphBuf = static_cast<lv_color_t *>(malloc(kGraphW * kGraphH * sizeof(lv_color_t)));
  refs.trafficChart = lv_canvas_create(page);
  lv_obj_set_size(refs.trafficChart, DASHBOARD_WIDTH - 20, 90);
  lv_obj_align(refs.trafficChart, LV_ALIGN_TOP_LEFT, 10, 14);
  lv_canvas_set_buffer(refs.trafficChart, sGraphBuf, kGraphW, kGraphH, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(refs.trafficChart, lv_color_hex(0x171C24), LV_OPA_COVER);
  lv_obj_set_style_bg_color(refs.trafficChart, lv_color_hex(0x171C24), LV_PART_MAIN);
  lv_obj_set_style_line_color(refs.trafficChart, lv_color_hex(0x293342), LV_PART_MAIN);
  lv_obj_set_style_border_color(refs.trafficChart, lv_color_hex(0x4A545F), LV_PART_MAIN);
  lv_obj_set_style_border_width(refs.trafficChart, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(refs.trafficChart, 6, LV_PART_MAIN);

  refs.chartRxVal = lvglCreateValueLabel(page, "RX 0.0 kbps", 10, 112, lv_color_hex(0x2AC7D8));
  refs.chartTxVal = lvglCreateValueLabel(page, "TX 0.0 kbps", 150, 112, lv_color_hex(0xF5B942));
}
