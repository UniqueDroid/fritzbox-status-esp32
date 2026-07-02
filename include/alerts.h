#pragma once

// Telegram-based alerting for WAN down/up and threshold breaches.
#include "globals.h"

// Sends a message via the Telegram Bot API. No-op (returns false) when bot
// token/chat ID aren't configured or WiFi is down.
bool sendTelegramMessage(const String &message);

// Evaluates current WAN/system state against persisted thresholds and fires
// Telegram alerts on state transitions (edge-triggered, not every poll).
void checkAndSendAlerts();

// Sends a periodic WAN/CPU/RAM/temp status summary, throttled to once/hour.
void checkAndSendStatusDigest();

// Sends the WAN/CPU/RAM/temp status summary immediately, bypassing the
// hourly throttle, and resets the throttle window. Used by the manual
// "send now" button in the web menu. Returns false if not configured or
// the send failed.
bool sendStatusDigestNow();

// Polls Telegram for incoming messages (throttled) and replies to /status
// with an immediate status digest. Only accepts commands from the
// configured chat ID; backlog from before boot is discarded unanswered.
void checkTelegramCommands();

// Starts NTP time sync (Europe/Berlin, DST-aware). Needed for the Telegram
// quiet-hours window; call once after WiFi connects.
void ensureTimeSynced();

// Sends a one-time "device booted" notification. Call once after WiFi
// connects; no-op if Telegram isn't configured.
void sendBootNotification();

// Notifies once (edge-triggered) when a firmware update becomes available.
void checkAndNotifyFirmwareUpdate(bool updateAvailable, const String &latestVersion);

// Feeds the running min/max stats used by the daily summary. Call once per
// poll cycle.
void updateDailyStats();

// Sends a once-a-day (08:00 local) min/max summary and resets the running
// stats. No-op outside the trigger window or if Telegram isn't configured.
void checkAndSendDailySummary();

// Marks a snapshot as requested from the (non-LVGL-thread) command poller.
// Safe to call from any task.
void requestDisplaySnapshot();

// Must run on the same thread that drives lv_timer_handler() (LVGL is not
// thread-safe). Call once per main loop iteration; no-op unless a snapshot
// was requested via requestDisplaySnapshot().
void processPendingSnapshotRequest();
