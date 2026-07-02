// GitHub release lookup and OTA flashing implementation.
#include "firmware_update.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "firmware_version.h"

namespace {
String normalizeVersion(String value) {
  value.trim();
  value.toLowerCase();
  if (value.startsWith("v")) {
    value.remove(0, 1);
  }
  return value;
}

String bytesToHex(const uint8_t *data, size_t len) {
  static const char *kHexChars = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += kHexChars[(data[i] >> 4) & 0xF];
    out += kHexChars[data[i] & 0xF];
  }
  return out;
}

// GitHub's asset "digest" field looks like "sha256:<hex>". Returns the bare
// hex string, or empty if the digest is missing or not a sha256 digest.
String expectedSha256Hex(const String &digest) {
  String value = digest;
  value.trim();
  int sep = value.indexOf(':');
  if (sep >= 0) {
    String algo = value.substring(0, sep);
    algo.toLowerCase();
    if (algo != "sha256") {
      return String();
    }
    value = value.substring(sep + 1);
  }
  value.toLowerCase();
  return value;
}

bool downloadJsonDocument(JsonDocument &doc, const String &url, String &errorMessage) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(20);

  HTTPClient http;
  if (!http.begin(client, url)) {
    errorMessage = "Unable to open GitHub release API";
    return false;
  }

  http.setTimeout(8000);
  http.addHeader("User-Agent", "FRITZ!Box-status-esp32");
  http.addHeader("Accept", "application/vnd.github+json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    if (code == HTTP_CODE_NOT_FOUND) {
      errorMessage = "No GitHub release found yet. Create a tagged release first.";
    } else {
      errorMessage = "GitHub API returned HTTP " + String(code);
    }
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    errorMessage = "Failed to parse GitHub release JSON";
    return false;
  }

  return true;
}

String pickAssetUrl(const JsonArray &assets, String &assetName, size_t &assetSize, String &assetDigest) {
  String fallbackUrl;
  String fallbackName;
  size_t fallbackSize = 0;
  String fallbackDigest;

  for (JsonObjectConst asset : assets) {
    String name = asset["name"] | "";
    String url = asset["browser_download_url"] | "";
    size_t size = asset["size"] | 0;
    String digest = asset["digest"] | "";
    if (url.length() == 0) {
      continue;
    }

    if (fallbackUrl.length() == 0) {
      fallbackUrl = url;
      fallbackName = name;
      fallbackSize = size;
      fallbackDigest = digest;
    }

    String lowerName = name;
    lowerName.toLowerCase();
    // Releases also carry bootloader/partition-table images for the web
    // flasher (which needs them to flash a blank chip); those also end in
    // ".bin" and must not be mistaken for the app firmware here.
    bool isFlasherOnlyAsset = lowerName.indexOf("bootloader") >= 0 ||
                              lowerName.indexOf("partitions") >= 0 ||
                              lowerName.indexOf("boot_app0") >= 0;
    if (lowerName.endsWith(".bin") && !isFlasherOnlyAsset) {
      assetName = name;
      assetSize = size;
      assetDigest = digest;
      return url;
    }
  }

  assetName = fallbackName;
  assetSize = fallbackSize;
  assetDigest = fallbackDigest;
  return fallbackUrl;
}
}  // namespace

bool fetchLatestFirmwareRelease(FirmwareReleaseInfo &info, String &errorMessage) {
  info.currentVersion = kFirmwareVersion;

  JsonDocument doc;
  if (!downloadJsonDocument(doc, kFirmwareGitHubReleaseApi, errorMessage)) {
    info.latestVersion = "unavailable";
    info.releaseName = "GitHub release unavailable";
    info.releaseUrl = kFirmwareGitHubReleasesUrl;
    return false;
  }

  info.latestVersion = doc["tag_name"] | "";
  info.releaseName = doc["name"] | info.latestVersion;
  info.releaseBody = doc["body"] | "";
  info.releaseUrl = doc["html_url"] | kFirmwareGitHubReleasesUrl;
  info.publishedAt = doc["published_at"] | "";

  JsonArray assets = doc["assets"].as<JsonArray>();
  info.assetUrl = pickAssetUrl(assets, info.assetName, info.assetSize, info.assetDigest);
  info.updateAvailable = normalizeVersion(info.currentVersion) != normalizeVersion(info.latestVersion);

  if (info.assetUrl.length() == 0) {
    errorMessage = "No .bin release asset found";
    return false;
  }

  return true;
}

