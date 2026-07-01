// Persistent configuration manager backed by ESP32 Preferences (NVS).
#include "config_manager.h"
#include "globals.h"
#include <Preferences.h>

ConfigManager& ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

ConfigManager::ConfigManager() {
  memset(&config_, 0, sizeof(config_));
  strcpy(config_.fritzbox_host, "192.168.1.1");
  strcpy(config_.web_menu_password, "fwmenu123");
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
    strlcpy(config_.fritzbox_host, "192.168.1.1", sizeof(config_.fritzbox_host));
  } else {
    strlcpy(config_.fritzbox_host, fritzbox_host.c_str(), sizeof(config_.fritzbox_host));
  }
  strlcpy(config_.api_key, api_key.c_str(), sizeof(config_.api_key));
  strlcpy(config_.web_menu_password, web_menu_password.c_str(), sizeof(config_.web_menu_password));
  
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
  
  prefs.end();
  
  // Update globals
  strlcpy(fritzBoxHost, config_.fritzbox_host, sizeof(fritzBoxHost));

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

void ConfigManager::clearConfig() {
  memset(&config_, 0, sizeof(config_));
  strcpy(config_.fritzbox_host, "192.168.1.1");
  strcpy(config_.web_menu_password, "fwmenu123");
  has_required_data_ = false;
  
  Preferences prefs;
  prefs.begin("fwstatus", false);
  // Explicitly wipe keys used by boot decision logic.
  prefs.putString("wlan_ssid", "");
  prefs.putString("wlan_password", "");
  prefs.putString("fritzbox_host", "");
  prefs.putString("api_key", "");
  prefs.putString("web_menu_password", "fwmenu123");
  prefs.end();
}
