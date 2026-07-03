// Captive portal, web menu and OTA UI logic.
// This module also owns boot overlay state and custom portal routes.
#include <Arduino.h>
#include <lvgl.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "alerts.h"
#include "config_portal.h"
#include "config_manager.h"
#include "assets/project_logo_png.h"
#include "firmware_update.h"
#include "firmware_version.h"
#include "globals.h"

#ifndef FW_BOOT_DEBUG
#define FW_BOOT_DEBUG 0
#endif

#if FW_BOOT_DEBUG
#define BOOTLOG(...) Serial.printf(__VA_ARGS__)
#else
#define BOOTLOG(...) do { } while (0)
#endif

namespace {
lv_obj_t *bootOverlay = nullptr;
lv_obj_t *bootStepWifi = nullptr;
lv_obj_t *bootStepApi = nullptr;
lv_obj_t *bootStepDash = nullptr;
bool bootCanDismiss = false;
uint32_t bootReadySinceMs = 0;
uint32_t bootShownSinceMs = 0;
bool bootSequenceEnabled = false;
bool bootHasHostConfig = false;
bool bootApiChecked = false;
bool bootApiReachableState = false;
uint32_t bootLastApiCheckMs = 0;
constexpr uint32_t kBootMinVisibleMs = 3600;
constexpr uint32_t kBootApiRetryMs = 4000;
String gCustomMenuHtml;
String gCustomStatusHtml;

enum class BootStepState : uint8_t {
  Pending,
  Ok,
  Fail,
  Skip,
};
}

bool isConfigured() {
  return ConfigManager::getInstance().isConfigured();
}

namespace {

void setStepText(lv_obj_t *label, const char *name, BootStepState state, const char *extra = nullptr) {
  if (!label) {
    return;
  }

  const char *symbol = LV_SYMBOL_MINUS;
  lv_color_t color = lv_color_hex(0x8E9BAC);
  switch (state) {
    case BootStepState::Pending:
      symbol = LV_SYMBOL_REFRESH;
      color = lv_color_hex(0x8E9BAC);
      break;
    case BootStepState::Ok:
      symbol = LV_SYMBOL_OK;
      color = lv_color_hex(0x83F7AF);
      break;
    case BootStepState::Fail:
      symbol = LV_SYMBOL_CLOSE;
      color = lv_color_hex(0xFF8B8B);
      break;
    case BootStepState::Skip:
      symbol = LV_SYMBOL_WARNING;
      color = lv_color_hex(0xF5B942);
      break;
  }

  if (extra && strlen(extra) > 0) {
    lv_label_set_text_fmt(label, "%s  %s: %s", symbol, name, extra);
  } else {
    lv_label_set_text_fmt(label, "%s  %s", symbol, name);
  }
  lv_obj_set_style_text_color(label, color, 0);
}

bool tryApiCheckHttp(const String &url) {
  // Lightweight probe used by boot sequence to validate host quickly.
  // Kept tight since this runs synchronously on the boot/UI thread - a
  // slow/unreachable host would otherwise block the display for several
  // seconds with no visual feedback.
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setConnectTimeout(1500);
  http.setTimeout(1500);
  int code = http.GET();
  http.end();
  return code == HTTP_CODE_OK;
}

bool tryApiCheckHttps(const String &url) {
  // TLS probe first; cert checks are intentionally relaxed for local appliances.
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(3);
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setConnectTimeout(1500);
  http.setTimeout(1500);
  int code = http.GET();
  http.end();
  return code == HTTP_CODE_OK;
}

bool isApiReachable() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String host = String(fritzBoxHost);
  String path = "/tr64desc.xml";
  // Prefer HTTPS and fallback to HTTP to tolerate mixed FRITZ!Box setups.
  if (tryApiCheckHttps(String("https://") + host + path)) {
    return true;
  }
  if (tryApiCheckHttp(String("http://") + host + path)) {
    return true;
  }
  return false;
}

String sanitizeHost(const char *raw) {
  String host = raw ? String(raw) : String();
  host.trim();

  if (host.startsWith("http://")) {
    host.remove(0, 7);
  } else if (host.startsWith("https://")) {
    host.remove(0, 8);
  }

  int slash = host.indexOf('/');
  if (slash >= 0) {
    host = host.substring(0, slash);
  }

  host.trim();
  return host;
}

String sanitizeMenuPassword(const char *raw) {
  String pass = raw ? String(raw) : String();
  pass.trim();
  return pass;
}

String buildCustomStatusHtml(bool firstRun) {
  if (firstRun) {
    String html = "<div class='msg'><strong>Fritzbox Host:</strong> not configured</div>";
    html += "<div class='msg'><strong>Firmware:</strong> ";
    html += String(kFirmwareVersion);
    html += "</div>";
    return html;
  }

  String fwColor = bootApiReachableState ? "#5cb85c" : "#FF8B8B";
  String html = "<div class='msg' style='border-left-color:";
  html += fwColor;
  html += "'><strong>Fritzbox Host:</strong> ";
  html += String(fritzBoxHost);
  html += "</div>";
  html += "<div class='msg'><strong>Firmware:</strong> ";
  html += String(kFirmwareVersion);
  html += "</div>";
  return html;
}

