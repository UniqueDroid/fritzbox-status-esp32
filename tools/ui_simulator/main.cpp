// Native (PC) renderer that reuses the real screen-layout code
// (createScreenDashboard/Graph/Metrics from src/dashboard_*.cpp) with an SDL2
// software display driver instead of TFT_eSPI, so the actual device screens
// can be captured as PNGs for the README without physical hardware.
//
// Not part of the firmware build - see tools/ui_simulator/README.md.
#include <SDL2/SDL.h>
#include <lvgl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "globals.h"
#include "lvgl_screens.h"

namespace {

constexpr int kWidth = DASHBOARD_WIDTH;
constexpr int kHeight = DASHBOARD_HEIGHT;
constexpr int kTrafficPoints = 40;

SDL_Renderer *gRenderer = nullptr;
SDL_Texture *gTexture = nullptr;

void flushCb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  SDL_Rect rect{area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1};
  SDL_UpdateTexture(gTexture, &rect, colorP, rect.w * static_cast<int>(sizeof(lv_color_t)));
  lv_disp_flush_ready(disp);
}

// Mirrors dashboard.cpp's refreshChartData() traffic-line drawing, but reads
// from plain float arrays instead of the ESP32 wanRxHistory/wanTxHistory globals.
void drawTraffic(lv_obj_t *canvas, int w, int h, const float *rx, const float *tx, int points) {
  if (!canvas || points < 2) {
    return;
  }
  lv_canvas_fill_bg(canvas, lv_color_hex(0x171C24), LV_OPA_COVER);

  const int gx = 4, gy = 4, gw = w - 8, gh = h - 8;
  if (gw <= 1 || gh <= 1) {
    return;
  }

  float maxRx = 1.0f, maxTx = 1.0f;
  for (int i = 0; i < points; ++i) {
    if (rx[i] > maxRx) maxRx = rx[i];
    if (tx[i] > maxTx) maxTx = tx[i];
  }

  lv_draw_line_dsc_t rxDsc;
  lv_draw_line_dsc_init(&rxDsc);
  rxDsc.color = lv_color_hex(0x2AC7D8);
  rxDsc.width = 1;

  lv_draw_line_dsc_t txDsc;
  lv_draw_line_dsc_init(&txDsc);
  txDsc.color = lv_color_hex(0xF5B942);
  txDsc.width = 1;

  for (int i = 1; i < points; ++i) {
    int x1 = gx + ((i - 1) * (gw - 1)) / (points - 1);
    int x2 = gx + (i * (gw - 1)) / (points - 1);

    int rxY1 = gy + gh - 1 - static_cast<int>((rx[i - 1] / maxRx) * (gh - 1));
    int rxY2 = gy + gh - 1 - static_cast<int>((rx[i] / maxRx) * (gh - 1));
    lv_point_t rxPts[2] = {{static_cast<lv_coord_t>(x1), static_cast<lv_coord_t>(rxY1)},
                           {static_cast<lv_coord_t>(x2), static_cast<lv_coord_t>(rxY2)}};
    lv_canvas_draw_line(canvas, rxPts, 2, &rxDsc);

    int txY1 = gy + gh - 1 - static_cast<int>((tx[i - 1] / maxTx) * (gh - 1));
    int txY2 = gy + gh - 1 - static_cast<int>((tx[i] / maxTx) * (gh - 1));
    lv_point_t txPts[2] = {{static_cast<lv_coord_t>(x1), static_cast<lv_coord_t>(txY1)},
                           {static_cast<lv_coord_t>(x2), static_cast<lv_coord_t>(txY2)}};
    lv_canvas_draw_line(canvas, txPts, 2, &txDsc);
  }
}

