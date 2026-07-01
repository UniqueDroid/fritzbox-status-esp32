#pragma once

// OTA update interface using GitHub releases as firmware source.
#include <Arduino.h>
#include <functional>

struct FirmwareReleaseInfo {
  String currentVersion;
  String latestVersion;
  String releaseName;
  String releaseBody;
  String releaseUrl;
  String assetUrl;
  String assetName;
  String assetDigest;  // "sha256:<hex>" from GitHub's release asset digest, empty if unavailable
  String publishedAt;
  size_t assetSize = 0;
  bool updateAvailable = false;
};

using FirmwareProgressCallback = std::function<void(size_t writtenBytes, size_t totalBytes)>;

bool fetchLatestFirmwareRelease(FirmwareReleaseInfo &info, String &errorMessage);
bool flashFirmwareAsset(const FirmwareReleaseInfo &info, String &errorMessage, const FirmwareProgressCallback &progressCallback = nullptr);