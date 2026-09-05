// lib/mqtt/mqtt_ws.h
// Minimal MQTT 3.1.1 client that runs over a WebSocket (WSS) transport —
// i.e. `wss://<host>:<port>/<path>` as required by the Cloudflare-tunneled
// host backend. TLS is handled by WiFiClientSecure (Cloudflare public-CA
// bundle, or an explicit CA via setCACert).
//
// Only what the host spec needs is implemented in this first pass:
//   * CONNECT (client id + username/password + keepalive)
//   * PUBLISH (QoS 1)  -> waits for PUBACK
//   * PINGREQ / PINGRESP keepalive
//   * WebSocket masking + binary frames, ping/pong/close handling
#pragma once

#include <Arduino.h>
#include <functional>
#include <WiFiClientSecure.h>

class MqttWs {
public:
  // Called when a QoS1 PUBACK (or QoS0 send) completes. ok=true when the
  // broker acknowledged the packet id.
  using AckCallback = std::function<void(uint16_t packetId, bool ok)>;

  MqttWs();
  ~MqttWs();

  // WebSocket endpoint (use wss:// form on the caller side: host/port/path).
  void begin(const char* host, uint16_t port, const char* path = "/");
  void setAuth(const char* clientId, const char* username, const char* password);
  // nullptr (default) -> verify against the bundled GTS Root R4 CA.
  void setCACert(const char* caPem);
  // Opt out of certificate verification (local testing only). Never for prod.
  void setInsecureMode() { _insecure = true; }

  // Establish TLS + WebSocket handshake + MQTT CONNECT. Returns true when the
  // broker replied CONNACK success.
  bool connect(uint32_t timeoutMs = 10000);
  void disconnect();
  bool connected();

  // Publish a QoS 1 message. Blocks until PUBACK or timeout. Returns true on ack.
  bool publish(const char* topic, const uint8_t* payload, size_t len);
  bool publish(const char* topic, const String& payload);

  // Pump the socket: reads incoming WS/MQTT frames (pings, PUBACKs). Call
  // regularly (e.g. every loop) while connected.
  void loop();

  void setAckCallback(AckCallback cb) { _ack = cb; }
  String endpoint() const {
    return String("wss://") + _host + ":" + _port + _path;
  }

private:
  // --- WebSocket level ---
  bool wsHandshake();
  bool wsSend(uint8_t opcode, const uint8_t* data, size_t len);
  // Reads one WebSocket frame body (opcode+payload). Returns false on error/EOF.
  bool wsRecv(uint8_t* out, size_t max, size_t* got, uint8_t* opcode);

  // --- MQTT level (payloads travel inside WS binary frames) ---
  bool mqttConnect();
  bool mqttSendConnect();
  bool mqttSendPublish(uint16_t packetId, const char* topic,
                       const uint8_t* payload, size_t len);
  bool mqttSendPing();
  // Wait for + parse one inbound MQTT packet from the WS stream.
  bool mqttWaitPacket(uint32_t timeoutMs, bool* handled);
  // Wait specifically for CONNACK/PUBACK/PINGRESP.
  bool mqttWaitFor(uint8_t wantType, uint32_t timeoutMs);

  bool writeAll(const uint8_t* data, size_t len);
  int  readByte(uint32_t timeoutMs);
  bool readN(uint8_t* out, size_t n, uint32_t timeoutMs);

  String _host;
  uint16_t _port = 443;
  String _path = "/";
  String _clientId = "esp32-master";
  String _username;
  String _password;
  const char* _ca = nullptr;
  bool _insecure = false;

  WiFiClientSecure _tls;
  bool _connected = false;
  bool _wsOpen    = false;

  AckCallback _ack;
  uint16_t _packetId = 0;
};