String buildCustomMenuHtml(bool firstRun) {
  if (firstRun) {
    return String(
      "<div class='msg'><strong>First Run</strong><br/>Open configuration.</div>"
      "<form action='/wifi' method='get'><button>Configure FW Settings</button></form>");
  }

  String html;
  html += "<form action='/telegram-settings' method='get'><button>Telegram Settings</button></form>";
  html += "<form action='/factory-erase' method='post' onsubmit=\"return confirm('Erase all saved config and reboot?');\">";
  html += "<button style='background:#b00020;color:#fff'>Config Erase</button></form>";
  html += "<form action='/firmware-update' method='get'><button>Firmware Update</button></form>";
  html += "<form action='/logout' method='get'><button>Logout</button></form>";
  return html;
}

String escapeHtml(String text) {
  text.replace("&", "&amp;");
  text.replace("<", "&lt;");
  text.replace(">", "&gt;");
  text.replace("\"", "&quot;");
  text.replace("'", "&#39;");
  return text;
}

String formatReleaseBody(String body) {
  body.replace("\r", "");
  body.replace("\n", "<br/>");
  if (body.length() > 900) {
    body = body.substring(0, 900) + "<br/>...";
  }
  return body;
}

String escapeJson(String text) {
  text.replace("\\", "\\\\");
  text.replace("\"", "\\\"");
  text.replace("\n", "\\n");
  text.replace("\r", "");
  return text;
}

String buildFirmwareUpdateStyles() {
  // Matches the Unique ESP32 Flasher theme (cyan/blue circuit ring, warm
  // orange accent, deep navy background).
  String css;
  css.reserve(2200);
  css += ":root{--accent:#4fc3f7;--accent-2:#0f6fa8;--spark:#ff8f3d;--bg:#070c13;--bg-grad:#0d2338;--surface:#101823;--border:#22303f;--text:#e7f3fb;--text-mute:#a9bcca;--warn:#f5a93c;--ok:#4ade80;--bad:#dc3630}";
  css += ".c,body{text-align:center;font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif;background:radial-gradient(ellipse 900px 500px at 50% -10%,var(--bg-grad),transparent) var(--bg);color:var(--text)}div,input,select{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}";
  css += "input,select{border-radius:10px;width:100%;background:var(--surface);border:1px solid var(--border);color:var(--text);margin:8px 0}";
  css += "button{border-radius:999px;width:100%;cursor:pointer;border:0;background:linear-gradient(135deg,var(--accent),var(--accent-2));color:#0d1015;font-weight:600;line-height:2.4rem;font-size:1.2rem;margin:6px 0}";
  css += "button.D{background:var(--bad);color:#fff}.wrap{text-align:left;display:inline-block;min-width:260px;max-width:500px}";
  css += "hr{border:none;border-top:1px solid var(--border);margin:18px 0}";
  css += ".brand{display:flex;justify-content:center;align-items:center;min-height:64px;margin:6px 0 4px 0}.brand-logo{display:block;max-width:min(100%,320px);height:auto;margin:0 auto}.brand-title{margin:0;line-height:1.1;color:var(--text)}body.haslogo .brand-title{display:none}";
  css += "a{color:var(--accent);font-weight:700;text-decoration:none}a:hover{color:#7dd8fb;text-decoration:underline}";
  css += ".msg{padding:20px;margin:20px 0;border:1px solid var(--border);border-radius:10px;border-left-width:5px;border-left-color:#777;background:var(--surface);color:var(--text-mute)}";
  css += ".msg.S{border-left-color:var(--ok)}.msg.D{border-left-color:var(--bad)}.msg.P{border-left-color:var(--accent)}";
  css += "dt{font-weight:bold;color:var(--text)}dd{margin:0;padding:0 0 .5em 0;min-height:12px}";
  css += ".progress-shell{margin:18px 0 14px 0}.progress-track{width:100%;height:14px;background:var(--surface);border-radius:999px;overflow:hidden;border:1px solid var(--border);padding:0 !important;margin:0 !important}.progress-bar{height:100%;width:0%;background:linear-gradient(90deg,var(--accent),var(--accent-2));border-radius:999px;transition:width .2s ease;padding:0 !important;margin:0 !important;display:block}.progress-shell.fail .progress-bar{background:var(--bad)}.progress-text{font-size:.95em;color:var(--text-mute);padding:0;margin:8px 0 0 0}";
  return css;
}

String buildPortalHeaderHtml(const String &subtitle) {
  String html;
  html.reserve(280);
  html += "<div class='brand'><img class='brand-logo' src='/project-logo.png' alt='Project logo' onload=\"document.body.classList.add('haslogo')\" onerror=\"this.style.display='none'\"><h1 class='brand-title'>FRITZ!Box Status</h1></div><h3>";
  html += escapeHtml(subtitle);
  html += "</h3>";
  return html;
}

