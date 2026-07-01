#pragma once

// Reusable helpers for parsing FRITZ!Box payloads and normalizing display values.
#include "globals.h"

// Helper functions for JSON parsing
String pickString(JsonVariantConst v, const String &fallback = "-");
int percentFromVariant(JsonVariantConst v);
int findPercentRecursive(JsonVariantConst node, const char *const *keys, size_t keyCount);
String findStringRecursive(JsonVariantConst node, const char *const *keys, size_t keyCount);
int temperatureToPercent(const String &tempRaw);
float floatFromVariant(JsonVariantConst v);
uint64_t uint64FromVariant(JsonVariantConst v);
bool containsIgnoreCase(String haystack, String needle);

// Traffic helpers
void pushTrafficSample(float rxKbps, float txKbps);
bool looksLikeRttValue(const String &value);
String normalizeRttOneDecimal(const String &value);
