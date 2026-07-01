#pragma once

// Build/version and repository constants used by OTA and UI paths.
#if __has_include("build_version.h")
#include "build_version.h"
#endif

#ifdef FW_VERSION
static constexpr const char kFirmwareVersion[] = FW_VERSION;
#elif defined(BUILD_VERSION)
static constexpr const char kFirmwareVersion[] = BUILD_VERSION;
#else
static constexpr const char kFirmwareVersion[] = "dev";
#endif
static constexpr const char kFirmwareGitHubOwner[] = "UniqueDroid";
static constexpr const char kFirmwareGitHubRepo[] = "fritzbox-status-esp32";
static constexpr const char kFirmwareGitHubReleaseApi[] = "https://api.github.com/repos/UniqueDroid/fritzbox-status-esp32/releases/latest";
static constexpr const char kFirmwareGitHubReleasesUrl[] = "https://github.com/UniqueDroid/fritzbox-status-esp32/releases";
static constexpr const char kFirmwareGitHubLogoRawUrl[] = "https://raw.githubusercontent.com/UniqueDroid/fritzbox-status-esp32/main/src/fritzbox-status-logo.png";