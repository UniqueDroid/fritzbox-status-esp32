// Persistent configuration manager backed by ESP32 Preferences (NVS).
#include "config_manager.h"
#include "globals.h"
#include "utils.h"
#include <Preferences.h>

ConfigManager& ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

namespace {
constexpr int kDefaultAlertLossThresholdPct = 5;
constexpr int kDefaultAlertTempThresholdPct = 85;
constexpr int kDefaultDigestIntervalMin = 60;
constexpr int kDefaultDndStartHour = 23;
constexpr int kDefaultDndEndHour = 7;
}  // namespace

ConfigManager::ConfigManager() {
  memset(&config_, 0, sizeof(config_));
  strcpy(config_.fritzbox_host, "192.168.178.1");
  strcpy(config_.web_menu_password, "fwmenu123");
  config_.alert_loss_threshold_pct = kDefaultAlertLossThresholdPct;
  config_.alert_temp_threshold_pct = kDefaultAlertTempThresholdPct;
  config_.telegram_digest_interval_min = kDefaultDigestIntervalMin;
  config_.telegram_dnd_enabled = false;
  config_.telegram_dnd_start_hour = kDefaultDndStartHour;
  config_.telegram_dnd_end_hour = kDefaultDndEndHour;
  has_required_data_ = false;
}

ConfigManager::~ConfigManager() {}

bool ConfigManager::loadConfig() {
  Preferences prefs;
  prefs.begin("fwstatus", true);  // Read-only mode
  
  String wlan_ssid = prefs.getString("wlan_ssid", "");
  String wlan_password = prefs.getString("wlan_password", "");
  String fritzbox_host = prefs.getString("fritzbox_host", "");
  String api_key = prefs.getString("api_key", "");
  String web_menu_password = prefs.getString("web_menu_password", "fwmenu123");
  String telegram_bot_token = prefs.getString("tg_bot_token", "");
  String telegram_chat_id = prefs.getString("tg_chat_id", "");
  int alert_loss_threshold_pct = prefs.getInt("alert_loss_pct", kDefaultAlertLossThresholdPct);
  int alert_temp_threshold_pct = prefs.getInt("alert_temp_pct", kDefaultAlertTempThresholdPct);
  int digest_interval_min = prefs.getInt("tg_digest_min", kDefaultDigestIntervalMin);
  bool dnd_enabled = prefs.getBool("tg_dnd_on", false);
  int dnd_start_hour = prefs.getInt("tg_dnd_start", kDefaultDndStartHour);
  int dnd_end_hour = prefs.getInt("tg_dnd_end", kDefaultDndEndHour);

  prefs.end();
  
  strlcpy(config_.wlan_ssid, wlan_ssid.c_str(), sizeof(config_.wlan_ssid));
  strlcpy(config_.wlan_password, wlan_password.c_str(), sizeof(config_.wlan_password));
  
  // Sanitize host
  fritzbox_host.trim();
  if (fritzbox_host.startsWith("http://")) {
    fritzbox_host.remove(0, 7);
  } else if (fritzbox_host.startsWith("https://")) {
    fritzbox_host.remove(0, 8);
  }
  int slash = fritzbox_host.indexOf('/');
  if (slash >= 0) {
    fritzbox_host = fritzbox_host.substring(0, slash);
  }
  fritzbox_host.trim();
  api_key.trim();

  // Decide configured state from raw persisted values, not from fallback defaults.
  // TR-064 mode requires only a host; API key is legacy and optional.
  has_required_data_ = (fritzbox_host.length() > 0);
  
  if (fritzbox_host.length() == 0) {
    strlcpy(config_.fritzbox_host, "192.168.178.1", sizeof(config_.fritzbox_host));
  } else {
    strlcpy(config_.fritzbox_host, fritzbox_host.c_str(), sizeof(config_.fritzbox_host));
  }
  strlcpy(config_.api_key, api_key.c_str(), sizeof(config_.api_key));
  strlcpy(config_.web_menu_password, web_menu_password.c_str(), sizeof(config_.web_menu_password));
  strlcpy(config_.telegram_bot_token, telegram_bot_token.c_str(), sizeof(config_.telegram_bot_token));
  strlcpy(config_.telegram_chat_id, telegram_chat_id.c_str(), sizeof(config_.telegram_chat_id));
  config_.alert_loss_threshold_pct = alert_loss_threshold_pct;
  config_.alert_temp_threshold_pct = alert_temp_threshold_pct;
  config_.telegram_digest_interval_min = digest_interval_min;
  config_.telegram_dnd_enabled = dnd_enabled;
  config_.telegram_dnd_start_hour = dnd_start_hour;
  config_.telegram_dnd_end_hour = dnd_end_hour;

  return isConfigured();
}

