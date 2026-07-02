#pragma once

// Owns load/save/clear operations for persisted device configuration.
#include <Arduino.h>
#include <ArduinoJson.h>

struct DeviceConfig {
  char wlan_ssid[64];
  char wlan_password[64];
  char fritzbox_host[64];
  char api_key[160];
  char web_menu_password[64];
  char telegram_bot_token[64];
  char telegram_chat_id[32];
  int alert_loss_threshold_pct;
  int alert_temp_threshold_pct;
  int telegram_digest_interval_min;
  bool telegram_dnd_enabled;
  int telegram_dnd_start_hour;
  int telegram_dnd_end_hour;
};

class ConfigManager {
 public:
  static ConfigManager& getInstance();
  
  // Load config from Preferences
  bool loadConfig();
  
  // Save config to Preferences
  bool saveConfig();
  
  // Check if config is valid (has WLAN and FRITZ!Box data)
  bool isConfigured();
  
  // Get current config
  const DeviceConfig& getConfig() const { return config_; }
  
  // Update specific fields
  void setWlanSsid(const char* ssid);
  void setWlanPassword(const char* password);
  void setFritzboxHost(const char* host);
  void setApiKey(const char* key);
  void setWebMenuPassword(const char* password);
  void setTelegramBotToken(const char* token);
  void setTelegramChatId(const char* chatId);
  void setAlertLossThresholdPct(int pct);
  void setAlertTempThresholdPct(int pct);
  void setTelegramDigestIntervalMin(int minutes);
  void setTelegramDndEnabled(bool enabled);
  void setTelegramDndStartHour(int hour);
  void setTelegramDndEndHour(int hour);

  // Clear all config
  void clearConfig();
  
 private:
  ConfigManager();
  ~ConfigManager();
  
  DeviceConfig config_;
    bool has_required_data_ = false;
  
  // Prevent copying
  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;
};
