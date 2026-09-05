// lib/network/ap_controller.h
// SoftAP control: SSID construction from the unified NODE_ prefix + ID,
// plus the node-to-node TCP data server.
#pragma once

#include <WiFi.h>
#include <stdint.h>
#include <stddef.h>
#include "lib/datapacket/dp_common.h"
#include "lib/config/config.h"

#define AP_MAX_DATA_CLIENTS 4

class ApController {
public:
  bool begin(const char* ssid, const char* psk, uint8_t channel = 1);
  void stop();

  bool isRunning() const { return _running; }
  int  clientCount();
  IPAddress ip() const { return _ip; }

  bool   beginDataServer(uint16_t port = DP_DATA_PORT);
  void   stopDataServer();
  int    readFrame(uint8_t* out, size_t maxLen);           // returns packet length or 0
  size_t broadcastFrame(const uint8_t* data, size_t len);  // frames + writes to all clients

  // Build "NODE_<nodeId>" into `out`.
  static void buildSsid(char* out, size_t outLen, const char* nodeId);

private:
  bool        _running     = false;
  bool        _dataRunning = false;
  IPAddress   _ip;
  WiFiServer* _dataServer  = nullptr;
  WiFiClient  _clients[AP_MAX_DATA_CLIENTS];
};