void appendFirmwareReleaseInfo(String &html, const FirmwareReleaseInfo &info) {
  html += "<h3>Release Information</h3><hr><dl>";
  html += "<dt>Current</dt><dd>";
  html += escapeHtml(info.currentVersion);
  html += "</dd>";
  html += "<dt>Latest</dt><dd>";
  html += escapeHtml(info.latestVersion.length() ? info.latestVersion : String("unknown"));
  html += "</dd>";
  html += "<dt>Release</dt><dd>";
  html += escapeHtml(info.releaseName.length() ? info.releaseName : String("-"));
  html += "</dd>";
  html += "<dt>Asset</dt><dd>";
  html += escapeHtml(info.assetName.length() ? info.assetName : String("-"));
  html += "</dd>";
  html += "<dt>Published</dt><dd>";
  html += escapeHtml(info.publishedAt.length() ? info.publishedAt : String("-"));
  html += "</dd></dl>";
}

String buildFirmwareInstallPageStart(const FirmwareReleaseInfo &info) {
  String html;
  html.reserve(7000);
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  html += "<title>Firmware Update</title><style>";
  html += buildFirmwareUpdateStyles();
  html += "</style></head><body><div class='wrap'>";
  html += buildPortalHeaderHtml("Firmware Update");
  html += "<div id='installProgress' class='progress-shell'><div class='progress-track'><div id='installBar' class='progress-bar'></div></div><p id='installStatus' class='progress-text'>Starting firmware download...</p></div>";
  html += "<div class='msg P'><strong>Current firmware:</strong> ";
  html += escapeHtml(kFirmwareVersion);
  html += "</div>";
  appendFirmwareReleaseInfo(html, info);
  if (info.releaseBody.length() > 0) {
    html += "<div class='msg'><strong>Release Notes</strong><br/>";
    html += formatReleaseBody(escapeHtml(info.releaseBody));
    html += "</div>";
  }
  html += "<script>function setFwProgress(percent,message,failed){var bar=document.getElementById('installBar');var shell=document.getElementById('installProgress');var status=document.getElementById('installStatus');if(bar){bar.style.width=percent+'%';}if(status){status.innerHTML=message;}if(shell){shell.className=failed?'progress-shell fail':'progress-shell';}}</script>";
  return html;
}

String buildFirmwareUpdatePage(const FirmwareReleaseInfo &info, const String &message = String(), bool success = false) {
  String html;
  html.reserve(9500);
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  html += "<title>Firmware Update</title>";
  html += "<style>";
  html += buildFirmwareUpdateStyles();
  html += "#installProgress{display:none}</style></head><body><div class='wrap'>";
  html += buildPortalHeaderHtml("Firmware Update");
  html += "<script>function startFirmwareInstall(){if(!confirm('Download and install this release now?'))return false;var buttons=document.querySelectorAll('.fw-install-btn');for(var i=0;i<buttons.length;i++){buttons[i].disabled=true;}window.location='/firmware-update/install';return false;}</script>";

  if (message.length() > 0) {
    html += "<div class='msg ";
    html += success ? "S'" : "D'";
    html += "><strong>";
    html += success ? "Status" : "Notice";
    html += "</strong><br/>";
    html += escapeHtml(message);
    html += "</div>";
  }

  html += "<div class='msg P'><strong>Current firmware:</strong> ";
  html += escapeHtml(kFirmwareVersion);
  html += "</div>";
  html += "<div id='installProgress' class='progress-shell'><div class='progress-track'><div class='progress-bar'></div></div><p id='installStatus' class='progress-text'>Preparing firmware update...</p></div>";

  appendFirmwareReleaseInfo(html, info);

  html += "<div class='msg ";
  html += info.updateAvailable ? "P'" : "S'";
  html += "><strong>";
  html += info.updateAvailable ? "Update available" : "Already on the latest GitHub release";
  html += "</strong></div>";

  if (info.releaseBody.length() > 0) {
    html += "<div class='msg'><strong>Release Notes</strong><br/>";
    html += formatReleaseBody(escapeHtml(info.releaseBody));
    html += "</div>";
  }

  html += "<form action='";
  html += kFirmwareGitHubReleasesUrl;
  html += "' method='get' target='_blank' style='margin-bottom:14px'><button type='submit'>Open GitHub Releases</button></form>";

  if (WiFi.status() == WL_CONNECTED && info.assetUrl.length() > 0) {
    html += "<form onsubmit='return startFirmwareInstall();'><button class='D fw-install-btn' type='submit'>Download & Flash Latest Release</button></form>";
  } else if (WiFi.status() != WL_CONNECTED) {
    html += "<div class='msg D'><strong>WiFi is not connected.</strong><br/>Firmware updates need network access.</div>";
  }

  html += "<hr><br/><form action='/' method='get'><button type='submit'>Back to Menu</button></form>";
  html += "</div></body></html>";
  return html;
}

