// Telegram-based alerting for WAN down/up and threshold breaches.
#include "alerts.h"
#include "config_manager.h"
#include "firmware_update.h"
#include "firmware_version.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <lvgl.h>
#include <time.h>

namespace {
bool sHasWanState = false;
bool sWanWasOnline = false;
bool sLossAlertActive = false;
bool sTempAlertActive = false;
uint32_t sLastDigestMs = 0;

int sDailyMinCpu = 101, sDailyMaxCpu = -1;
int sDailyMinTemp = 101, sDailyMaxTemp = -1;
float sDailyMinLoss = 1.0e9f, sDailyMaxLoss = -1.0f;
int sDailyWanDownCount = 0;
int sLastSummaryYday = -1;
constexpr int kDailySummaryHour = 8;

constexpr const char *kDailyStatsNs = "dailystats";
uint32_t sLastDailyStatsSaveMs = 0;
constexpr uint32_t kDailyStatsSaveMs = 60000;

// Writes the running daily min/max stats to NVS immediately (no throttle) -
// used right after a reset so a reboot moments later can't reload stale
// pre-reset values. Routine periodic saves go through the throttled
// wrapper below instead, to limit flash wear.
void writeDailyStatsToNvs() {
  Preferences p;
  if (!p.begin(kDailyStatsNs, false)) {
    return;
  }
  p.putInt("min_cpu", sDailyMinCpu);
  p.putInt("max_cpu", sDailyMaxCpu);
  p.putInt("min_temp", sDailyMinTemp);
  p.putInt("max_temp", sDailyMaxTemp);
  p.putFloat("min_loss", sDailyMinLoss);
  p.putFloat("max_loss", sDailyMaxLoss);
  p.putInt("down_count", sDailyWanDownCount);
  p.putInt("last_yday", sLastSummaryYday);
  p.end();
}

void saveDailyStatsThrottled() {
  uint32_t now = millis();
  if (sLastDailyStatsSaveMs != 0 && now - sLastDailyStatsSaveMs < kDailyStatsSaveMs) {
    return;
  }
  sLastDailyStatsSaveMs = now;
  writeDailyStatsToNvs();
}

float parsePercent(const String &raw) {
  String s = raw;
  s.replace("%", "");
  s.trim();
  if (s.length() == 0 || s == "-") {
    return -1.0f;
  }
  return s.toFloat();
}

// Returns the current local hour (0-23), or -1 if NTP hasn't synced yet.
int currentLocalHourOrUnknown() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year + 1900 < 2020) {
    return -1;
  }
  return timeinfo.tm_hour;
}

bool isWithinDnd() {
  if (!telegramDndEnabled) {
    return false;
  }
  int hour = currentLocalHourOrUnknown();
  if (hour < 0) {
    return false;
  }
  int start = telegramDndStartHour;
  int end = telegramDndEndHour;
  if (start == end) {
    return false;
  }
  if (start < end) {
    return hour >= start && hour < end;
  }
  // Window wraps past midnight, e.g. 23 -> 7.
  return hour >= start || hour < end;
}
}  // namespace

void ensureTimeSynced() {
  // Europe/Berlin POSIX TZ rule - handles CET/CEST DST switches automatically.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
}

