// lib/network/host_link.h
// HostLink — UDP 4210 link to the Windows Qt Host Controller (ESP32QtApp).
//
// Runs in SETUP mode: once the node has joined the fixed setup WiFi (the host's
// Windows Mobile Hotspot, e.g. "ESP32_Host"), this module
//   1. periodically sends a JSON `hello` so the host registers the device
//      (MAC + ID), and
//   2. listens on UDP 4210 for a `set_key` packet and replies `key_ack`
//      (ok / error) to the sender's IP/port.
// The received chunk_key is persisted to NVS via ConfigStore::saveChunkKey().
//
// Implemented on the built-in WiFiUDP (polled from loop()); no extra library.
// Protocol (UTF-8 JSON, no trailing newline) — see ESP32QtApp README.
#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#include "../config/config_store.h"

#define HOSTLINK_PORT     4210
#define HOSTLINK_HELLO_MS 5000      // hello period while STA is connected
#define HOSTLINK_KEY_MAX    63      // max chunk_key length we accept
#define HOSTLINK_RX_BUF     1024

class HostLink {
public:
  // nodeId: device ID sent in hello (e.g. "00000001"); firmware: version string.
  void begin(const char* nodeId, const char* firmware);
  void stop();
  void loop();                       // call from setup-mode loop handler

private:
  void sendHello();
  void handleSetKey(IPAddress fromIP, uint16_t fromPort, StaticJsonDocument<512>& doc);
  void replyAck(IPAddress dst, uint16_t port, const char* msgId,
                const char* status, const char* errorCode, const char* message);

  WiFiUDP       _udp;
  char          _nodeId[DP_NODEID_LEN + 1] = {0};
  const char*   _firmware = "1.0.0";
  uint32_t      _lastHelloMs = 0;
  bool          _helloSent = false;
};