String buildTelegramSettingsPage(const String &notice = String(), bool noticeSuccess = false) {
  const DeviceConfig& cfg = ConfigManager::getInstance().getConfig();

  String html;
  html.reserve(5500);
  html += "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>";
  html += "<title>Telegram Settings</title><style>";
  html += buildFirmwareUpdateStyles();
  html += "label{display:block;font-weight:bold;margin-top:10px;color:var(--text)}";
  html += "</style></head><body><div class='wrap'>";
  html += buildPortalHeaderHtml("Telegram Settings");

  if (notice.length() > 0) {
    html += "<div class='msg ";
    html += noticeSuccess ? "S'" : "D'";
    html += "><strong>";
    html += noticeSuccess ? "Status" : "Notice";
    html += "</strong><br/>";
    html += escapeHtml(notice);
    html += "</div>";
  }

  html += "<form method='POST' action='/telegram-settings'>";
  html += "<label for='tg_bot_token'>Bot Token</label><input id='tg_bot_token' name='tg_bot_token' maxlength='63' value='";
  html += escapeHtml(String(cfg.telegram_bot_token));
  html += "'>";
  html += "<label for='tg_chat_id'>Chat ID</label><input id='tg_chat_id' name='tg_chat_id' maxlength='31' value='";
  html += escapeHtml(String(cfg.telegram_chat_id));
  html += "'>";
  html += "<label for='alert_loss_pct'>Alert: Packet Loss above (%)</label><input id='alert_loss_pct' type='number' min='0' max='100' name='alert_loss_pct' value='";
  html += String(cfg.alert_loss_threshold_pct);
  html += "'>";
  html += "<label for='alert_temp_pct'>Alert: Temperature above (%)</label><input id='alert_temp_pct' type='number' min='0' max='100' name='alert_temp_pct' value='";
  html += String(cfg.alert_temp_threshold_pct);
  html += "'>";
  html += "<label for='tg_digest_min'>Status digest interval (minutes)</label><input id='tg_digest_min' type='number' min='1' max='1440' name='tg_digest_min' value='";
  html += String(cfg.telegram_digest_interval_min);
  html += "'>";
  html += "<label style='margin-top:16px'><input type='checkbox' style='width:auto;display:inline-block' name='tg_dnd_on' ";
  html += cfg.telegram_dnd_enabled ? "checked" : "";
  html += "> Quiet hours (mutes the digest only, alerts still come through)</label>";
  html += "<label for='tg_dnd_start'>Quiet hours start (0-23)</label><input id='tg_dnd_start' type='number' min='0' max='23' name='tg_dnd_start' value='";
  html += String(cfg.telegram_dnd_start_hour);
  html += "'>";
  html += "<label for='tg_dnd_end'>Quiet hours end (0-23)</label><input id='tg_dnd_end' type='number' min='0' max='23' name='tg_dnd_end' value='";
  html += String(cfg.telegram_dnd_end_hour);
  html += "'>";
  html += "<button type='submit' style='margin-top:14px'>Save</button></form>";

  if (strlen(cfg.telegram_bot_token) > 0 && strlen(cfg.telegram_chat_id) > 0) {
    html += "<form method='POST' action='/telegram-settings/test' style='margin-top:10px'><button>Send Status Now</button></form>";
  }

  html += "<hr><br/><form action='/' method='get'><button type='submit'>Back to Menu</button></form>";
  html += "</div></body></html>";
  return html;
}

void applyPortalCustomHtml(bool firstRun) {
  // WiFiManager stores pointers, so keep backing strings in module-level storage.
  gCustomMenuHtml = buildCustomMenuHtml(firstRun);
  gCustomStatusHtml = buildCustomStatusHtml(firstRun);
  gCustomMenuHtml += gCustomStatusHtml;
  wm.setCustomMenuHTML(gCustomMenuHtml.c_str());
}

void startProtectedConfigPortal() {
  // Ensure AP is recreated with the expected SSID/password after mode switches.
  WiFi.softAPdisconnect(true);
  wm.startConfigPortal(kApName, kApPassword);
}

void clearBootOverlay() {
  if (bootOverlay) {
    lv_obj_del(bootOverlay);
    bootOverlay = nullptr;
  }
  bootStepWifi = nullptr;
  bootStepApi = nullptr;
  bootStepDash = nullptr;
  bootCanDismiss = false;
  bootReadySinceMs = 0;
  bootShownSinceMs = 0;
  bootApiChecked = false;
  bootApiReachableState = false;
  bootLastApiCheckMs = 0;
}
}  // namespace

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void saveParamsCallback() {
  // Persist immediately and force apply path in loop() so host changes take effect.
  persistFirewallConfig();
  shouldSaveConfig = true;
}

void persistFirewallConfig() {
  if (!fritzboxIpParam) {
    return;
  }

  String host = sanitizeHost(fritzboxIpParam->getValue());

  if (host.length() == 0) {
    host = String(fritzBoxHost);
  }

  strlcpy(fritzBoxHost, host.c_str(), sizeof(fritzBoxHost));
  
  // Save sanitized values in persistent config for next boot.
  ConfigManager& cfg = ConfigManager::getInstance();
  cfg.setFritzboxHost(fritzBoxHost);
  if (menuPasswordParam) {
    cfg.setWebMenuPassword(sanitizeMenuPassword(menuPasswordParam->getValue()).c_str());
  }
  cfg.saveConfig();
  
  // Re-evaluate boot mode immediately so overlay/status reflects new state.
  bootSequenceEnabled = cfg.isConfigured();
  bootHasHostConfig = bootSequenceEnabled;
  if (bootOverlay) {
    drawBootScreen();
  }
}