namespace {
String formatUptime() {
  uint32_t totalSeconds = millis() / 1000;
  uint32_t days = totalSeconds / 86400;
  uint32_t hours = (totalSeconds % 86400) / 3600;
  uint32_t minutes = (totalSeconds % 3600) / 60;
  String s;
  if (days > 0) {
    s += String(days) + "d ";
  }
  s += String(hours) + "h " + String(minutes) + "m";
  return s;
}

String buildWanDetailsMessage() {
  bool wanOnline = wanStatus.equalsIgnoreCase("online") || wanStatus.equalsIgnoreCase("up");
  bool wanDown = wanStatus.equalsIgnoreCase("down") || wanStatus.equalsIgnoreCase("offline");
  String wanIcon = wanOnline ? u8"🟢" : (wanDown ? u8"🔴" : u8"❔");

  String msg = String(u8"🌐 WAN Details\n");
  msg += "Name: " + wanName + "\n";
  msg += wanIcon + " Status: " + wanStatus + "\n";
  msg += "RTT: " + wanDelay + "\n";
  msg += "Jitter: " + wanRttSd + "\n";
  msg += "Loss: " + wanLoss;
  return msg;
}

String buildHelpMessage() {
  String msg = String(u8"🤖 FRITZ!Box Status Bot\n\n");
  msg += "/status - current WAN/CPU/RAM/temp summary\n";
  msg += "/wan - WAN details (RTT, jitter, loss)\n";
  msg += "/uptime - device uptime\n";
  msg += "/setloss <percent> - change packet-loss alert threshold\n";
  msg += "/settemp <percent> - change temperature alert threshold\n";
  msg += "/snapshot - photo of the current display\n";
  msg += "/update - check for a firmware update\n";
  msg += "/help - this list";
  return msg;
}

// "/update" checks and reports; "/update confirm" actually downloads,
// verifies and flashes the release, then reboots on success. Runs
// synchronously in the command-poll task, same tradeoff the existing
// web-menu install flow already accepts (blocks other work for the
// duration of the download+flash).
void handleUpdateCommand(bool confirm) {
  FirmwareReleaseInfo info;
  String errorMessage;
  if (!fetchLatestFirmwareRelease(info, errorMessage)) {
    sendTelegramMessage(String(u8"❌ Could not check for updates: ") + (errorMessage.length() ? errorMessage : String("unknown error")));
    return;
  }

  if (!info.updateAvailable) {
    sendTelegramMessage(String(u8"✅ Already on the latest version (") + kFirmwareVersion + ").");
    return;
  }

  if (!confirm) {
    sendTelegramMessage(String(u8"⬆️ Update available: ") + info.latestVersion + " (current: " + kFirmwareVersion + ").\nSend \"/update confirm\" to download and install now. The device will reboot automatically when done.");
    return;
  }

  sendTelegramMessage(String(u8"⬇️ Installing ") + info.latestVersion + "... device will reboot when finished.");

  if (!flashFirmwareAsset(info, errorMessage)) {
    sendTelegramMessage(String(u8"❌ Update failed: ") + (errorMessage.length() ? errorMessage : String("unknown error")) + ". Still running " + kFirmwareVersion + ".");
    return;
  }

  delay(300);
  ESP.restart();
}

// Parses "/cmd <int>"-style messages. Returns false if no valid int follows.
bool parseCommandInt(const String &text, const String &prefix, int &outValue) {
  if (!text.startsWith(prefix)) {
    return false;
  }
  String rest = text.substring(prefix.length());
  rest.trim();
  if (rest.length() == 0) {
    return false;
  }
  for (size_t i = 0; i < rest.length(); ++i) {
    if (!isDigit(rest[i])) {
      return false;
    }
  }
  outValue = rest.toInt();
  return true;
}
}  // namespace

void sendBootNotification() {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }
  sendTelegramMessage(String(u8"🔌 FRITZ!Box Status device booted (firmware ") + kFirmwareVersion + ").");
}

namespace {
bool sFirmwareUpdateNotified = false;
}  // namespace

void checkAndNotifyFirmwareUpdate(bool updateAvailable, const String &latestVersion) {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }
  if (updateAvailable && !sFirmwareUpdateNotified) {
    sendTelegramMessage(String(u8"⬆️ Firmware update available: ") + latestVersion + " (current: " + kFirmwareVersion + "). Install via the web menu.");
  }
  sFirmwareUpdateNotified = updateAvailable;
}

bool sendTelegramMessage(const String &message) {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(6);

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + telegramBotToken + "/sendMessage";
  if (!http.begin(client, url)) {
    return false;
  }
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["chat_id"] = telegramChatId;
  doc["text"] = message;
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  http.end();
  return code == HTTP_CODE_OK;
}