bool ConfigManager::saveConfig() {
  Preferences prefs;
  prefs.begin("fwstatus", false);  // Write mode
  
  prefs.putString("wlan_ssid", config_.wlan_ssid);
  prefs.putString("wlan_password", config_.wlan_password);
  prefs.putString("fritzbox_host", config_.fritzbox_host);
  prefs.putString("api_key", config_.api_key);
  prefs.putString("web_menu_password", config_.web_menu_password);
  prefs.putString("tg_bot_token", config_.telegram_bot_token);
  prefs.putString("tg_chat_id", config_.telegram_chat_id);
  prefs.putInt("alert_loss_pct", config_.alert_loss_threshold_pct);
  prefs.putInt("alert_temp_pct", config_.alert_temp_threshold_pct);
  prefs.putInt("tg_digest_min", config_.telegram_digest_interval_min);
  prefs.putBool("tg_dnd_on", config_.telegram_dnd_enabled);
  prefs.putInt("tg_dnd_start", config_.telegram_dnd_start_hour);
  prefs.putInt("tg_dnd_end", config_.telegram_dnd_end_hour);

  prefs.end();

  // Update globals
  strlcpy(fritzBoxHost, config_.fritzbox_host, sizeof(fritzBoxHost));
  strlcpy(telegramBotToken, config_.telegram_bot_token, sizeof(telegramBotToken));
  strlcpy(telegramChatId, config_.telegram_chat_id, sizeof(telegramChatId));
  alertLossThresholdPct = config_.alert_loss_threshold_pct;
  alertTempThresholdPct = config_.alert_temp_threshold_pct;
  telegramDigestIntervalMin = config_.telegram_digest_interval_min;
  telegramDndEnabled = config_.telegram_dnd_enabled;
  telegramDndStartHour = config_.telegram_dnd_start_hour;
  telegramDndEndHour = config_.telegram_dnd_end_hour;

  String host(config_.fritzbox_host);
  host.trim();
  has_required_data_ = (host.length() > 0);
  
  return true;
}

bool ConfigManager::isConfigured() {
  return has_required_data_;
}

void ConfigManager::setWlanSsid(const char* ssid) {
  strlcpy(config_.wlan_ssid, ssid ? ssid : "", sizeof(config_.wlan_ssid));
}

void ConfigManager::setWlanPassword(const char* password) {
  strlcpy(config_.wlan_password, password ? password : "", sizeof(config_.wlan_password));
}

void ConfigManager::setFritzboxHost(const char* host) {
  String h(host ? host : "");
  h.trim();
  if (h.startsWith("http://")) {
    h.remove(0, 7);
  } else if (h.startsWith("https://")) {
    h.remove(0, 8);
  }
  int slash = h.indexOf('/');
  if (slash >= 0) {
    h = h.substring(0, slash);
  }
  h.trim();
  strlcpy(config_.fritzbox_host, h.c_str(), sizeof(config_.fritzbox_host));

  has_required_data_ = (h.length() > 0);
}

void ConfigManager::setApiKey(const char* key) {
  String k(key ? key : "");
  k.trim();
  strlcpy(config_.api_key, k.c_str(), sizeof(config_.api_key));
}