void drawBootScreen() {
  // Rebuild overlay from scratch to avoid stale LVGL object pointers.
  clearBootOverlay();

  bootOverlay = lv_obj_create(lv_scr_act());
  bootShownSinceMs = millis();
  lv_obj_remove_style_all(bootOverlay);
  lv_obj_set_size(bootOverlay, DASHBOARD_WIDTH, DASHBOARD_HEIGHT);
  lv_obj_align(bootOverlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(bootOverlay, lv_color_hex(0x101318), 0);
  lv_obj_set_style_bg_grad_color(bootOverlay, lv_color_hex(0x1B2330), 0);
  lv_obj_set_style_bg_grad_dir(bootOverlay, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(bootOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bootOverlay, 0, 0);

  lv_obj_t *icon = lv_label_create(bootOverlay);
  lv_label_set_text(icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(0x83F7AF), 0);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 14, 10);

  lv_obj_t *title = lv_label_create(bootOverlay);
  lv_label_set_text(title, "FRITZ!Box Status");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xDDE7F2), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 48, 12);

  if (bootSequenceEnabled) {
    lv_obj_t *status = lv_label_create(bootOverlay);
    lv_label_set_text(status, "Boot sequence");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x8E9BAC), 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 14, 36);

    bootStepWifi = lv_label_create(bootOverlay);
    lv_obj_set_style_text_font(bootStepWifi, &lv_font_montserrat_14, 0);
    lv_obj_align(bootStepWifi, LV_ALIGN_TOP_LEFT, 14, 60);
    setStepText(bootStepWifi, "WiFi", BootStepState::Pending, "connecting");

    bootStepApi = lv_label_create(bootOverlay);
    lv_obj_set_style_text_font(bootStepApi, &lv_font_montserrat_14, 0);
    lv_obj_align(bootStepApi, LV_ALIGN_TOP_LEFT, 14, 84);
    setStepText(bootStepApi, "TR-064", BootStepState::Pending, "waiting");

    bootStepDash = lv_label_create(bootOverlay);
    lv_obj_set_style_text_font(bootStepDash, &lv_font_montserrat_14, 0);
    lv_obj_align(bootStepDash, LV_ALIGN_TOP_LEFT, 14, 110);
    setStepText(bootStepDash, "Dashboard", BootStepState::Pending, "initializing");
  } else {
    lv_obj_t *status = lv_label_create(bootOverlay);
    lv_label_set_text(status, "First start / portal mode");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0xF5B942), 0);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 14, 36);

    lv_obj_t *portalState = lv_label_create(bootOverlay);
    lv_label_set_text(portalState, "No saved host config yet");
    lv_obj_set_style_text_font(portalState, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(portalState, lv_color_hex(0x8E9BAC), 0);
    lv_obj_align(portalState, LV_ALIGN_TOP_LEFT, 14, 60);

    lv_obj_t *pass = lv_label_create(bootOverlay);
    lv_label_set_text_fmt(pass, "AP Password: %s", kApPassword);
    lv_obj_set_style_text_font(pass, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pass, lv_color_hex(0xF5B942), 0);
    lv_obj_align(pass, LV_ALIGN_TOP_LEFT, 14, 84);

    lv_obj_t *hint = lv_label_create(bootOverlay);
    lv_label_set_text(hint, "Open portal for setup:");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAB6C4), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 14, 110);

    lv_obj_t *ap = lv_label_create(bootOverlay);
    lv_label_set_text_fmt(ap, "AP: %s", kApName);
    lv_obj_set_style_text_font(ap, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ap, lv_color_hex(0x83F7AF), 0);
    lv_obj_align(ap, LV_ALIGN_BOTTOM_LEFT, 14, -20);

    lv_obj_t *portal = lv_label_create(bootOverlay);
    lv_label_set_text(portal, "Portal: 192.168.4.1");
    lv_obj_set_style_text_font(portal, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(portal, lv_color_hex(0xF5B942), 0);
    lv_obj_align(portal, LV_ALIGN_BOTTOM_RIGHT, -14, -20);
  }

  lv_timer_handler();
}