void checkAndSendAlerts() {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }

  // WAN up/down, edge-triggered so it fires once per transition.
  bool wanOnline = wanStatus.equalsIgnoreCase("online") || wanStatus.equalsIgnoreCase("up");
  bool wanDown = wanStatus.equalsIgnoreCase("down") || wanStatus.equalsIgnoreCase("offline");
  if (wanOnline || wanDown) {
    if (!sHasWanState || wanOnline != sWanWasOnline) {
      if (wanDown) {
        sendTelegramMessage(String(u8"🔴 FRITZ!Box WAN (") + wanName + ") is DOWN.");
        sDailyWanDownCount++;
      } else if (sHasWanState) {
        // Skip announcing "online" on the very first reading after boot -
        // that's the normal startup state, not a recovery.
        sendTelegramMessage(String(u8"🟢 FRITZ!Box WAN (") + wanName + ") is back ONLINE.");
      }
      sWanWasOnline = wanOnline;
      sHasWanState = true;
    }
  }

  // Packet loss threshold, edge-triggered.
  float lossPct = parsePercent(wanLoss);
  if (lossPct >= 0.0f) {
    bool over = lossPct >= (float)alertLossThresholdPct;
    if (over && !sLossAlertActive) {
      sendTelegramMessage(String(u8"⚠️ FRITZ!Box WAN packet loss high: ") + String(lossPct, 1) + "% (threshold " + alertLossThresholdPct + "%).");
    } else if (!over && sLossAlertActive) {
      sendTelegramMessage(String(u8"✅ FRITZ!Box WAN packet loss back to normal: ") + String(lossPct, 1) + "%.");
    }
    sLossAlertActive = over;
  }

  // Temperature threshold (percent of sensor range, same metric as the UI bar).
  if (tempPercent > 0) {
    bool over = tempPercent >= alertTempThresholdPct;
    if (over && !sTempAlertActive) {
      sendTelegramMessage(String(u8"🌡️ FRITZ!Box system temperature high: ") + tempValue + ".");
    } else if (!over && sTempAlertActive) {
      sendTelegramMessage(String(u8"✅ FRITZ!Box system temperature back to normal: ") + tempValue + ".");
    }
    sTempAlertActive = over;
  }
}

namespace {
String buildStatusDigestMessage() {
  bool wanOnline = wanStatus.equalsIgnoreCase("online") || wanStatus.equalsIgnoreCase("up");
  bool wanDown = wanStatus.equalsIgnoreCase("down") || wanStatus.equalsIgnoreCase("offline");
  String wanIcon = wanOnline ? u8"🟢" : (wanDown ? u8"🔴" : u8"❔");
  String wanState = wanOnline ? "online" : (wanDown ? "offline" : wanStatus);

  String msg = String(u8"🖥️ FRITZ!Box Status\n");
  msg += wanIcon + " WAN: " + wanState + "\n";
  msg += String(u8"🧠 CPU: ") + cpuPercent + "%\n";
  msg += String(u8"📊 RAM: ") + memPercent + "%\n";
  msg += String(u8"🌡️ Temp: ") + tempValue;
  return msg;
}
}  // namespace

bool sendStatusDigestNow() {
  sLastDigestMs = millis();
  return sendTelegramMessage(buildStatusDigestMessage());
}

void checkAndSendStatusDigest() {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }

  uint32_t now = millis();
  uint32_t intervalMs = (uint32_t)telegramDigestIntervalMin * 60UL * 1000UL;
  if (sLastDigestMs != 0 && (now - sLastDigestMs) < intervalMs) {
    return;
  }
  if (isWithinDnd()) {
    // Quiet hours: skip silently and push the next check out by a full
    // interval instead of spamming a catch-up digest once DND ends.
    sLastDigestMs = now;
    return;
  }
  sendStatusDigestNow();
}

namespace {
// Telegram update IDs and chat IDs routinely exceed INT32_MAX (e.g. user
// chat IDs like 8694868858), which silently truncates in a 32-bit `long`
// on ESP32 - must use 64-bit here or the chat-ID auth check always fails.
int64_t sLastUpdateId = -1;
bool sCommandOffsetInitialized = false;
uint32_t sLastCommandPollMs = 0;
constexpr uint32_t kCommandPollIntervalMs = 15000;
}  // namespace

