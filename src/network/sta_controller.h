// lib/network/sta_controller.h
// Wi-Fi STA control: scan for NODE_<targetId>, connect with backoff, and
// maintain the upstream node-to-node TCP data socket.
#pragma once

#include <WiFi.h>
#include <stdint.h>
#include <stddef.h>
#include "../datapacket/dp_common.h"
#include "../config/config.h"

#define STA_MAX_SCAN_RESULTS 16

struct ScanEntry {
  char    ssid[33];
  uint8_t bssid[6];
  int8_t  rssi;
};

class StaController {
public:
  void begin(const char* targetId, const char* psk);
  void loop();
  void stop();

  bool      isConnected() const { return _connected; }
  int8_t    rssi() const { return _rssi; }
  IPAddress ip() const { return _ip; }
  IPAddress parentIp() const;

  int  scanMatching(ScanEntry* out, int maxEntries);  // filters NODE_<targetId>
  bool connectToBestParent();
  bool forceReconnect();

  bool   connectDataSocket(uint16_t port = DP_DATA_PORT);
  bool   dataConnected() { return _dataClient && _dataClient.connected(); }
  size_t sendFrame(const uint8_t* data, size_t len);  // frames + writes upstream

private:
  bool _configured = false;
  bool _connected  = false;
  int8_t _rssi     = 0;
  IPAddress _ip;

  char _targetId[DP_NODEID_LEN + 1] = {0};
  char _psk[DP_AP_PSK_LEN + 1]      = {0};

  WiFiClient _dataClient;

  uint32_t _lastScanMs    = 0;
  uint32_t _lastAttemptMs = 0;
  uint8_t  _backoffTicks  = 0;
};
