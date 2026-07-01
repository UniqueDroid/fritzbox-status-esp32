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
