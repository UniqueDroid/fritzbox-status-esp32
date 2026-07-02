// LVGL dashboard runtime: display driver, page switching, button handling and UI refresh.
#include "dashboard.h"

#include <lvgl.h>
#include <WiFi.h>
#include <driver/gpio.h>

#include "alerts.h"
#include "config_portal.h"
#include "globals.h"
#include "lvgl_screens.h"
#include "firmware_update.h"
#include "fritzbox_api.h"

namespace {
constexpr uint16_t kBufferLines = 24;
constexpr uint32_t kButtonDebounceMsLocal = 120;
constexpr uint32_t kUiRefreshMs = 250;
constexpr uint32_t kWifiRetryMsLocal = 10000;

static SemaphoreHandle_t xApiMutex = nullptr;
static volatile bool apiDataReady = false;

// Heap-allocated (not static) so the linker doesn't have to fit it in the
// fixed DRAM segment - matters on boards with less internal RAM (e.g. CYD).
static lv_color_t *drawBuf = nullptr;
static lv_disp_draw_buf_t dispDrawBuf;
static lv_disp_drv_t dispDrv;

static uint32_t lastTickMs = 0;
static uint32_t lastUiRefreshMs = 0;
static uint32_t lastWifiRetry = 0;
static uint32_t lastUpperActionMs = 0;
static uint32_t lastLowerActionMs = 0;

static bool lastUpperState = HIGH;
static bool lastLowerState = HIGH;
static uint8_t pageIndex = 0;
static uint8_t brightnessIndex = 3;
static const uint8_t brightnessLevels[] = {40, 96, 160, 255};

static lv_obj_t *pill = nullptr;
static lv_obj_t *updateIcon = nullptr;
static lv_obj_t *wifiSignalCanvas = nullptr;
static lv_color_t *wifiSignalCanvasBuf = nullptr;
static lv_obj_t *pages[3] = {nullptr, nullptr, nullptr};
static LvglScreenRefs refs;

static lv_obj_t *footerHostVal = nullptr;
static lv_obj_t *footerIpVal = nullptr;
// Button long-press state
static uint32_t upperPressStart = 0;
static bool upperIsDown = false;
static uint32_t lowerPressStart = 0;
static bool lowerIsDown = false;
static bool upperLongPressHandled = false;
static bool lowerLongPressHandled = false;
static volatile uint8_t upperPressCount = 0;
static volatile uint8_t upperReleaseCount = 0;
static volatile uint8_t lowerPressCount = 0;
static volatile uint8_t lowerReleaseCount = 0;
static uint32_t upperLastTransitionMs = 0;
static uint32_t lowerLastTransitionMs = 0;
constexpr uint32_t kLongPressMs = 850;
constexpr uint32_t kButtonTransitionDebounceMs = 25;

#define cpuBar refs.cpuBar
#define ramBar refs.ramBar
#define tempBar refs.tempBar
#define cpuVal refs.cpuVal
#define ramVal refs.ramVal
#define tempVal refs.tempVal
#define mainStatusCard refs.mainStatusCard
#define mainStatusVal refs.mainStatusVal
#define mainRttVal refs.mainRttVal
#define mainRttSdLabel refs.mainRttSdLabel
#define mainRttSdVal refs.mainRttSdVal
#define mainLossVal refs.mainLossVal
#define mainHostVal refs.mainHostVal
#define mainIpVal refs.mainIpVal
#define mainChartRxVal refs.mainChartRxVal
#define mainChartTxVal refs.mainChartTxVal
#define mainTrafficChart refs.mainTrafficChart
#define mainRxSeries refs.mainRxSeries
#define mainTxSeries refs.mainTxSeries
#define chartRxVal refs.chartRxVal
#define chartTxVal refs.chartTxVal
#define trafficChart refs.trafficChart
#define rxSeries refs.rxSeries
#define txSeries refs.txSeries
#define rttVal refs.rttVal
#define rttSdVal refs.rttSdVal
#define lossVal refs.lossVal
#define uptimeVal refs.uptimeVal
#define hostVal refs.hostVal
#define bigCpuBar refs.bigCpuBar
#define bigCpuVal refs.bigCpuVal
#define bigRamBar refs.bigRamBar
#define bigRamVal refs.bigRamVal
#define bigTempBar refs.bigTempBar
#define bigTempVal refs.bigTempVal
#define bigRttVal refs.bigRttVal
#define bigRttSdVal refs.bigRttSdVal
#define bigLossVal refs.bigLossVal
#define espInfoSsid refs.espInfoSsid
#define espInfoIp refs.espInfoIp
#define espInfoMac refs.espInfoMac
#define espInfoRssi refs.espInfoRssi
#define espInfoGw refs.espInfoGw
#define espInfoSubnet refs.espInfoSubnet
#define espInfoChip refs.espInfoChip
#define espInfoCpu refs.espInfoCpu
#define espInfoHeap refs.espInfoHeap
#define espInfoFlash refs.espInfoFlash
#define espInfoUptime refs.espInfoUptime

String formatUptimeLocal(uint32_t uptimeSeconds) {
  uint32_t hours = uptimeSeconds / 3600;
  uint32_t minutes = (uptimeSeconds % 3600) / 60;
  uint32_t seconds = uptimeSeconds % 60;
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
  return String(buffer);
}

void applyBrightness() {
#if defined(TFT_BL)
  analogWrite(TFT_BL, brightnessLevels[brightnessIndex]);
#endif
}

void IRAM_ATTR onUpperButtonChange() {
  if (gpio_get_level(GPIO_NUM_14) == 0) {
    if (upperPressCount < 255) upperPressCount++;
  } else {
    if (upperReleaseCount < 255) upperReleaseCount++;
  }
}

void IRAM_ATTR onLowerButtonChange() {
  if (gpio_get_level(GPIO_NUM_0) == 0) {
    if (lowerPressCount < 255) lowerPressCount++;
  } else {
    if (lowerReleaseCount < 255) lowerReleaseCount++;
  }
}

void cycleBrightness() {
  if (brightnessIndex == 0) {
    brightnessIndex = 3;
  } else {
    brightnessIndex--;
  }
  applyBrightness();
}

void flushCallback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&colorP->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void setPage(uint8_t index) {
  pageIndex = index % 3;
  for (int i = 0; i < 3; ++i) {
    if (i == pageIndex) {
      lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  // Page 0 has its own status card; hide global pill, shift WiFi bars to far right.
  if (pageIndex == 0) {
    lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
    if (mainHostVal) lv_obj_add_flag(mainHostVal, LV_OBJ_FLAG_HIDDEN);
    if (mainIpVal) lv_obj_add_flag(mainIpVal, LV_OBJ_FLAG_HIDDEN);
    if (wifiSignalCanvas) lv_obj_align(wifiSignalCanvas, LV_ALIGN_TOP_RIGHT, -10, 7);
  } else {
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(pill, LV_SYMBOL_OK);
    // Subpages use a plain icon-only status (no badge background).
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(pill, 0, 0);
    lv_obj_set_style_pad_right(pill, 0, 0);
    lv_obj_set_style_pad_top(pill, 0, 0);
    lv_obj_set_style_pad_bottom(pill, 0, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -30, 7);
    if (mainHostVal) lv_obj_clear_flag(mainHostVal, LV_OBJ_FLAG_HIDDEN);
    if (mainIpVal) lv_obj_clear_flag(mainIpVal, LV_OBJ_FLAG_HIDDEN);
    if (wifiSignalCanvas) lv_obj_align(wifiSignalCanvas, LV_ALIGN_TOP_RIGHT, -10, 7);
  }
}

void refreshUpdateBadge() {
  if (!updateIcon) {
    return;
  }

  if (firmwareUpdateAvailable) {
    lv_obj_clear_flag(updateIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(updateIcon);
  } else {
    lv_obj_add_flag(updateIcon, LV_OBJ_FLAG_HIDDEN);
  }
}

// Draw a 4-bar signal-strength icon onto wifiSignalCanvas.
// 0 activeBars = no signal; 4 = full signal.
void refreshWifiSignalIcon() {
  if (!wifiSignalCanvas) return;

  bool connected = (WiFi.status() == WL_CONNECTED);
  int  rssi      = connected ? (int)WiFi.RSSI() : -100;

  int activeBars = 0;
  if (connected) {
    if      (rssi >= -55) activeBars = 4;
    else if (rssi >= -67) activeBars = 3;
    else if (rssi >= -75) activeBars = 2;
    else if (rssi >= -82) activeBars = 1;
  }

  // Fill with header background so the icon blends seamlessly.
  lv_canvas_fill_bg(wifiSignalCanvas, lv_color_hex(0x1F2328), LV_OPA_COVER);

  lv_color_t activeColor   = connected ? lv_color_hex(0x83F7AF) : lv_color_hex(0x6B6BFF);
  lv_color_t inactiveColor = lv_color_hex(0x35404C);

  // 4 bars: width 3px, 1px gap between, increasing heights (canvas is 15x14).
  const int canvasH  = 14;
  const int barW     = 3;
  const int barH[4]  = {4, 7, 10, 13};

  lv_draw_rect_dsc_t rdsc;
  lv_draw_rect_dsc_init(&rdsc);
  rdsc.radius       = 1;
  rdsc.border_width = 0;

  for (int i = 0; i < 4; ++i) {
    rdsc.bg_color = (i < activeBars) ? activeColor : inactiveColor;
    rdsc.bg_opa   = (i < activeBars) ? LV_OPA_COVER : LV_OPA_40;
    lv_coord_t x  = i * (barW + 1);
    lv_coord_t y  = canvasH - barH[i];
    lv_canvas_draw_rect(wifiSignalCanvas, x, y, barW, barH[i], &rdsc);
  }
}

void refreshChartData() {
  auto drawTraffic = [&](lv_obj_t *canvas, int w, int h) {
    if (!canvas || kTrafficPoints < 2) {
      return;
    }

    lv_canvas_fill_bg(canvas, lv_color_hex(0x171C24), LV_OPA_COVER);

    const int gx = 4;
    const int gy = 4;
    const int gw = w - 8;
    const int gh = h - 8;
    if (gw <= 1 || gh <= 1) {
      return;
    }

    float maxRxKbps = 1.0f;
    float maxTxKbps = 1.0f;
    for (int i = 0; i < kTrafficPoints; ++i) {
      if (wanRxHistory[i] > maxRxKbps) maxRxKbps = wanRxHistory[i];
      if (wanTxHistory[i] > maxTxKbps) maxTxKbps = wanTxHistory[i];
    }

    lv_draw_line_dsc_t rxDsc;
    lv_draw_line_dsc_init(&rxDsc);
    rxDsc.color = lv_color_hex(0x2AC7D8);
    rxDsc.width = 1;

    lv_draw_line_dsc_t txDsc;
    lv_draw_line_dsc_init(&txDsc);
    txDsc.color = lv_color_hex(0xF5B942);
    txDsc.width = 1;

    for (int i = 1; i < kTrafficPoints; ++i) {
      int x1 = gx + ((i - 1) * (gw - 1)) / (kTrafficPoints - 1);
      int x2 = gx + (i * (gw - 1)) / (kTrafficPoints - 1);

      int rxY1 = gy + gh - 1 - (int)((wanRxHistory[i - 1] / maxRxKbps) * (gh - 1));
      int rxY2 = gy + gh - 1 - (int)((wanRxHistory[i] / maxRxKbps) * (gh - 1));
      lv_point_t rxPts[2] = {{(lv_coord_t)x1, (lv_coord_t)rxY1}, {(lv_coord_t)x2, (lv_coord_t)rxY2}};
      lv_canvas_draw_line(canvas, rxPts, 2, &rxDsc);

      int txY1 = gy + gh - 1 - (int)((wanTxHistory[i - 1] / maxTxKbps) * (gh - 1));
      int txY2 = gy + gh - 1 - (int)((wanTxHistory[i] / maxTxKbps) * (gh - 1));
      lv_point_t txPts[2] = {{(lv_coord_t)x1, (lv_coord_t)txY1}, {(lv_coord_t)x2, (lv_coord_t)txY2}};
      lv_canvas_draw_line(canvas, txPts, 2, &txDsc);
    }
  };

  drawTraffic(trafficChart, DASHBOARD_WIDTH - 20, 90);
  drawTraffic(mainTrafficChart, 114, 76);
}

void refreshLiveDataUi() {
  bool isOnline = (wanStatus == "online" || wanStatus == "none");
  if (pageIndex == 0) {
    lv_label_set_text(pill, isOnline ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(pill, 10, 0);
    lv_obj_set_style_pad_right(pill, 10, 0);
    lv_obj_set_style_pad_top(pill, 4, 0);
    lv_obj_set_style_pad_bottom(pill, 4, 0);
  } else {
    lv_label_set_text(pill, isOnline ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(pill, 0, 0);
    lv_obj_set_style_pad_right(pill, 0, 0);
    lv_obj_set_style_pad_top(pill, 0, 0);
    lv_obj_set_style_pad_bottom(pill, 0, 0);
  }
  lv_obj_set_style_bg_color(pill, isOnline ? lv_color_hex(0x174E2E) : lv_color_hex(0x5A1E1E), 0);
  lv_obj_set_style_text_color(pill, isOnline ? lv_color_hex(0x83F7AF) : lv_color_hex(0xFF8B8B), 0);

  if (mainStatusVal) {
    lv_label_set_text(mainStatusVal, isOnline ? "Online" : "Offline");
    lv_obj_set_style_text_color(mainStatusVal, isOnline ? lv_color_hex(0x83F7AF) : lv_color_hex(0xFF8B8B), 0);
  }
  if (mainStatusCard) {
    lv_obj_set_style_bg_color(mainStatusCard, isOnline ? lv_color_hex(0x174E2E) : lv_color_hex(0x5A1E1E), 0);
    lv_obj_set_style_border_color(mainStatusCard, isOnline ? lv_color_hex(0x2A9D63) : lv_color_hex(0xC85B5B), 0);
  }

  lv_bar_set_value(cpuBar, cpuPercent, LV_ANIM_OFF);
  lv_bar_set_value(ramBar, memPercent, LV_ANIM_OFF);
  lv_bar_set_value(tempBar, tempPercent, LV_ANIM_OFF);
  lv_label_set_text_fmt(cpuVal, "%d%%", cpuPercent);
  lv_label_set_text_fmt(ramVal, "%d%%", memPercent);
  lv_label_set_text(tempVal, tempValue.c_str());

  refreshChartData();
  lv_label_set_text_fmt(chartRxVal, "RX %.1f kbps", wanRxHistory[kTrafficPoints - 1]);
  lv_label_set_text_fmt(chartTxVal, "TX %.1f kbps", wanTxHistory[kTrafficPoints - 1]);
  if (mainChartRxVal) {
    lv_label_set_text_fmt(mainChartRxVal, "R %.1f", wanRxHistory[kTrafficPoints - 1]);
  }
  if (mainChartTxVal) {
    lv_label_set_text_fmt(mainChartTxVal, "T %.1f", wanTxHistory[kTrafficPoints - 1]);
  }

  if (rttVal) lv_label_set_text(rttVal, wanDelay.c_str());
  if (rttSdVal) lv_label_set_text(rttSdVal, wanRttSd.c_str());
  if (lossVal) lv_label_set_text(lossVal, wanLoss.c_str());
  if (uptimeVal) lv_label_set_text(uptimeVal, lastUpdate.c_str());
  if (hostVal) lv_label_set_text(hostVal, fritzBoxHost);

  if (mainRttVal) {
    lv_label_set_text(mainRttVal, wanDelay.c_str());
  }
  if (mainRttSdVal) {
    lv_label_set_text(mainRttSdVal, wanRttSd.c_str());
  }
  bool hasRttSd = (wanRttSd != "-");
  if (mainRttSdLabel) {
    if (hasRttSd) {
      lv_obj_clear_flag(mainRttSdLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(mainRttSdLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (mainRttSdVal) {
    if (hasRttSd) {
      lv_obj_clear_flag(mainRttSdVal, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(mainRttSdVal, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (mainLossVal) {
    lv_label_set_text(mainLossVal, wanLoss.c_str());
  }
  if (mainHostVal) {
    lv_label_set_text_fmt(mainHostVal, "Host: %s", fritzBoxHost);
  }
  if (mainIpVal) {
    lv_label_set_text_fmt(mainIpVal, "IP: %s", WiFi.localIP().toString().c_str());
  }

  if (bigCpuVal) {
    lv_label_set_text_fmt(bigCpuVal, "%d%%", cpuPercent);
  }
  if (bigCpuBar) {
    lv_bar_set_value(bigCpuBar, cpuPercent, LV_ANIM_OFF);
  }
  if (bigRamVal) {
    lv_label_set_text_fmt(bigRamVal, "%d%%", memPercent);
  }
  if (bigRamBar) {
    lv_bar_set_value(bigRamBar, memPercent, LV_ANIM_OFF);
  }
  if (bigTempVal) {
    lv_label_set_text(bigTempVal, tempValue.c_str());
  }
  if (bigTempBar) {
    lv_bar_set_value(bigTempBar, tempPercent, LV_ANIM_OFF);
  }
  if (bigRttVal) {
    lv_label_set_text(bigRttVal, wanDelay.c_str());
  }
  if (bigRttSdVal) {
    lv_label_set_text(bigRttSdVal, wanRttSd.c_str());
  }
  if (bigLossVal) {
    lv_label_set_text(bigLossVal, wanLoss.c_str());
  }

  // Update WiFi signal bars based on connection state
  refreshWifiSignalIcon();
  if (updateIcon) {
    lv_obj_set_style_text_color(updateIcon,
      firmwareUpdateAvailable ? lv_color_hex(0xF5B942) : lv_color_hex(0x8E9BAC), 0);
  }
  // Update global footer
  if (footerHostVal) {
    lv_label_set_text_fmt(footerHostVal, "Host: %s", fritzBoxHost);
  }
  if (footerIpVal) {
    lv_label_set_text_fmt(footerIpVal, "IP: %s", WiFi.localIP().toString().c_str());
  }
  // Update ESP & WiFi info screen (screen 4)
  if (espInfoSsid)   lv_label_set_text_fmt(espInfoSsid,   "%s", WiFi.SSID().c_str());
  if (espInfoIp)     lv_label_set_text_fmt(espInfoIp,     "%s", WiFi.localIP().toString().c_str());
  if (espInfoMac)    lv_label_set_text_fmt(espInfoMac,    "%s", WiFi.macAddress().c_str());
  if (espInfoRssi)   lv_label_set_text_fmt(espInfoRssi,   "%d dBm", (int)WiFi.RSSI());
  if (espInfoGw)     lv_label_set_text_fmt(espInfoGw,     "%s", WiFi.gatewayIP().toString().c_str());
  if (espInfoSubnet) lv_label_set_text_fmt(espInfoSubnet, "%s", WiFi.subnetMask().toString().c_str());
  if (espInfoHeap)   lv_label_set_text_fmt(espInfoHeap,   "%lu B", (unsigned long)ESP.getFreeHeap());
  if (espInfoUptime) lv_label_set_text_fmt(espInfoUptime, "%s", lastUpdate.c_str());
}

void createUi() {
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101318), 0);

  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, DASHBOARD_WIDTH, 28);
  lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(header, lv_color_hex(0x1F2328), 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(header, 0, 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "FRITZ!Box Status");
  lv_obj_set_style_text_color(title, lv_color_hex(0xDDE7F2), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  pill = lv_label_create(screen);
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

  wifiSignalCanvas = lv_canvas_create(screen);
  lv_canvas_set_buffer(wifiSignalCanvas, wifiSignalCanvasBuf, 15, 14, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(wifiSignalCanvas, LV_ALIGN_TOP_RIGHT, -88, 7);

  updateIcon = lv_label_create(screen);
  lv_label_set_text(updateIcon, LV_SYMBOL_DOWNLOAD);
  lv_obj_set_style_text_font(updateIcon, &lv_font_montserrat_14, 0);
  lv_obj_align(updateIcon, LV_ALIGN_TOP_RIGHT, -64, 8);
  lv_obj_add_flag(updateIcon, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < 3; ++i) {
    pages[i] = lv_obj_create(screen);
    lv_obj_remove_style_all(pages[i]);
    lv_obj_set_size(pages[i], DASHBOARD_WIDTH, DASHBOARD_HEIGHT - 28);
    lv_obj_align(pages[i], LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_bg_opa(pages[i], LV_OPA_TRANSP, 0);
  }

  createScreenDashboard(pages[0], refs, kTrafficPoints);
  createScreenGraph(pages[1], refs, kTrafficPoints);
  createScreenMetrics(pages[2], refs);

  // Global footer on all screens
  lv_obj_t *footer = lv_obj_create(screen);
  lv_obj_remove_style_all(footer);
  lv_obj_set_size(footer, DASHBOARD_WIDTH, 16);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(footer, lv_color_hex(0x1F2328), 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(footer, 0, 0);

  footerHostVal = lvglCreateValueLabel(footer, "Host: -", 6, 2, lv_color_hex(0x8E9BAC));
  footerIpVal   = lvglCreateValueLabel(footer, "IP: -",   168, 2, lv_color_hex(0x8E9BAC));

  setPage(0);
  refreshLiveDataUi();
  refreshWifiSignalIcon();
  refreshUpdateBadge();
}
}  // namespace

void initDashboard() {
  lv_init();

  drawBuf = static_cast<lv_color_t *>(malloc(DASHBOARD_WIDTH * kBufferLines * sizeof(lv_color_t)));
  wifiSignalCanvasBuf = static_cast<lv_color_t *>(malloc(15 * 14 * sizeof(lv_color_t)));
  lv_disp_draw_buf_init(&dispDrawBuf, drawBuf, nullptr, DASHBOARD_WIDTH * kBufferLines);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = DASHBOARD_WIDTH;
  dispDrv.ver_res = DASHBOARD_HEIGHT;
  dispDrv.flush_cb = flushCallback;
  dispDrv.draw_buf = &dispDrawBuf;
  lv_disp_drv_register(&dispDrv);

#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
  pinMode(0, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(14), onUpperButtonChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(0), onLowerButtonChange, CHANGE);
#endif

  createUi();
  applyBrightness();
  lastUpperState = digitalRead(14);
  lastLowerState = digitalRead(0);
  lastUiRefreshMs = millis();
  lastTickMs = millis();

  xApiMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    [](void *) {
      const TickType_t xInterval = pdMS_TO_TICKS(kPollMs);
      TickType_t xLastWake = xTaskGetTickCount();
      uint32_t lastReleaseCheckMs = 0;
      bool firstReleaseCheckDone = false;
      for (;;) {
        vTaskDelayUntil(&xLastWake, xInterval);
        if (WiFi.status() != WL_CONNECTED) continue;
        if (xSemaphoreTake(xApiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
          fetchGatewayStatus();
          fetchWanTraffic();
          fetchSystemStatus();
          checkAndSendAlerts();
          checkAndSendStatusDigest();
          checkTelegramCommands();
          updateDailyStats();
          checkAndSendDailySummary();
          // Check once shortly after boot (not just every 15 min from a
          // zero-initialized timer, which used to delay the very first
          // check - and therefore the update badge/notification - by a
          // full 15 minutes after every reboot).
          if (!firstReleaseCheckDone || (millis() - lastReleaseCheckMs) >= 15UL * 60UL * 1000UL) {
            FirmwareReleaseInfo releaseInfo;
            String errorMessage;
            if (fetchLatestFirmwareRelease(releaseInfo, errorMessage)) {
              firmwareUpdateAvailable = releaseInfo.updateAvailable;
              checkAndNotifyFirmwareUpdate(releaseInfo.updateAvailable, releaseInfo.latestVersion);
            }
            lastReleaseCheckMs = millis();
            firstReleaseCheckDone = true;
          }
          xSemaphoreGive(xApiMutex);
          apiDataReady = true;
        }
      }
    },
    "apiPoll", 8192, nullptr, 1, nullptr, 0
  );
}

void loopDashboard() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastTickMs;
  lastTickMs = now;

  markBootDashboardReady();
  dismissBootScreenIfConnected();
  processPendingSnapshotRequest();

  wm.process();
  if (wm.server) {
    wm.server->handleClient();
  }

  if (shouldSaveConfig) {
    shouldSaveConfig = false;
    handleConfigSavedTransition();
  }

  bool upperState = digitalRead(14);
  bool lowerState = digitalRead(0);

  // Consume ISR-produced edge counters in the main loop to keep interrupts short
  // and apply debouncing/gesture logic in one place.
  while (upperPressCount > 0) {
    upperPressCount--;
    if ((now - upperLastTransitionMs) < kButtonTransitionDebounceMs) {
      continue;
    }
    upperLastTransitionMs = now;
    upperPressStart = now;
    upperIsDown = true;
    upperLongPressHandled = false;
  }
  while (upperReleaseCount > 0) {
    upperReleaseCount--;
    if ((now - upperLastTransitionMs) < kButtonTransitionDebounceMs) {
      continue;
    }
    upperLastTransitionMs = now;
    if (upperIsDown && !upperLongPressHandled && (now - upperPressStart) < kLongPressMs && (now - lastUpperActionMs) >= kButtonDebounceMsLocal) {
      setPage((pageIndex + 1) % 3);
      lastUpperActionMs = now;
    }
    upperIsDown = false;
    upperLongPressHandled = false;
  }

  while (lowerPressCount > 0) {
    lowerPressCount--;
    if ((now - lowerLastTransitionMs) < kButtonTransitionDebounceMs) {
      continue;
    }
    lowerLastTransitionMs = now;
    lowerPressStart = now;
    lowerIsDown = true;
    lowerLongPressHandled = false;
  }
  while (lowerReleaseCount > 0) {
    lowerReleaseCount--;
    if ((now - lowerLastTransitionMs) < kButtonTransitionDebounceMs) {
      continue;
    }
    lowerLastTransitionMs = now;
    if (lowerIsDown && !lowerLongPressHandled && (now - lowerPressStart) < kLongPressMs && (now - lastLowerActionMs) >= kButtonDebounceMsLocal) {
      cycleBrightness();
      lastLowerActionMs = now;
    }
    lowerIsDown = false;
    lowerLongPressHandled = false;
  }

  // Long press currently only suppresses short-press actions. Hook point for
  // future long-press features without touching ISR code.
  if (upperIsDown && !upperLongPressHandled && (now - upperPressStart) >= kLongPressMs) {
    upperLongPressHandled = true;
  }
  if (lowerIsDown && !lowerLongPressHandled && (now - lowerPressStart) >= kLongPressMs) {
    lowerLongPressHandled = true;
  }

  lastUpperState = upperState;
  lastLowerState = lowerState;

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected) {
    if (now - lastWifiRetry >= kWifiRetryMsLocal) {
      lastWifiRetry = now;
      WiFi.reconnect();
    }
    wanStatus = "offline";
  }

  if (apiDataReady) {
    apiDataReady = false;
    lastUpdate = formatUptimeLocal(now / 1000);
  }

  if ((now - lastUiRefreshMs) >= kUiRefreshMs) {
    lastUiRefreshMs = now;
    // Prefer mutex-protected snapshot; fallback keeps UI responsive during API polling.
    if (xSemaphoreTake(xApiMutex, 0) == pdTRUE) {
      refreshLiveDataUi();
      refreshUpdateBadge();
      xSemaphoreGive(xApiMutex);
    } else {
      refreshLiveDataUi(); // stale data fine while API runs
      refreshUpdateBadge();
    }
  }

  lv_tick_inc(elapsed);
  lv_timer_handler();
  delay(5);
}
