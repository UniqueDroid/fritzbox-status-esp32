#pragma once

// Cross-module runtime state shared by dashboard, portal and API modules.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "boards/board_profile.h"
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>

// Global objects
extern Preferences prefs;
extern TFT_eSPI tft;
extern TFT_eSprite *sprite;  // Pointer to allow lazy initialization
extern WiFiManager wm;
extern WiFiManagerParameter *fritzboxIpParam;
extern WiFiManagerParameter *menuPasswordParam;

// Configuration
extern char fritzBoxHost[64];
extern bool shouldSaveConfig;

// Telegram alerting
extern char telegramBotToken[64];
extern char telegramChatId[32];
extern int alertLossThresholdPct;
extern int alertTempThresholdPct;
extern int telegramDigestIntervalMin;
extern bool telegramDndEnabled;
extern int telegramDndStartHour;
extern int telegramDndEndHour;

// WAN Status
extern String wanName;
extern String wanStatus;
extern String wanDelay;
extern String wanRttSd;
extern String wanLoss;

// Traffic
extern int kTrafficPoints;
extern float wanRxHistory[];
extern float wanTxHistory[];
extern bool wanTrafficPrimed;
extern uint64_t wanPrevInBytes;
extern uint64_t wanPrevOutBytes;
extern uint32_t wanPrevSampleMs;

// System metrics
extern int cpuPercent;
extern int memPercent;
extern int tempPercent;
extern String tempValue;
extern String lastUpdate;

// Polling
extern uint32_t lastPoll;
extern const uint32_t kPollMs;
extern const char *kApName;
extern const char *kApPassword;

// Firmware update notification state
extern bool firmwareUpdateAvailable;
