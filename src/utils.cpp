// Shared parsing and normalization helpers for API payloads and traffic data.
#include "utils.h"
#include "globals.h"

String pickString(JsonVariantConst v, const String &fallback) {
  if (v.isNull()) {
    return fallback;
  }
  String s = v.as<String>();
  s.trim();
  if (s.length() == 0 || s == "null") {
    return fallback;
  }
  return s;
}

int percentFromVariant(JsonVariantConst v) {
  if (v.isNull()) {
    return -1;
  }
  String s = v.as<String>();
  s.trim();
  s.replace("%", "");
  if (s.length() == 0 || s == "null") {
    return -1;
  }
  float val = s.toFloat();
  if (val < 0.0f) {
    return -1;
  }
  return constrain((int)val, 0, 100);
}

int findPercentRecursive(JsonVariantConst node, const char *const *keys, size_t keyCount) {
  if (node.is<JsonObjectConst>()) {
    JsonObjectConst obj = node.as<JsonObjectConst>();
    for (size_t i = 0; i < keyCount; ++i) {
      JsonVariantConst candidate = obj[keys[i]];
      int percent = percentFromVariant(candidate);
      if (percent >= 0) {
        return percent;
      }
    }
    for (JsonPairConst kv : obj) {
      int percent = findPercentRecursive(kv.value(), keys, keyCount);
      if (percent >= 0) {
        return percent;
      }
    }
  } else if (node.is<JsonArrayConst>()) {
    JsonArrayConst arr = node.as<JsonArrayConst>();
    for (JsonVariantConst item : arr) {
      int percent = findPercentRecursive(item, keys, keyCount);
      if (percent >= 0) {
        return percent;
      }
    }
  }
  return -1;
}

String findStringRecursive(JsonVariantConst node, const char *const *keys, size_t keyCount) {
  if (node.is<JsonObjectConst>()) {
    JsonObjectConst obj = node.as<JsonObjectConst>();
    for (size_t i = 0; i < keyCount; ++i) {
      String s = pickString(obj[keys[i]], "-");
      if (s != "-") {
        return s;
      }
    }
    for (JsonPairConst kv : obj) {
      String s = findStringRecursive(kv.value(), keys, keyCount);
      if (s != "-") {
        return s;
      }
    }
  } else if (node.is<JsonArrayConst>()) {
    JsonArrayConst arr = node.as<JsonArrayConst>();
    for (JsonVariantConst item : arr) {
      String s = findStringRecursive(item, keys, keyCount);
      if (s != "-") {
        return s;
      }
    }
  }
  return "-";
}

int temperatureToPercent(const String &tempRaw) {
  String s = tempRaw;
  s.trim();
  if (s.length() == 0 || s == "-") {
    return -1;
  }
  bool isF = (s.indexOf("F") >= 0 || s.indexOf("f") >= 0);
  s.replace("C", "");
  s.replace("c", "");
  s.replace("F", "");
  s.replace("f", "");
  s.replace(" ", "");
  float temp = s.toFloat();
  if (temp <= -100.0f || temp > 300.0f) {
    return -1;
  }
  if (isF) {
    temp = (temp - 32.0f) * (5.0f / 9.0f);
  }
  return constrain((int)temp, 0, 100);
}

float floatFromVariant(JsonVariantConst v) {
  if (v.isNull()) {
    return -1.0f;
  }
  String s = v.as<String>();
  s.trim();
  if (s.length() == 0 || s == "null") {
    return -1.0f;
  }
  return s.toFloat();
}

uint64_t uint64FromVariant(JsonVariantConst v) {
  if (v.isNull()) {
    return 0;
  }
  String s = v.as<String>();
  s.trim();
  s.replace(",", ".");
  if (s.length() == 0 || s == "null") {
    return 0;
  }
  bool hasUnit = false;
  for (size_t i = 0; i < s.length(); ++i) {
    if ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')) {
      hasUnit = true;
      break;
    }
  }
  if (!hasUnit) {
    return strtoull(s.c_str(), nullptr, 10);
  }
  String numStr = "";
  String unitStr = "";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if ((c >= '0' && c <= '9') || c == '.') {
      if (unitStr.length() == 0) numStr += c;
    } else if (c != ' ') {
      unitStr += c;
    }
  }
  if (numStr.length() == 0) {
    return 0;
  }
  float value = numStr.toFloat();
  String u = unitStr;
  u.toLowerCase();
  double mult = 1.0;
  if (u == "b" || u == "byte" || u == "bytes") {
    mult = 1.0;
  } else if (u == "kb" || u == "k") {
    mult = 1000.0;
  } else if (u == "kib") {
    mult = 1024.0;
  } else if (u == "mb" || u == "m") {
    mult = 1000.0 * 1000.0;
  } else if (u == "mib") {
    mult = 1024.0 * 1024.0;
  } else if (u == "gb" || u == "g") {
    mult = 1000.0 * 1000.0 * 1000.0;
  } else if (u == "gib") {
    mult = 1024.0 * 1024.0 * 1024.0;
  } else if (u == "tb" || u == "t") {
    mult = 1000.0 * 1000.0 * 1000.0 * 1000.0;
  } else if (u == "tib") {
    mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
  }
  double bytes = value * mult;
  if (bytes < 0.0) {
    return 0;
  }
  return (uint64_t)bytes;
}

bool containsIgnoreCase(String haystack, String needle) {
  haystack.toLowerCase();
  needle.toLowerCase();
  return haystack.indexOf(needle) >= 0;
}

void pushTrafficSample(float rxKbps, float txKbps) {
  for (int i = 0; i < kTrafficPoints - 1; ++i) {
    wanRxHistory[i] = wanRxHistory[i + 1];
    wanTxHistory[i] = wanTxHistory[i + 1];
  }
  wanRxHistory[kTrafficPoints - 1] = max(0.0f, rxKbps);
  wanTxHistory[kTrafficPoints - 1] = max(0.0f, txKbps);
}

bool looksLikeRttValue(const String &value) {
  String s = value;
  s.trim();
  if (s == "-" || s.length() == 0) {
    return false;
  }
  String numeric = s;
  numeric.replace("ms", "");
  numeric.replace("MS", "");
  numeric.replace(" ", "");
  float ms = numeric.toFloat();
  return ms > 0.0f && ms < 2000.0f;
}

String normalizeRttOneDecimal(const String &value) {
  String s = value;
  s.trim();
  s.replace("ms", "");
  s.replace("MS", "");
  s.replace(" ", "");
  float ms = s.toFloat();
  if (ms <= 0.0f) {
    return "-";
  }
  return String(ms, 1) + "ms";
}