void ConfigManager::setWebMenuPassword(const char* password) {
  String p(password ? password : "");
  p.trim();
  if (p.length() < 8) {
    return;
  }
  strlcpy(config_.web_menu_password, p.c_str(), sizeof(config_.web_menu_password));
}

void ConfigManager::setTelegramBotToken(const char* token) {
  String t(token ? token : "");
  t.trim();
  strlcpy(config_.telegram_bot_token, t.c_str(), sizeof(config_.telegram_bot_token));
}

void ConfigManager::setTelegramChatId(const char* chatId) {
  String c(chatId ? chatId : "");
  c.trim();
  strlcpy(config_.telegram_chat_id, c.c_str(), sizeof(config_.telegram_chat_id));
}

void ConfigManager::setAlertLossThresholdPct(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  config_.alert_loss_threshold_pct = pct;
}

void ConfigManager::setAlertTempThresholdPct(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  config_.alert_temp_threshold_pct = pct;
}

void ConfigManager::setTelegramDigestIntervalMin(int minutes) {
  if (minutes < 1) minutes = 1;
  if (minutes > 1440) minutes = 1440;
  config_.telegram_digest_interval_min = minutes;
}

void ConfigManager::setTelegramDndEnabled(bool enabled) {
  config_.telegram_dnd_enabled = enabled;
}

void ConfigManager::setTelegramDndStartHour(int hour) {
  if (hour < 0) hour = 0;
  if (hour > 23) hour = 23;
  config_.telegram_dnd_start_hour = hour;
}

void ConfigManager::setTelegramDndEndHour(int hour) {
  if (hour < 0) hour = 0;
  if (hour > 23) hour = 23;
  config_.telegram_dnd_end_hour = hour;
}

void ConfigManager::clearConfig() {
  memset(&config_, 0, sizeof(config_));
  strcpy(config_.fritzbox_host, "192.168.178.1");
  strcpy(config_.web_menu_password, "fwmenu123");
  config_.alert_loss_threshold_pct = kDefaultAlertLossThresholdPct;
  config_.alert_temp_threshold_pct = kDefaultAlertTempThresholdPct;
  config_.telegram_digest_interval_min = kDefaultDigestIntervalMin;
  config_.telegram_dnd_enabled = false;
  config_.telegram_dnd_start_hour = kDefaultDndStartHour;
  config_.telegram_dnd_end_hour = kDefaultDndEndHour;
  has_required_data_ = false;

  Preferences prefs;
  prefs.begin("fwstatus", false);
  // Explicitly wipe keys used by boot decision logic.
  prefs.putString("wlan_ssid", "");
  prefs.putString("wlan_password", "");
  prefs.putString("fritzbox_host", "");
  prefs.putString("api_key", "");
  prefs.putString("web_menu_password", "fwmenu123");
  prefs.putString("tg_bot_token", "");
  prefs.putString("tg_chat_id", "");
  prefs.putInt("alert_loss_pct", kDefaultAlertLossThresholdPct);
  prefs.putInt("alert_temp_pct", kDefaultAlertTempThresholdPct);
  prefs.putInt("tg_digest_min", kDefaultDigestIntervalMin);
  prefs.putBool("tg_dnd_on", false);
  prefs.putInt("tg_dnd_start", kDefaultDndStartHour);
  prefs.putInt("tg_dnd_end", kDefaultDndEndHour);
  prefs.end();

  // Update globals so a running device reflects the reset immediately.
  telegramBotToken[0] = '\0';
  telegramChatId[0] = '\0';
  alertLossThresholdPct = kDefaultAlertLossThresholdPct;
  alertTempThresholdPct = kDefaultAlertTempThresholdPct;
  telegramDigestIntervalMin = kDefaultDigestIntervalMin;
  telegramDndEnabled = false;
  telegramDndStartHour = kDefaultDndStartHour;
  telegramDndEndHour = kDefaultDndEndHour;

  clearTrafficHistory();
}
