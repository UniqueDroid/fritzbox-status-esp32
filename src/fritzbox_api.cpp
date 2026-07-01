// FRITZ!Box TR-064/UPnP API client for gateway/system metrics and traffic samples.
#include "fritzbox_api.h"
#include "utils.h"
#include "globals.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {

// Extract text content between XML tags
String extractXmlValue(const String& xml, const String& tagName) {
  String openTag = "<" + tagName + ">";
  String closeTag = "</" + tagName + ">";
  
  int start = xml.indexOf(openTag);
  if (start < 0) return "";
  
  start += openTag.length();
  int end = xml.indexOf(closeTag, start);
  if (end < 0) return "";
  
  return xml.substring(start, end);
}

// Send SOAP request to TR-064 endpoint
int soapRequest(const String& serviceType, const String& action, 
                const String& requestXml, String& responseXml) {
  
  const String host = String(fritzBoxHost);
  
  // Try HTTPS first (port 49443)
  {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(15);
    
    HTTPClient http;
    String url = String("https://") + host + ":49443/upnp/control/" + serviceType;
    
    if (http.begin(secureClient, url)) {
      // Build SOAP envelope
      String soapEnv = String("<?xml version=\"1.0\" encoding=\"utf-8\"?>")
        + "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        + "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        + "<s:Body>"
        + "<u:" + action + " xmlns:u=\"urn:schemas-upnp-org:service:" + serviceType + ":1\">"
        + requestXml
        + "</u:" + action + ">"
        + "</s:Body>"
        + "</s:Envelope>";
      
      http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
      http.addHeader("SOAPAction", String("urn:schemas-upnp-org:service:") + serviceType + ":1#" + action);
      
      int code = http.POST(soapEnv);
      if (code == HTTP_CODE_OK) {
        responseXml = http.getString();
        http.end();
        return code;
      }
      http.end();
      if (code > 0) return code;
    }
  }
  
  // Fallback to HTTP (port 49000)
  {
    WiFiClient plainClient;
    HTTPClient http;
    String url = String("http://") + host + ":49000/upnp/control/" + serviceType;
    
    if (http.begin(plainClient, url)) {
      String soapEnv = String("<?xml version=\"1.0\" encoding=\"utf-8\"?>")
        + "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        + "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        + "<s:Body>"
        + "<u:" + action + " xmlns:u=\"urn:schemas-upnp-org:service:" + serviceType + ":1\">"
        + requestXml
        + "</u:" + action + ">"
        + "</s:Body>"
        + "</s:Envelope>";
      
      http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
      http.addHeader("SOAPAction", String("urn:schemas-upnp-org:service:") + serviceType + ":1#" + action);
      
      int code = http.POST(soapEnv);
      if (code == HTTP_CODE_OK) {
        responseXml = http.getString();
      }
      http.end();
      return code;
    }
  }
  
  return -1;
}

}  // namespace

bool parseGateway(const String &json) {
  // For TR-064, this function is kept for compatibility but not used
  // SOAP responses are parsed directly in fetchGatewayStatus()
  return true;
}

void fetchGatewayStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    wanStatus = "NO WIFI";
    wanDelay = "-";
    wanRttSd = "-";
    wanLoss = "-";
    return;
  }

  String response;
  int code = soapRequest("WANIPConnection", "GetStatusInfo", "", response);
  
  if (code != HTTP_CODE_OK) {
    wanStatus = String("HTTP ") + code;
    wanDelay = "-";
    wanRttSd = "-";
    wanLoss = "-";
    return;
  }

  // Parse SOAP response
  String connectionStatus = extractXmlValue(response, "NewConnectionStatus");
  if (connectionStatus.length() == 0) {
    wanStatus = "PARSE ERR";
    wanDelay = "-";
    wanRttSd = "-";
    wanLoss = "-";
    return;
  }

  wanStatus = connectionStatus;
  
  // TR-064 doesn't provide delay/RTT/loss in GetStatusInfo
  // These would need external probing or ICMP ping
  wanDelay = "-";
  wanRttSd = "-";
  wanLoss = "-";
}

void fetchWanTraffic() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  String responseIn, responseOut;
  
  // Get bytes received via GetTotalBytesReceived
  int codeIn = soapRequest("WANCommonInterfaceConfig", "GetTotalBytesReceived", "", responseIn);
  if (codeIn != HTTP_CODE_OK) {
    return;
  }
  
  String bytesRecStr = extractXmlValue(responseIn, "NewTotalBytesReceived");
  if (bytesRecStr.length() == 0) {
    return;
  }
  
  // Get bytes sent via GetTotalBytesSent
  int codeOut = soapRequest("WANCommonInterfaceConfig", "GetTotalBytesSent", "", responseOut);
  if (codeOut != HTTP_CODE_OK) {
    return;
  }
  
  String bytesSentStr = extractXmlValue(responseOut, "NewTotalBytesSent");
  if (bytesSentStr.length() == 0) {
    return;
  }
  
  // Parse byte values (may contain very large numbers)
  uint64_t bytesRec = 0;
  uint64_t bytesSent = 0;
  
  // Simple parsing for uint64
  for (int i = 0; i < bytesRecStr.length(); i++) {
    if (isDigit(bytesRecStr[i])) {
      bytesRec = bytesRec * 10 + (bytesRecStr[i] - '0');
    }
  }
  
  for (int i = 0; i < bytesSentStr.length(); i++) {
    if (isDigit(bytesSentStr[i])) {
      bytesSent = bytesSent * 10 + (bytesSentStr[i] - '0');
    }
  }
  
  uint32_t nowMs = millis();
  if (wanTrafficPrimed && nowMs > wanPrevSampleMs && bytesRec >= wanPrevInBytes && bytesSent >= wanPrevOutBytes) {
    float dt = (nowMs - wanPrevSampleMs) / 1000.0f;
    if (dt > 0.1f) {
      float rxKbps = ((bytesRec - wanPrevInBytes) * 8.0f) / (dt * 1000.0f);
      float txKbps = ((bytesSent - wanPrevOutBytes) * 8.0f) / (dt * 1000.0f);
      pushTrafficSample(rxKbps, txKbps);
    }
  }
  wanPrevInBytes = bytesRec;
  wanPrevOutBytes = bytesSent;
  wanPrevSampleMs = nowMs;
  wanTrafficPrimed = true;
}

void fetchSystemStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    cpuPercent = 0;
    memPercent = 0;
    tempPercent = 0;
    tempValue = "-";
    return;
  }

  String response;
  int code = soapRequest("DeviceInfo", "GetInfo", "", response);
  
  if (code != HTTP_CODE_OK) {
    cpuPercent = 0;
    memPercent = 0;
    tempPercent = 0;
    tempValue = "-";
    return;
  }

  // TR-064 DeviceInfo service typically doesn't provide CPU/memory/temp
  // These metrics are not part of standard TR-064 spec
  // Set to defaults or implement extended services if available
  
  cpuPercent = 0;
  memPercent = 0;
  tempPercent = 0;
  tempValue = "-";
}
