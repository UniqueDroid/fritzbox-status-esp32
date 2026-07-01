#pragma once

// LVGL screen/widget references shared between screen builders and runtime updater.
#include <lvgl.h>

struct LvglScreenRefs {
  lv_obj_t *cpuBar = nullptr;
  lv_obj_t *ramBar = nullptr;
  lv_obj_t *tempBar = nullptr;
  lv_obj_t *cpuVal = nullptr;
  lv_obj_t *ramVal = nullptr;
  lv_obj_t *tempVal = nullptr;

  lv_obj_t *mainStatusCard = nullptr;
  lv_obj_t *mainStatusVal = nullptr;
  lv_obj_t *mainRttVal = nullptr;
  lv_obj_t *mainRttSdLabel = nullptr;
  lv_obj_t *mainRttSdVal = nullptr;
  lv_obj_t *mainLossVal = nullptr;
  lv_obj_t *mainHostVal = nullptr;
  lv_obj_t *mainIpVal = nullptr;
  lv_obj_t *mainChartRxVal = nullptr;
  lv_obj_t *mainChartTxVal = nullptr;
  lv_obj_t *mainTrafficChart = nullptr;
  lv_chart_series_t *mainRxSeries = nullptr;
  lv_chart_series_t *mainTxSeries = nullptr;

  lv_obj_t *chartRxVal = nullptr;
  lv_obj_t *chartTxVal = nullptr;
  lv_obj_t *trafficChart = nullptr;
  lv_chart_series_t *rxSeries = nullptr;
  lv_chart_series_t *txSeries = nullptr;

  lv_obj_t *rttVal = nullptr;
  lv_obj_t *rttSdVal = nullptr;
  lv_obj_t *lossVal = nullptr;
  lv_obj_t *uptimeVal = nullptr;
  lv_obj_t *hostVal = nullptr;

  lv_obj_t *bigCpuBar = nullptr;
  lv_obj_t *bigCpuVal = nullptr;
  lv_obj_t *bigRamBar = nullptr;
  lv_obj_t *bigRamVal = nullptr;
  lv_obj_t *bigTempBar = nullptr;
  lv_obj_t *bigTempVal = nullptr;
  lv_obj_t *bigRttVal = nullptr;
  lv_obj_t *bigRttSdVal = nullptr;
  lv_obj_t *bigLossVal = nullptr;

  lv_obj_t *espInfoSsid = nullptr;
  lv_obj_t *espInfoIp = nullptr;
  lv_obj_t *espInfoMac = nullptr;
  lv_obj_t *espInfoRssi = nullptr;
  lv_obj_t *espInfoGw = nullptr;
  lv_obj_t *espInfoSubnet = nullptr;
  lv_obj_t *espInfoChip = nullptr;
  lv_obj_t *espInfoCpu = nullptr;
  lv_obj_t *espInfoHeap = nullptr;
  lv_obj_t *espInfoFlash = nullptr;
  lv_obj_t *espInfoUptime = nullptr;
};

inline lv_obj_t *lvglCreateValueLabel(lv_obj_t *parent, const char *text, int x, int y, lv_color_t color) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_color(obj, color, 0);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
  lv_obj_align(obj, LV_ALIGN_TOP_LEFT, x, y);
  return obj;
}

void createScreenDashboard(lv_obj_t *page, LvglScreenRefs &refs, int trafficPoints);
void createScreenGraph(lv_obj_t *page, LvglScreenRefs &refs, int trafficPoints);
void createScreenMetrics(lv_obj_t *page, LvglScreenRefs &refs);
void createScreenEsp32Info(lv_obj_t *page, LvglScreenRefs &refs);
void createScreenAbout(lv_obj_t *page);