void checkTelegramCommands() {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }
  uint32_t now = millis();
  if (sLastCommandPollMs != 0 && (now - sLastCommandPollMs) < kCommandPollIntervalMs) {
    return;
  }
  sLastCommandPollMs = now;
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(10);

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + telegramBotToken + "/getUpdates?timeout=0&limit=20";
  if (sCommandOffsetInitialized) {
    url += "&offset=" + String((long long)(sLastUpdateId + 1));
  }
  if (!http.begin(client, url)) {
    return;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return;
  }

  int64_t configuredChatId = strtoll(telegramChatId, nullptr, 10);

  JsonArray results = doc["result"].as<JsonArray>();
  for (JsonObject upd : results) {
    int64_t updateId = upd["update_id"] | (int64_t)-1;
    if (updateId > sLastUpdateId) {
      sLastUpdateId = updateId;
    }

    // Discard any backlog collected before this boot instead of acting on it.
    if (!sCommandOffsetInitialized) {
      continue;
    }

    JsonObject message = upd["message"];
    if (message.isNull()) {
      continue;
    }
    int64_t msgChatId = message["chat"]["id"] | (int64_t)0;
    if (msgChatId != configuredChatId) {
      continue;
    }
    String text = message["text"] | "";
    text.trim();
    if (text.equalsIgnoreCase("/status")) {
      sendStatusDigestNow();
    } else if (text.equalsIgnoreCase("/wan")) {
      sendTelegramMessage(buildWanDetailsMessage());
    } else if (text.equalsIgnoreCase("/uptime")) {
      sendTelegramMessage(String(u8"⏱️ Uptime: ") + formatUptime());
    } else if (text.equalsIgnoreCase("/help") || text.equalsIgnoreCase("/start")) {
      sendTelegramMessage(buildHelpMessage());
    } else if (text.equalsIgnoreCase("/snapshot")) {
      requestDisplaySnapshot();
    } else if (text.equalsIgnoreCase("/update")) {
      handleUpdateCommand(false);
    } else if (text.equalsIgnoreCase("/update confirm")) {
      handleUpdateCommand(true);
    } else {
      int value = 0;
      if (parseCommandInt(text, "/setloss", value)) {
        ConfigManager& cfg = ConfigManager::getInstance();
        cfg.setAlertLossThresholdPct(value);
        cfg.saveConfig();
        sendTelegramMessage(String(u8"✅ Packet loss alert threshold set to ") + alertLossThresholdPct + "%.");
      } else if (parseCommandInt(text, "/settemp", value)) {
        ConfigManager& cfg = ConfigManager::getInstance();
        cfg.setAlertTempThresholdPct(value);
        cfg.saveConfig();
        sendTelegramMessage(String(u8"✅ Temperature alert threshold set to ") + alertTempThresholdPct + "%.");
      }
    }
  }
  sCommandOffsetInitialized = true;
}

void updateDailyStats() {
  if (cpuPercent < sDailyMinCpu) sDailyMinCpu = cpuPercent;
  if (cpuPercent > sDailyMaxCpu) sDailyMaxCpu = cpuPercent;

  if (tempPercent > 0) {
    if (tempPercent < sDailyMinTemp) sDailyMinTemp = tempPercent;
    if (tempPercent > sDailyMaxTemp) sDailyMaxTemp = tempPercent;
  }

  float lossPct = parsePercent(wanLoss);
  if (lossPct >= 0.0f) {
    if (lossPct < sDailyMinLoss) sDailyMinLoss = lossPct;
    if (lossPct > sDailyMaxLoss) sDailyMaxLoss = lossPct;
  }

  saveDailyStatsThrottled();
}

void checkAndSendDailySummary() {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year + 1900 < 2020) {
    return;  // NTP not synced yet
  }
  if (timeinfo.tm_hour < kDailySummaryHour || timeinfo.tm_yday == sLastSummaryYday) {
    return;
  }
  if (isWithinDnd()) {
    // Retry later the same day once quiet hours end - don't reset stats yet.
    return;
  }

  String msg = String(u8"📅 Daily Summary\n");
  if (sDailyMaxCpu >= 0) {
    msg += String(u8"🧠 CPU: ") + sDailyMinCpu + "-" + sDailyMaxCpu + "%\n";
  }
  if (sDailyMaxTemp >= 0) {
    msg += String(u8"🌡️ Temp: ") + sDailyMinTemp + "-" + sDailyMaxTemp + "%\n";
  }
  if (sDailyMaxLoss >= 0.0f) {
    msg += String(u8"📉 Loss: ") + String(sDailyMinLoss, 1) + "-" + String(sDailyMaxLoss, 1) + "%\n";
  }
  msg += String(u8"🔴 WAN down events: ") + sDailyWanDownCount;
  sendTelegramMessage(msg);

  sLastSummaryYday = timeinfo.tm_yday;
  sDailyMinCpu = 101;
  sDailyMaxCpu = -1;
  sDailyMinTemp = 101;
  sDailyMaxTemp = -1;
  sDailyMinLoss = 1.0e9f;
  sDailyMaxLoss = -1.0f;
  sDailyWanDownCount = 0;
  writeDailyStatsToNvs();
}