bool flashFirmwareAsset(const FirmwareReleaseInfo &info, String &errorMessage, const FirmwareProgressCallback &progressCallback) {
  if (info.assetUrl.length() == 0) {
    errorMessage = "No firmware download URL available";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, info.assetUrl)) {
    errorMessage = "Unable to open firmware download URL";
    return false;
  }

  http.setTimeout(15000);
  http.addHeader("User-Agent", "FRITZ!Box-status-esp32");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errorMessage = "Firmware download failed with HTTP " + String(code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  size_t totalBytes = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    errorMessage = "Unable to start OTA update";
    http.end();
    return false;
  }

  String expectedHash = expectedSha256Hex(info.assetDigest);
  bool verifyHash = expectedHash.length() == 64;
  mbedtls_sha256_context shaCtx;
  if (verifyHash) {
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts_ret(&shaCtx, 0);
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[2048];
  size_t written = 0;
  unsigned long lastDataMs = millis();

  if (progressCallback) {
    progressCallback(0, totalBytes);
  }

  // Stream chunks from HTTP directly into Update to keep RAM usage low.
  while (http.connected() && (contentLength > 0 || contentLength == -1)) {
    size_t available = stream->available();
    if (available == 0) {
      if ((millis() - lastDataMs) > 15000) {
        errorMessage = "Firmware download timed out";
        if (verifyHash) {
          mbedtls_sha256_free(&shaCtx);
        }
        Update.abort();
        http.end();
        return false;
      }
      delay(1);
      continue;
    }

    size_t toRead = available;
    if (toRead > sizeof(buffer)) {
      toRead = sizeof(buffer);
    }

    int bytesRead = stream->readBytes(buffer, toRead);
    if (bytesRead <= 0) {
      delay(1);
      continue;
    }

    lastDataMs = millis();
    size_t chunkWritten = Update.write(buffer, bytesRead);
    if (chunkWritten != static_cast<size_t>(bytesRead)) {
      errorMessage = "OTA write failed: " + String(Update.errorString());
      if (verifyHash) {
        mbedtls_sha256_free(&shaCtx);
      }
      Update.abort();
      http.end();
      return false;
    }

    if (verifyHash) {
      mbedtls_sha256_update_ret(&shaCtx, buffer, bytesRead);
    }

    written += chunkWritten;
    if (contentLength > 0) {
      contentLength -= bytesRead;
    }

    if (progressCallback) {
      progressCallback(written, totalBytes);
    }
  }

  if (verifyHash) {
    uint8_t digestBytes[32];
    mbedtls_sha256_finish_ret(&shaCtx, digestBytes);
    mbedtls_sha256_free(&shaCtx);
    String computedHash = bytesToHex(digestBytes, sizeof(digestBytes));
    if (computedHash != expectedHash) {
      // Abort instead of end(true): the new partition is discarded and the
      // device keeps booting the currently running firmware.
      Update.abort();
      http.end();
      errorMessage = "Firmware checksum mismatch (SHA256) - update aborted";
      return false;
    }
  }

  bool finished = Update.end(true);
  http.end();

  if (!finished) {
    errorMessage = "OTA update failed: " + String(Update.errorString());
    return false;
  }

  if (written == 0) {
    errorMessage = "OTA update wrote no data";
    return false;
  }

  if (progressCallback) {
    progressCallback(written, totalBytes);
  }

  return true;
}