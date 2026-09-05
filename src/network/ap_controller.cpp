// lib/network/ap_controller.cpp
#include "ap_controller.h"
#include <string.h>

void ApController::buildSsid(char* out, size_t outLen, const char* nodeId) {
  if (!out || outLen == 0) return;
  snprintf(out, outLen, "%s%s", AP_SSID_PREFIX, nodeId ? nodeId : "");
  out[outLen - 1] = '\0';
}

bool ApController::begin(const char* ssid, const char* psk, uint8_t channel) {
  stop();
  // Hidden network (ssid_hidden = 1). Normal M/S nodes broadcast NODE_<id>
  // as a hidden AP; the STA side scans with show_hidden=true to find it.
  if (!WiFi.softAP(ssid, psk ? psk : "", channel, 1, 4)) return false;
  _ip      = WiFi.softAPIP();
  _running = true;
  return true;
}

void ApController::stop() {
  stopDataServer();
  if (_running) {
    // `false` keeps the STA interface alive (a relay must stay connected upstream).
    WiFi.softAPdisconnect(false);
    _running = false;
  }
}

int ApController::clientCount() {
  if (!_running) return 0;
  return WiFi.softAPgetStationNum();
}

bool ApController::beginDataServer(uint16_t port) {
  if (_dataRunning) return true;
  if (!_dataServer) _dataServer = new WiFiServer(port);
  _dataServer->begin();
  _dataRunning = true;
  return true;
}

void ApController::stopDataServer() {
  _dataRunning = false;
  for (auto& c : _clients) {
    if (c) c.stop();
  }
  if (_dataServer) {
    delete _dataServer;
    _dataServer = nullptr;
  }
}

int ApController::readFrame(uint8_t* out, size_t maxLen) {
  if (!_dataServer || !out) return 0;

  // Admit pending connections into a free slot (drop if full).
  if (_dataServer->hasClient()) {
    WiFiClient c = _dataServer->available();
    bool accepted = false;
    for (auto& slot : _clients) {
      if (!slot || !slot.connected()) {
        slot     = c;
        accepted = true;
        break;
      }
    }
    if (!accepted) c.stop();
  }

  for (auto& c : _clients) {
    if (!c || !c.connected()) continue;
    if (c.available() < DP_FRAME_LEN_BYTES) continue;

    uint8_t lenBytes[DP_FRAME_LEN_BYTES];
    if (c.read(lenBytes, DP_FRAME_LEN_BYTES) != DP_FRAME_LEN_BYTES) continue;

    uint16_t len = (uint16_t)lenBytes[0] | ((uint16_t)lenBytes[1] << 8);
    if (len == 0 || len > maxLen || len > DP_MAX_PACKET_SIZE) continue;

    size_t   got   = 0;
    uint32_t start = millis();
    while (got < len && (millis() - start) < 300) {
      int r = c.read(out + got, len - got);
      if (r > 0) got += (size_t)r;
      else delay(2);
    }
    if (got == len) return (int)len;
  }
  return 0;
}

size_t ApController::broadcastFrame(const uint8_t* data, size_t len) {
  if (!data || len == 0 || len > DP_MAX_PACKET_SIZE) return 0;

  uint8_t lenBytes[DP_FRAME_LEN_BYTES] = {
    (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF)
  };

  size_t sent = 0;
  for (auto& c : _clients) {
    if (!c || !c.connected()) continue;
    c.write(lenBytes, DP_FRAME_LEN_BYTES);
    sent += c.write(data, len);
  }
  return sent;
}