void loadDailyStats() {
  Preferences p;
  if (!p.begin(kDailyStatsNs, true)) {
    return;
  }
  if (p.isKey("min_cpu")) sDailyMinCpu = p.getInt("min_cpu", sDailyMinCpu);
  if (p.isKey("max_cpu")) sDailyMaxCpu = p.getInt("max_cpu", sDailyMaxCpu);
  if (p.isKey("min_temp")) sDailyMinTemp = p.getInt("min_temp", sDailyMinTemp);
  if (p.isKey("max_temp")) sDailyMaxTemp = p.getInt("max_temp", sDailyMaxTemp);
  if (p.isKey("min_loss")) sDailyMinLoss = p.getFloat("min_loss", sDailyMinLoss);
  if (p.isKey("max_loss")) sDailyMaxLoss = p.getFloat("max_loss", sDailyMaxLoss);
  if (p.isKey("down_count")) sDailyWanDownCount = p.getInt("down_count", sDailyWanDownCount);
  if (p.isKey("last_yday")) sLastSummaryYday = p.getInt("last_yday", sLastSummaryYday);
  p.end();
}

void clearDailyStats() {
  sDailyMinCpu = 101;
  sDailyMaxCpu = -1;
  sDailyMinTemp = 101;
  sDailyMaxTemp = -1;
  sDailyMinLoss = 1.0e9f;
  sDailyMaxLoss = -1.0f;
  sDailyWanDownCount = 0;
  sLastSummaryYday = -1;

  Preferences p;
  if (!p.begin(kDailyStatsNs, false)) {
    return;
  }
  p.clear();
  p.end();
}

