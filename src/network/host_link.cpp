// lib/network/host_link.cpp
#include "host_link.h"
#include <WiFi.h>

void HostLink::begin(const char* nodeId, const char* firmware) {
  if (nodeId)   strlcpy(_nodeId, nodeId, sizeof(_nodeId));
  if (firmware) _firmware = firmware;
  _lastHelloMs = 0;
  _helloSent   = false;

  if (_udp.begin(HOSTLINK_PORT)) {
    Serial.printf("[host] UDP %d listener up\n", HOSTLINK_PORT);
  } else {
    Serial.printf("[host] UDP %d listen FAILED\n", HOSTLINK_PORT);
  }
}

void HostLink::stop() {
  _udp.stop();
}

void HostLink::loop() {
  uint32_t now = millis();

  // 1) periodic hello once we have a STA link to the host network
  if (WiFi.status() == WL_CONNECTED) {
    if (now - _lastHelloMs >= HOSTLINK_HELLO_MS) {
      _lastHelloMs = now;
      sendHello();
    }
  } else {
    _helloSent = false;
  }

  // 2) receive incoming datagrams (set_key)
  int len = _udp.parsePacket();
  if (len > 0) {
    char buf[HOSTLINK_RX_BUF];
    int n = _udp.read(buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      StaticJsonDocument<512> doc;
      if (deserializeJson(doc, buf) == DeserializationError::Ok) {
        const char* type = doc["type"] | "";
        if (strcmp(type, "set_key") == 0) {
          handleSetKey(_udp.remoteIP(), _udp.remotePort(), doc);
        }
      }
    }
  }
}

void HostLink::sendHello() {
  StaticJsonDocument<320> doc;
  doc["protocol"] = "esp32-control";
  doc["version"]  = 1;
  doc["type"]     = "hello";
  doc["mac"]      = WiFi.macAddress();
  doc["id"]       = _nodeId;
  doc["firmware"] = _firmware;

  char buf[320];
  size_t len = serializeJson(doc, buf, sizeof(buf));

  // The Windows host sits on the STA gateway of the hotspot adapter, so a
  // unicast to the gateway IP is the most reliable way to reach it.
  IPAddress gw = WiFi.gatewayIP();
  if (gw[0] == 0) return;                       // no route yet
  _udp.beginPacket(gw, HOSTLINK_PORT);
  _udp.write((const uint8_t*)buf, len);
  _udp.endPacket();
  if (!_helloSent) {
    _helloSent = true;
    Serial.printf("[host] hello -> %s (mac=%s id=%s)\n",
                  gw.toString().c_str(), WiFi.macAddress().c_str(), _nodeId);
  }
}

void HostLink::handleSetKey(IPAddress fromIP, uint16_t fromPort,
                            StaticJsonDocument<512>& doc) {
  const char* msgId = doc["message_id"] | "";
  const char* chunk = doc["chunk_key"] | "";
  size_t klen = strlen(chunk);

  Serial.printf("[host] set_key from %s:%u (msg %s, key len %u)\n",
                fromIP.toString().c_str(), fromPort, msgId, (unsigned)klen);

  if (klen == 0 || klen > HOSTLINK_KEY_MAX) {
    replyAck(fromIP, fromPort, msgId, "error", "KEY_INVALID",
             "key empty or too long");
    return;
  }
  if (!ConfigStore::saveChunkKey(chunk)) {
    replyAck(fromIP, fromPort, msgId, "error", "FLASH_WRITE_FAIL",
             "flash write failed");
    return;
  }
  Serial.printf("[host] chunk_key persisted (len %u)\n", (unsigned)klen);
  replyAck(fromIP, fromPort, msgId, "ok", nullptr, "key received");
}

void HostLink::replyAck(IPAddress dst, uint16_t port, const char* msgId,
                        const char* status, const char* errorCode,
                        const char* message) {
  StaticJsonDocument<384> doc;
  doc["protocol"]   = "esp32-control";
  doc["version"]    = 1;
  doc["type"]       = "key_ack";
  doc["message_id"] = msgId;
  doc["mac"]        = WiFi.macAddress();
  doc["id"]         = _nodeId;
  doc["status"]     = status;
  if (errorCode && errorCode[0]) doc["error_code"] = errorCode;
  if (message && message[0])     doc["message"]     = message;

  char buf[384];
  size_t len = serializeJson(doc, buf, sizeof(buf));
  _udp.beginPacket(dst, port);
  _udp.write((const uint8_t*)buf, len);
  _udp.endPacket();
  Serial.printf("[host] key_ack(%s) -> %s:%u\n", status,
                dst.toString().c_str(), port);
}