void dismissBootScreenIfConnected() {
  if (!bootOverlay) {
    return;
  }
  if (!bootSequenceEnabled) {
    return;
  }

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  if (wifiOk) {
    setStepText(bootStepWifi, "WiFi", BootStepState::Ok, WiFi.localIP().toString().c_str());
  } else if (bootHasHostConfig) {
    setStepText(bootStepWifi, "WiFi", BootStepState::Pending, "reconnecting");
  }

  // Retry failed API checks periodically while overlay is still visible.
  if (bootHasHostConfig && wifiOk && (!bootApiChecked || (!bootApiReachableState && (millis() - bootLastApiCheckMs) >= kBootApiRetryMs))) {
    setStepText(bootStepApi, "TR-064", BootStepState::Pending, "checking host");
    // Flush this state to the display before the blocking network call below -
    // otherwise the "checking host" text never actually renders and the
    // screen looks frozen for the whole check instead of visibly retrying.
    lv_timer_handler();
    bootApiReachableState = isApiReachable();
    bootApiChecked = true;
    bootLastApiCheckMs = millis();
    if (bootApiReachableState) {
      setStepText(bootStepApi, "TR-064", BootStepState::Ok, "connected");
    } else {
      setStepText(bootStepApi, "TR-064", BootStepState::Fail, "unreachable");
    }
    applyPortalCustomHtml(false);
  }

  if (!bootCanDismiss) {
    return;
  }
  if (!wifiOk) {
    return;
  }
  if (bootHasHostConfig && !bootApiChecked) {
    return;
  }
  if (millis() - bootShownSinceMs < kBootMinVisibleMs) {
    return;
  }
  if (millis() - bootReadySinceMs < 700) {
    return;
  }
  clearBootOverlay();
}

void markBootDashboardReady() {
  if (!bootOverlay || bootCanDismiss) {
    return;
  }
  if (!bootSequenceEnabled) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setStepText(bootStepDash, "Dashboard", BootStepState::Pending, "wait wifi");
    return;
  }
  if (bootHasHostConfig && !bootApiChecked) {
    setStepText(bootStepDash, "Dashboard", BootStepState::Pending, "wait tr-064");
    return;
  }
  setStepText(bootStepDash, "Dashboard", BootStepState::Ok, "ready");
  bootCanDismiss = true;
  bootReadySinceMs = millis();
}

void setupPortalRoutes() {
  if (!wm.server) {
    return;
  }

  // Serve project logo directly from firmware flash to avoid external dependencies.
  wm.server->on("/project-logo.png", HTTP_GET, []() {
    wm.server->sendHeader("Cache-Control", "public, max-age=86400");
    wm.server->setContentLength(kProjectLogoPngLen);
    wm.server->send(200, "image/png", "");
    WiFiClient client = wm.server->client();
    client.write(kProjectLogoPng, kProjectLogoPngLen);
  });

  auto eraseAllAndReboot = []() {
    // Custom routes bypass WiFiManager's built-in page handlers, so they don't
    // get the session/menu-password gate for free. Apply it explicitly here.
    if (!wm.handleRequest()) {
      return;
    }
    BOOTLOG("[BOOT] Factory erase requested\n");
    wm.server->send(200, "text/html", "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif;text-align:center;padding:18px;background:radial-gradient(ellipse 900px 500px at 50% -10%,#0d2338,transparent) #070c13;color:#e7f3fb}.wm-brand{display:flex;justify-content:center;align-items:center;min-height:64px;margin:4px 0 10px 0}.wm-brand-logo{display:block;max-width:min(100%,320px);height:auto;margin:0 auto}</style></head><body><div class='wm-brand'><img class='wm-brand-logo' src='/project-logo.png' alt='Project logo'></div><p><strong>Config erased. Rebooting...</strong></p></body></html>");
    wm.server->client().stop();
    delay(600);
    ConfigManager::getInstance().clearConfig();
    // Clear WiFiManager/SDK STA credentials after app config keys are wiped.
    wm.resetSettings();
    delay(500);
    ESP.restart();
  };

  wm.server->on("/factory-erase", HTTP_POST, eraseAllAndReboot);

  wm.server->on("/telegram-settings", HTTP_GET, []() {
    if (!wm.handleRequest()) {
      return;
    }
    wm.server->send(200, "text/html", buildTelegramSettingsPage());
  });

  wm.server->on("/telegram-settings", HTTP_POST, []() {
    if (!wm.handleRequest()) {
      return;
    }
    ConfigManager& cfg = ConfigManager::getInstance();
    cfg.setTelegramBotToken(wm.server->arg("tg_bot_token").c_str());
    cfg.setTelegramChatId(wm.server->arg("tg_chat_id").c_str());
    cfg.setAlertLossThresholdPct(wm.server->arg("alert_loss_pct").toInt());
    cfg.setAlertTempThresholdPct(wm.server->arg("alert_temp_pct").toInt());
    cfg.setTelegramDigestIntervalMin(wm.server->arg("tg_digest_min").toInt());
    cfg.setTelegramDndEnabled(wm.server->hasArg("tg_dnd_on"));
    cfg.setTelegramDndStartHour(wm.server->arg("tg_dnd_start").toInt());
    cfg.setTelegramDndEndHour(wm.server->arg("tg_dnd_end").toInt());
    cfg.saveConfig();
    wm.server->send(200, "text/html", buildTelegramSettingsPage("Settings saved.", true));
  });

  wm.server->on("/telegram-settings/test", HTTP_POST, []() {
    if (!wm.handleRequest()) {
      return;
    }
    bool sent = sendStatusDigestNow();
    wm.server->send(200, "text/html", buildTelegramSettingsPage(sent ? "Status sent to Telegram." : "Send failed - check bot token/chat ID and WiFi.", sent));
  });

  wm.server->on("/firmware-update", HTTP_GET, []() {
    if (!wm.handleRequest()) {
      return;
    }
    FirmwareReleaseInfo info;
    String errorMessage;
    bool ok = fetchLatestFirmwareRelease(info, errorMessage);
    if (!ok) {
      info.currentVersion = kFirmwareVersion;
      info.latestVersion = "unavailable";
        info.releaseName = "GitHub release unavailable";
        info.releaseBody.clear();
        info.releaseUrl = kFirmwareGitHubReleasesUrl;
    }

    wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, ok ? String() : errorMessage, ok));
  });

  wm.server->on("/firmware-update/install", HTTP_GET, []() {
    if (!wm.handleRequest()) {
      return;
    }
    FirmwareReleaseInfo info;
    String errorMessage;
    if (!fetchLatestFirmwareRelease(info, errorMessage)) {
      wm.server->send(200, "text/html", buildFirmwareUpdatePage(info, errorMessage, false));
      return;
    }

    wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    wm.server->send(200, "text/html", "");
    wm.server->sendContent(buildFirmwareInstallPageStart(info));

    // Stream incremental progress into the page while OTA writes flash blocks.
    int lastPercent = -1;
    bool flashed = flashFirmwareAsset(info, errorMessage, [&](size_t writtenBytes, size_t totalBytes) {
      int percent = 0;
      if (totalBytes > 0) {
        percent = static_cast<int>((writtenBytes * 100U) / totalBytes);
      } else if (writtenBytes > 0) {
        percent = 95;
      }
      if (percent > 100) {
        percent = 100;
      }
      if (percent == lastPercent) {
        return;
      }
      lastPercent = percent;
      String script = "<script>setFwProgress(";
      script += percent;
      script += ",\"";
      script += escapeJson(percent >= 100 ? String("Verifying checksum (SHA256)...") : String("Downloading and flashing firmware... ") + percent + "%");
      script += "\",false);</script>";
      wm.server->sendContent(script);
    });

    if (!flashed) {
      String script = "<script>setFwProgress(100,\"";
      script += escapeJson(errorMessage.length() ? errorMessage : String("Firmware update failed."));
      script += "\",true);</script>";
      wm.server->sendContent(script);
      wm.server->sendContent("<hr><br/><form action='/firmware-update' method='get'><button type='submit'>Back to Firmware Update</button></form></div></body></html>");
      return;
    }

    wm.server->sendContent("<script>setFwProgress(100,\"<span style='color:#2e7d32;font-weight:700'>&#10003;</span> Checksum verified. Firmware updated successfully. Device is rebooting now...\",false);</script>");
    wm.server->sendContent("</div></body></html>");
    delay(1600);
    ESP.restart();
  });
}

