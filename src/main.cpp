// Main application entrypoint.
// Initializes device config, display/dashboard, WiFi portal and runtime loop.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "boards/board_profile.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>

// Imports
#include "alerts.h"
#include "config_manager.h"
#include "config_portal.h"
#include "fritzbox_api.h"
#include "dashboard.h"
#include "utils.h"

// Global constants
const char *kPrefsNs = "fwstatus";
const char *kApName = "FritzBox-Status-AP";
const char *kApPassword = "FWStatus2026";
const uint32_t kPollMs = 4500;
const uint32_t kWifiRetryMs = 10000;

#ifndef DASHBOARD_WIDTH
#define DASHBOARD_WIDTH 320
#endif

#ifndef DASHBOARD_HEIGHT
#define DASHBOARD_HEIGHT 170
#endif

#ifndef DASHBOARD_ROTATION
#define DASHBOARD_ROTATION 1
#endif

// Global objects
Preferences prefs;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite *sprite = nullptr;  // Initialized in setup() after tft.init()

// WiFi & Config
char fritzBoxHost[64] = "192.168.178.1";
bool shouldSaveConfig = false;
WiFiManager wm;
WiFiManagerParameter *fritzboxIpParam = nullptr;
WiFiManagerParameter *menuPasswordParam = nullptr;

// Telegram alerting
char telegramBotToken[64] = "";
char telegramChatId[32] = "";
int alertLossThresholdPct = 5;
int alertTempThresholdPct = 85;
int telegramDigestIntervalMin = 60;
bool telegramDndEnabled = false;
int telegramDndStartHour = 23;
int telegramDndEndHour = 7;

// WAN Status
String wanName = "WAN";
String wanStatus = "INIT";
String wanDelay = "-";
String wanRttSd = "-";
String wanLoss = "-";
int kTrafficPoints = 46;
float wanRxHistory[46] = {0};
float wanTxHistory[46] = {0};
bool wanTrafficPrimed = false;
uint64_t wanPrevInBytes = 0;
uint64_t wanPrevOutBytes = 0;
uint32_t wanPrevSampleMs = 0;

// System Metrics
int cpuPercent = 0;
int memPercent = 0;
int tempPercent = 0;
String tempValue = "-";
String lastUpdate = "00:00:00";

// Polling
uint32_t lastPoll = 0;
// Shared brightness presets used by dashboard button cycling and boot init.
const uint8_t kBrightnessLevels[] = {40, 96, 160, 255};
const uint8_t kBrightnessLevelCount = sizeof(kBrightnessLevels) / sizeof(kBrightnessLevels[0]);
uint8_t brightnessLevelIndex = kBrightnessLevelCount - 1;

// Dashboard uses this to show a small notification badge near the WiFi icon.
bool firmwareUpdateAvailable = false;

void applyBacklightLevel() {
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3) && defined(TFT_BL)
  analogWrite(TFT_BL, kBrightnessLevels[brightnessLevelIndex]);
#elif defined(TFT_BL)
#ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#else
  digitalWrite(TFT_BL, HIGH);
#endif
#endif
}

String formatUptime(uint32_t uptimeSeconds) {
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



void enableBacklight() {
#ifdef TFT_BL
#if defined(BOARD_PROFILE_LILYGO_T_DISPLAY_S3)
#if defined(LCD_POWER_ON)
  pinMode(LCD_POWER_ON, OUTPUT);
  digitalWrite(LCD_POWER_ON, HIGH);
#endif
  pinMode(TFT_BL, OUTPUT);
  applyBacklightLevel();
#else
  pinMode(TFT_BL, OUTPUT);
#ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#else
  digitalWrite(TFT_BL, HIGH);
#endif
#endif
#endif
}



void setup() {
  delay(200);

  // Load persisted configuration first so globals and portal state are in sync.
  ConfigManager& cfg = ConfigManager::getInstance();
  cfg.loadConfig();
  
  // Update global variables
  const DeviceConfig& config = cfg.getConfig();
  strlcpy(fritzBoxHost, config.fritzbox_host, sizeof(fritzBoxHost));
  strlcpy(telegramBotToken, config.telegram_bot_token, sizeof(telegramBotToken));
  strlcpy(telegramChatId, config.telegram_chat_id, sizeof(telegramChatId));
  alertLossThresholdPct = config.alert_loss_threshold_pct;
  alertTempThresholdPct = config.alert_temp_threshold_pct;
  telegramDigestIntervalMin = config.telegram_digest_interval_min;
  telegramDndEnabled = config.telegram_dnd_enabled;
  telegramDndStartHour = config.telegram_dnd_start_hour;
  telegramDndEndHour = config.telegram_dnd_end_hour;

  // Restore the traffic graph's last known state so it doesn't start empty
  // after every reboot/OTA update.
  loadTrafficHistory();
  // Same for the Telegram daily summary's running min/max stats.
  loadDailyStats();

  enableBacklight();

  tft.init();
  tft.setRotation(DASHBOARD_ROTATION);
  // Dashboard initialization also creates the API polling FreeRTOS task.
  initDashboard();
  
  // Start networking and portal stack.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  
  // WiFiManager configures STA/AP mode and portal routes depending on saved config.
  configureWiFi();
  if (WiFi.status() == WL_CONNECTED && isConfigured()) {
    // Keep local menu available even after normal STA reconnect.
    wm.startWebPortal();
  }
  setupPortalRoutes();
}

void loop() {
  // UI and data polling are driven by the dashboard module.
  loopDashboard();
}
