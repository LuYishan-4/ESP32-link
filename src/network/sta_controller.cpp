// lib/network/sta_controller.cpp
#include "sta_controller.h"
#include <string.h>

static bool matchesTarget(const String& ssid, const char* targetId) {
  char expected[33];
  snprintf(expected, sizeof(expected), "%s%s", AP_SSID_PREFIX, targetId);
  return ssid == expected;
}

void StaController::begin(const char* targetId, const char* psk) {
  strlcpy(_targetId, targetId ? targetId : "", sizeof(_targetId));
  strlcpy(_psk, psk ? psk : "", sizeof(_psk));
  _configured = true;
  _connected  = false;
  _rssi       = 0;
  _ip         = IPAddress(0, 0, 0, 0);
}

void StaController::stop() {
  _connected = false;
  if (_dataClient) _dataClient.stop();
  WiFi.disconnect(true);
}

int StaController::scanMatching(ScanEntry* out, int maxEntries) {
  int found = 0;
  int n = WiFi.scanNetworks(false, true, false, 300);
  if (n <= 0) return 0;

  for (int i = 0; i < n && found < maxEntries; ++i) {
    String ssid = WiFi.SSID(i);
    if (!matchesTarget(ssid, _targetId)) continue;
    if (out) {
      memset(&out[found], 0, sizeof(ScanEntry));
      strlcpy(out[found].ssid, ssid.c_str(), sizeof(out[found].ssid));
      memcpy(out[found].bssid, WiFi.BSSID(i), 6);
      out[found].rssi = WiFi.RSSI(i);
    }
    found++;
  }
  WiFi.scanDelete();
  return found;
}

bool StaController::connectToBestParent() {
  ScanEntry entries[STA_MAX_SCAN_RESULTS];
  int n = scanMatching(entries, STA_MAX_SCAN_RESULTS);
  if (n == 0) return false;

  int best = 0;
  for (int i = 1; i < n; ++i) {
    if (entries[i].rssi > entries[best].rssi) best = i;
  }

  WiFi.disconnect(false);
  WiFi.mode(WIFI_AP_STA);   // AP_STA so a relay can still host its own AP
  WiFi.begin(entries[best].ssid, _psk);
  return true;
}

bool StaController::forceReconnect() {
  _backoffTicks  = 0;
  _lastAttemptMs = 0;
  return connectToBestParent();
}

void StaController::loop() {
  if (!_configured) return;

  wl_status_t st = WiFi.status();
  bool nowConnected = (st == WL_CONNECTED);

  if (nowConnected && !_connected) {
    _connected    = true;
    _rssi         = WiFi.RSSI();
    _ip           = WiFi.localIP();
    _backoffTicks = 0;
    connectDataSocket();
  } else if (!nowConnected && _connected) {
    _connected = false;
    _rssi      = 0;
    _ip        = IPAddress(0, 0, 0, 0);
    if (_dataClient) _dataClient.stop();
  }

  if (!_connected) {
    uint32_t now = millis();
    uint32_t delayMs = 2000UL << _backoffTicks;
    if (_lastAttemptMs == 0 || (now - _lastAttemptMs) > delayMs) {
      _lastAttemptMs = now;
      if (!connectToBestParent()) {
        if (_backoffTicks < 8) _backoffTicks++;
      }
    }
    return;
  }

  // Keep the upstream data socket alive.
  if (!_dataClient || !_dataClient.connected()) {
    connectDataSocket();
  }
}

bool StaController::connectDataSocket(uint16_t port) {
  if (_dataClient) _dataClient.stop();
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return false;
  if (!_dataClient.connect(gw, port, 2000)) return false;
  _dataClient.setNoDelay(true);
  return true;
}

size_t StaController::sendFrame(const uint8_t* data, size_t len) {
  if (!_dataClient || !_dataClient.connected()) return 0;
  if (!data || len == 0 || len > DP_MAX_PACKET_SIZE) return 0;

  uint8_t lenBytes[DP_FRAME_LEN_BYTES] = {
    (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF)
  };
  _dataClient.write(lenBytes, DP_FRAME_LEN_BYTES);
  size_t sent = _dataClient.write(data, len);
  _dataClient.flush();
  return sent;
}

IPAddress StaController::parentIp() const {
  return WiFi.gatewayIP();
}