void handleConfigSavedTransition() {
  // Persist custom params, then restart so WiFiManager reconnects using stored WLAN credentials.
  persistFirewallConfig();
  delay(250);
  ESP.restart();
}

void configureWiFi() {
  const char *firstRunMenu[] = {"custom"};
  const char *fullMenu[] = {"wifi", "param", "info", "custom", "restart", "sep"};
  static const char kPortalLogoHeadElement[] = R"HTML(
<style>
.wm-brand{display:flex;justify-content:center;align-items:center;min-height:64px;margin:6px 0 8px 0}
.wm-brand-logo{display:block;max-width:min(100%,320px);height:auto;margin:0 auto}

/* Unique ESP32 Flasher theme, applied to WiFiManager's native pages.
   Comes after WiFiManager's own HTTP_STYLE in the page <head>, so equal-
   specificity rules here win without needing !important. */
body{background:radial-gradient(ellipse 900px 500px at 50% -10%,#0d2338,transparent) #070c13;color:#e7f3fb;font-family:-apple-system,system-ui,'Segoe UI',Roboto,sans-serif}
h1,h2,h3{color:#e7f3fb}
a{color:#4fc3f7}
a:hover{color:#7dd8fb}
input,select{background:#101823;border:1px solid #22303f;color:#e7f3fb;border-radius:10px;margin:8px 0}
button,input[type='button'],input[type='submit']{background:linear-gradient(135deg,#4fc3f7,#0f6fa8);color:#0d1015;font-weight:600;border-radius:999px;margin:6px 0}
button.D{background:#dc3630;color:#fff}
.msg{background:#101823;border:1px solid #22303f;border-left-width:5px;border-radius:10px;color:#a9bcca}
.msg.P{border-left-color:#4fc3f7}
.msg.D{border-left-color:#dc3630}
.msg.S{border-left-color:#4ade80}
/* WiFiManager's own templates space things out with bare <hr>/<br> tags
   instead of consistent CSS margins - the hr in particular rendered as a
   plain white default browser line against the dark theme. This makes
   spacing uniform regardless of which template put what where. */
hr{border:none;border-top:1px solid #22303f;margin:18px 0}
</style>
<script>
(function(){
  function ensurePortalLogo(){
    var body=document.body;
    if(!body||body.classList.contains('wm-logo-ready')) return;
    body.classList.add('wm-logo-ready');

    var brand=document.createElement('div');
    brand.className='wm-brand';

    var img=document.createElement('img');
    img.className='wm-brand-logo';
    img.alt='Project logo';
    img.src='/project-logo.png';
    img.onerror=function(){this.style.display='none';};
    brand.appendChild(img);

    var heading=body.querySelector('h1, h2, h3');
    if(heading){
      heading.style.display='none';
      heading.parentNode.insertBefore(brand, heading);
      return;
    }

    var wrap=body.querySelector('.wrap');
    if(wrap){
      wrap.insertBefore(brand, wrap.firstChild);
      return;
    }

    body.insertBefore(brand, body.firstChild);
  }

  if(document.readyState==='loading'){
    document.addEventListener('DOMContentLoaded', ensurePortalLogo);
  }else{
    ensurePortalLogo();
  }
})();
</script>
)HTML";

  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setSaveParamsCallback(saveParamsCallback);
  wm.setConfigPortalBlocking(false);
  wm.setTitle("FRITZ!Box Status");
  wm.setCustomHeadElement(kPortalLogoHeadElement);
  wm.setShowBack(true);
  // Keep Info page clean: only informational content, no destructive quick actions.
  wm.setShowInfoErase(false);
  wm.setShowInfoUpdate(false);

  // Seed portal fields from persisted config so edits are incremental.
  const DeviceConfig& cfg = ConfigManager::getInstance().getConfig();

  if (!fritzboxIpParam) {
    fritzboxIpParam = new WiFiManagerParameter("fritzbox_ip", "FRITZ!Box Host / IP", cfg.fritzbox_host, sizeof(cfg.fritzbox_host));
    wm.addParameter(fritzboxIpParam);
  }


  if (!menuPasswordParam) {
    menuPasswordParam = new WiFiManagerParameter("web_menu_password", "Menu Password (min 8 chars)", cfg.web_menu_password, sizeof(cfg.web_menu_password));
    wm.addParameter(menuPasswordParam);
  }

  bootSequenceEnabled = isConfigured();
  BOOTLOG("[BOOT] isConfigured=%d host='%s'\n", bootSequenceEnabled ? 1 : 0, cfg.fritzbox_host);
  wm.setParamsPage(false);
  wm.setMenu(bootSequenceEnabled ? fullMenu : firstRunMenu, bootSequenceEnabled ? 6 : 1);
  applyPortalCustomHtml(!bootSequenceEnabled);
  if (bootSequenceEnabled && strlen(cfg.web_menu_password) >= 8) {
    wm.setHttpAuth("admin", cfg.web_menu_password);
  } else {
    wm.setHttpAuth("", "");
  }
  drawBootScreen();
  bootHasHostConfig = bootSequenceEnabled;

  if (!bootSequenceEnabled) {
    BOOTLOG("[BOOT] First-run path, starting AP '%s' (pass len=%u)\n", kApName, (unsigned)strlen(kApPassword));
    setStepText(bootStepWifi, "WiFi", BootStepState::Skip, "AP mode");
    setStepText(bootStepApi, "TR-064", BootStepState::Skip, "host not set");
    bootApiChecked = true;
    bootApiReachableState = false;
    startProtectedConfigPortal();
    return;
  }

  // In configured mode, first try STA reconnect before exposing AP portal.
  WiFi.begin();
  uint32_t wifiWaitStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiWaitStart) < 8000) {
    delay(50);
    lv_timer_handler();
  }
  if (WiFi.status() != WL_CONNECTED) {
    BOOTLOG("[BOOT] WiFi connect failed, fallback to AP '%s'\n", kApName);
    setStepText(bootStepWifi, "WiFi", BootStepState::Fail, "connect failed");
    setStepText(bootStepApi, "TR-064", BootStepState::Skip, "no network");
    startProtectedConfigPortal();
    return;
  }
  setStepText(bootStepWifi, "WiFi", BootStepState::Ok, WiFi.localIP().toString().c_str());
  // Flush before the blocking calls below (NTP kickoff is fire-and-forget and
  // fast, but the Telegram boot notification and API check both do network
  // I/O with no other UI updates in between - without this the screen would
  // sit on the previous frame and look frozen for their combined duration).
  lv_timer_handler();
  ensureTimeSynced();
  sendBootNotification();

  setStepText(bootStepApi, "TR-064", BootStepState::Pending, "checking host");
  lv_timer_handler();
  bool apiReachable = isApiReachable();
  bootApiChecked = true;
  bootApiReachableState = apiReachable;
  bootLastApiCheckMs = millis();
  if (apiReachable) {
    setStepText(bootStepApi, "TR-064", BootStepState::Ok, "connected");
  } else {
    setStepText(bootStepApi, "TR-064", BootStepState::Fail, "unreachable");
  }
  applyPortalCustomHtml(false);

  if (shouldSaveConfig) {
    persistFirewallConfig();
  }
}