namespace {
volatile bool sSnapshotRequested = false;

struct SnapshotBuffer {
  void *pixelBuf;
  lv_img_dsc_t dsc;
};

// Streams a captured screen (RGB565 snapshot buffer) as a 24-bit BMP
// directly into the upload connection - only the single snapshot buffer
// (one frame) is ever held in RAM; the BMP itself is generated row-by-row
// on the wire instead of being assembled in a second buffer.
// Runs on its own task (see snapshotUploadTask) because the TLS handshake
// needs more stack than the main Arduino loop task has - doing this inline
// in loopDashboard() previously crashed the device with a stack overflow.
bool streamSnapshotBmp(const lv_img_dsc_t *dsc) {
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  if (!dsc || !dsc->data) {
    return false;
  }

  int w = dsc->header.w;
  int h = dsc->header.h;
  const uint16_t *pixels = reinterpret_cast<const uint16_t *>(dsc->data);

  int rowSize = w * 3;
  int rowPadding = (4 - (rowSize % 4)) % 4;
  int paddedRowSize = rowSize + rowPadding;
  uint32_t pixelDataSize = (uint32_t)paddedRowSize * (uint32_t)h;
  uint32_t bmpFileSize = 54 + pixelDataSize;

  uint8_t *rowBuf = static_cast<uint8_t *>(malloc(paddedRowSize));
  if (!rowBuf) {
    return false;
  }
  memset(rowBuf + rowSize, 0, rowPadding);

  uint8_t bmpHeader[54] = {0};
  bmpHeader[0] = 'B';
  bmpHeader[1] = 'M';
  memcpy(&bmpHeader[2], &bmpFileSize, 4);
  uint32_t pixelOffset = 54;
  memcpy(&bmpHeader[10], &pixelOffset, 4);
  uint32_t dibHeaderSize = 40;
  memcpy(&bmpHeader[14], &dibHeaderSize, 4);
  int32_t wSigned = w;
  int32_t hSigned = h;  // positive height = bottom-up row order
  memcpy(&bmpHeader[18], &wSigned, 4);
  memcpy(&bmpHeader[22], &hSigned, 4);
  uint16_t planes = 1;
  memcpy(&bmpHeader[26], &planes, 2);
  uint16_t bpp = 24;
  memcpy(&bmpHeader[28], &bpp, 2);
  memcpy(&bmpHeader[34], &pixelDataSize, 4);

  String boundary = "fritzboxSnapBoundary";
  String preamble;
  preamble += "--" + boundary + "\r\n";
  preamble += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  preamble += String(telegramChatId) + "\r\n";
  preamble += "--" + boundary + "\r\n";
  preamble += "Content-Disposition: form-data; name=\"photo\"; filename=\"snapshot.bmp\"\r\n";
  preamble += "Content-Type: image/bmp\r\n\r\n";
  String closing = "\r\n--" + boundary + "--\r\n";

  uint32_t contentLength = preamble.length() + bmpFileSize + closing.length();

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(15);
  if (!client.connect("api.telegram.org", 443)) {
    free(rowBuf);
    return false;
  }

  client.print(String("POST /bot") + telegramBotToken + "/sendPhoto HTTP/1.1\r\n");
  client.print("Host: api.telegram.org\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(contentLength) + "\r\n");
  client.print("Connection: close\r\n\r\n");

  client.print(preamble);
  client.write(bmpHeader, sizeof(bmpHeader));

  // BMP rows are stored bottom-up; BMP pixel order is B,G,R.
  for (int y = h - 1; y >= 0; --y) {
    const uint16_t *srcRow = pixels + (size_t)y * w;
    for (int x = 0; x < w; ++x) {
      uint16_t p = srcRow[x];
      uint8_t r5 = (p >> 11) & 0x1F;
      uint8_t g6 = (p >> 5) & 0x3F;
      uint8_t b5 = p & 0x1F;
      rowBuf[x * 3 + 0] = (b5 << 3) | (b5 >> 2);
      rowBuf[x * 3 + 1] = (g6 << 2) | (g6 >> 4);
      rowBuf[x * 3 + 2] = (r5 << 3) | (r5 >> 2);
    }
    client.write(rowBuf, paddedRowSize);
  }

  client.print(closing);

  free(rowBuf);

  uint32_t waitStart = millis();
  while (client.connected() && !client.available() && (millis() - waitStart) < 8000) {
    delay(20);
  }
  String response = client.readString();
  client.stop();

  return response.indexOf("\"ok\":true") >= 0;
}

void snapshotUploadTask(void *param) {
  SnapshotBuffer *sb = static_cast<SnapshotBuffer *>(param);
  streamSnapshotBmp(&sb->dsc);
  free(sb->pixelBuf);
  delete sb;
  vTaskDelete(nullptr);
}
}  // namespace

void requestDisplaySnapshot() {
  sSnapshotRequested = true;
}

void processPendingSnapshotRequest() {
  if (!sSnapshotRequested) {
    return;
  }
  sSnapshotRequested = false;
  if (strlen(telegramBotToken) == 0 || strlen(telegramChatId) == 0) {
    return;
  }

  // lv_snapshot_take_to_buf() must run on the LVGL/main-loop thread; the
  // upload (TLS handshake) needs a task with a bigger stack than that
  // thread has, so hand the captured buffer off to a dedicated one-shot
  // task. Deliberately NOT using lv_snapshot_take(): it allocates the
  // pixel buffer from LVGL's own internal memory pool (LV_MEM_SIZE,
  // default 48KB), which is far too small for a full-screen ~100KB+
  // RGB565 buffer - the failed allocation hits LV_ASSERT_HANDLER, which
  // defaults to `while(1);` and hangs the device with no crash log.
  // Supplying our own malloc()'d buffer (regular heap, ~226KB free)
  // sidesteps LVGL's pool entirely for the big allocation.
  lv_obj_t *scr = lv_scr_act();
  uint32_t bufSize = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
  if (bufSize == 0) {
    return;
  }
  void *pixelBuf = malloc(bufSize);
  if (!pixelBuf) {
    return;
  }

  SnapshotBuffer *sb = new SnapshotBuffer();
  sb->pixelBuf = pixelBuf;
  if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &sb->dsc, pixelBuf, bufSize) != LV_RES_OK) {
    free(pixelBuf);
    delete sb;
    return;
  }

  BaseType_t taskOk = xTaskCreatePinnedToCore(snapshotUploadTask, "tgSnapUp", 8192, sb, 1, nullptr, 0);
  if (taskOk != pdPASS) {
    free(pixelBuf);
    delete sb;
  }
}