void saveScreenshot(const char *path) {
  // lv_timer_handler()'s internal refresh timer is gated by elapsed lv_tick
  // time, which nothing advances in this simulator (no real event loop) -
  // without ticking it forward, later captures would just re-flush the first
  // frame instead of picking up page-visibility changes.
  lv_tick_inc(50);
  lv_timer_handler();
  lv_tick_inc(50);
  lv_timer_handler();

  SDL_RenderClear(gRenderer);
  SDL_RenderCopy(gRenderer, gTexture, nullptr, nullptr);
  SDL_RenderPresent(gRenderer);

  SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, kWidth, kHeight, 24, SDL_PIXELFORMAT_RGB24);
  SDL_RenderReadPixels(gRenderer, nullptr, SDL_PIXELFORMAT_RGB24, surf->pixels, surf->pitch);
  SDL_SaveBMP(surf, path);
  SDL_FreeSurface(surf);
  std::printf("wrote %s\n", path);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
    return 1;
  }
  const char *outDir = argv[1];

  setenv("SDL_VIDEODRIVER", "dummy", 0);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow("fritzbox-status-esp32 sim", SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED, kWidth, kHeight, SDL_WINDOW_HIDDEN);
  gRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, kWidth, kHeight);

  lv_init();

  static lv_color_t *drawBuf = static_cast<lv_color_t *>(malloc(kWidth * 40 * sizeof(lv_color_t)));
  static lv_disp_draw_buf_t dispDrawBuf;
  lv_disp_draw_buf_init(&dispDrawBuf, drawBuf, nullptr, kWidth * 40);

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kWidth;
  dispDrv.ver_res = kHeight;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &dispDrawBuf;
  lv_disp_drv_register(&dispDrv);

  // --- Recreate dashboard.cpp's createUi() header/pages/footer scaffold ---
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101318), 0);

  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, kWidth, 28);
  lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x1F2328), 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(header, 0, 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "FRITZ!Box Status");
  lv_obj_set_style_text_color(title, lv_color_hex(0xDDE7F2), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  lv_obj_t *pill = lv_label_create(screen);
  lv_label_set_text(pill, "ONLINE");
  lv_obj_set_style_bg_color(pill, lv_color_hex(0x174E2E), 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(pill, lv_color_hex(0x83F7AF), 0);
  lv_obj_set_style_pad_left(pill, 10, 0);
  lv_obj_set_style_pad_right(pill, 10, 0);
  lv_obj_set_style_pad_top(pill, 4, 0);
  lv_obj_set_style_pad_bottom(pill, 4, 0);
  lv_obj_set_style_radius(pill, 10, 0);
  lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -10, 7);

  static lv_color_t wifiBuf[15 * 14];
  lv_obj_t *wifiCanvas = lv_canvas_create(screen);
  lv_canvas_set_buffer(wifiCanvas, wifiBuf, 15, 14, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(wifiCanvas, LV_ALIGN_TOP_RIGHT, -88, 7);
  lv_canvas_fill_bg(wifiCanvas, lv_color_hex(0x1F2328), LV_OPA_COVER);
  {
    // Full 4-bar signal, matching refreshWifiSignalIcon()'s "good connection" case.
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.radius = 1;
    rdsc.border_width = 0;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.bg_color = lv_color_hex(0x83F7AF);
    const int barH[4] = {4, 7, 10, 13};
    for (int i = 0; i < 4; ++i) {
      lv_canvas_draw_rect(wifiCanvas, i * 4, 14 - barH[i], 3, barH[i], &rdsc);
    }
  }

  lv_obj_t *pages[3];
  LvglScreenRefs refs;
  for (int i = 0; i < 3; ++i) {
    pages[i] = lv_obj_create(screen);
    lv_obj_remove_style_all(pages[i]);
    lv_obj_set_size(pages[i], kWidth, kHeight - 28);
    lv_obj_align(pages[i], LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, 0);
  }

  createScreenDashboard(pages[0], refs, kTrafficPoints);
  createScreenGraph(pages[1], refs, kTrafficPoints);
  createScreenMetrics(pages[2], refs);

  lv_obj_t *footer = lv_obj_create(screen);
  lv_obj_remove_style_all(footer);
  lv_obj_set_size(footer, kWidth, 16);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(footer, lv_color_hex(0x1F2328), 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(footer, 0, 0);
  lvglCreateValueLabel(footer, "Host: fritz.box", 6, 2, lv_color_hex(0x8E9BAC));
  lvglCreateValueLabel(footer, "IP: 192.168.178.42", 168, 2, lv_color_hex(0x8E9BAC));

  // --- Mock data (mirrors refreshLiveDataUi() with fixed demo values) ---
  const int cpuPercent = 18, memPercent = 42, tempPercent = 55;
  const char *tempValue = "46C";
  const char *wanDelay = "11.2 ms";
  const char *wanRttSd = "1.4 ms";
  const char *wanLoss = "0.0%";

  lv_bar_set_value(refs.cpuBar, cpuPercent, LV_ANIM_OFF);
  lv_bar_set_value(refs.ramBar, memPercent, LV_ANIM_OFF);
  lv_bar_set_value(refs.tempBar, tempPercent, LV_ANIM_OFF);
  lv_label_set_text_fmt(refs.cpuVal, "%d%%", cpuPercent);
  lv_label_set_text_fmt(refs.ramVal, "%d%%", memPercent);
  lv_label_set_text(refs.tempVal, tempValue);

  lv_label_set_text(refs.mainStatusVal, "Online");
  lv_label_set_text(refs.mainRttVal, wanDelay);
  lv_label_set_text(refs.mainRttSdVal, wanRttSd);
  lv_label_set_text(refs.mainLossVal, wanLoss);
  lv_label_set_text(refs.mainHostVal, "Host: fritz.box");
  lv_label_set_text(refs.mainIpVal, "IP: 192.168.178.42");

  lv_label_set_text(refs.chartRxVal, "RX 842.3 kbps");
  lv_label_set_text(refs.chartTxVal, "TX 213.7 kbps");
  // mainChartRxVal/mainChartTxVal are declared in LvglScreenRefs but never
  // created by createScreenDashboard() - real refreshLiveDataUi() guards
  // them with an "if (mainChartRxVal)" null check, so skip them here too.

  lv_label_set_text_fmt(refs.bigCpuVal, "%d%%", cpuPercent);
  lv_bar_set_value(refs.bigCpuBar, cpuPercent, LV_ANIM_OFF);
  lv_label_set_text_fmt(refs.bigRamVal, "%d%%", memPercent);
  lv_bar_set_value(refs.bigRamBar, memPercent, LV_ANIM_OFF);
  lv_label_set_text(refs.bigTempVal, tempValue);
  lv_bar_set_value(refs.bigTempBar, tempPercent, LV_ANIM_OFF);
  lv_label_set_text(refs.bigRttVal, wanDelay);
  lv_label_set_text(refs.bigRttSdVal, wanRttSd);
  lv_label_set_text(refs.bigLossVal, wanLoss);

  float rx[kTrafficPoints];
  float tx[kTrafficPoints];
  for (int i = 0; i < kTrafficPoints; ++i) {
    rx[i] = 420.0f + 320.0f * std::sin(i * 0.35f) + 180.0f * std::sin(i * 0.09f + 0.6f);
    tx[i] = 130.0f + 90.0f * std::sin(i * 0.28f + 1.2f);
    if (rx[i] < 15.0f) rx[i] = 15.0f;
    if (tx[i] < 8.0f) tx[i] = 8.0f;
  }
  drawTraffic(refs.trafficChart, kWidth - 20, 90, rx, tx, kTrafficPoints);
  drawTraffic(refs.mainTrafficChart, 114, 76, rx, tx, kTrafficPoints);

  auto showPage = [&](int idx) {
    for (int i = 0; i < 3; ++i) {
      if (i == idx) {
        lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
    // Mirrors setPage() in dashboard.cpp: page 0 uses its own inline status
    // card instead of the header pill, and hides the host/IP footer labels.
    if (idx == 0) {
      lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(refs.mainHostVal, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(refs.mainIpVal, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(pill, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(pill, LV_SYMBOL_OK);
      lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
      lv_obj_set_style_pad_left(pill, 0, 0);
      lv_obj_set_style_pad_right(pill, 0, 0);
      lv_obj_set_style_pad_top(pill, 0, 0);
      lv_obj_set_style_pad_bottom(pill, 0, 0);
      lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -30, 7);
      lv_obj_clear_flag(refs.mainHostVal, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(refs.mainIpVal, LV_OBJ_FLAG_HIDDEN);
    }
  };

  char path[512];
  showPage(0);
  std::snprintf(path, sizeof(path), "%s/dashboard.bmp", outDir);
  saveScreenshot(path);

  showPage(1);
  std::snprintf(path, sizeof(path), "%s/graph.bmp", outDir);
  saveScreenshot(path);

  showPage(2);
  std::snprintf(path, sizeof(path), "%s/metrics.bmp", outDir);
  saveScreenshot(path);

  SDL_DestroyTexture(gTexture);
  SDL_DestroyRenderer(gRenderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
