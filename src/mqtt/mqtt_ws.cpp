// lib/mqtt/mqtt_ws.cpp
#include "mqtt_ws.h"

#include <esp_system.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "../config/certs.h"

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
namespace {

const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(const uint8_t* data, size_t len) {
  String out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)data[i] << 16;
    if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) n |= data[i + 2];
    out += B64[(n >> 18) & 63];
    out += B64[(n >> 12) & 63];
    out += (i + 1 < len) ? B64[(n >> 6) & 63] : '=';
    out += (i + 2 < len) ? B64[n & 63] : '=';
  }
  return out;
}

// Encode MQTT "remaining length" (variable byte integer).
bool mqttVarInt(uint8_t* out, size_t* o, size_t value) {
  do {
    uint8_t b = value % 128;
    value /= 128;
    if (value) b |= 0x80;
    out[(*o)++] = b;
  } while (value);
  return true;
}

size_t mqttLenBytes(size_t value) {
  size_t n = 0;
  do { n++; value /= 128; } while (value);
  return n;
}

// Decode a 16-bit big-endian from buffer.
uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

} // namespace

// ---------------------------------------------------------------------------
MqttWs::MqttWs() { _packetId = (uint16_t)esp_random(); }
MqttWs::~MqttWs() { disconnect(); }

void MqttWs::begin(const char* host, uint16_t port, const char* path) {
  _host = host ? host : "";
  _port = port ? port : 443;
  _path = (path && path[0]) ? path : "/";
}

void MqttWs::setAuth(const char* clientId, const char* username, const char* password) {
  if (clientId)  _clientId = clientId;
  if (username)  _username = username;
  if (password)  _password = password;
}

void MqttWs::setCACert(const char* caPem) { _ca = caPem; }

void MqttWs::disconnect() {
  _wsOpen = false;
  _connected = false;
  if (_tls.connected()) {
    _tls.stop();
  }
}

bool MqttWs::connected() { return _connected && _tls.connected(); }

bool MqttWs::writeAll(const uint8_t* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    size_t w = _tls.write(data + sent, len - sent);
    if (w == 0 || w == (size_t)-1) {
      if (!_tls.connected()) return false;
      delay(2);
      if (sent == 0 && millis() % 1000 == 0) return false; // safety
    } else {
      sent += w;
    }
  }
  return true;
}

int MqttWs::readByte(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!_tls.available()) {
    if (!_tls.connected()) return -1;
    if (millis() - start >= timeoutMs) return -1;
    delay(1);
  }
  return _tls.read();
}

bool MqttWs::readN(uint8_t* out, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  while (got < n) {
    int b = readByte(timeoutMs);
    if (b < 0) return false;
    out[got++] = (uint8_t)b;
  }
  return true;
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
bool MqttWs::wsHandshake() {
  uint8_t keyRaw[16];
  for (int i = 0; i < 16; i++) keyRaw[i] = (uint8_t)esp_random();
  const String key = base64Encode(keyRaw, sizeof(keyRaw));

  String req;
  req.reserve(256);
  req += "GET " + _path + " HTTP/1.1\r\n";
  req += "Host: " + _host + "\r\n";
  req += "Upgrade: websocket\r\n";
  req += "Connection: Upgrade\r\n";
  req += "Sec-WebSocket-Key: " + key + "\r\n";
  req += "Sec-WebSocket-Version: 13\r\n";
  req += "Sec-WebSocket-Protocol: mqtt\r\n";   // MQTT-over-WebSocket subprotocol
  req += "\r\n";

  if (!writeAll((const uint8_t*)req.c_str(), req.length())) return false;

  // Read HTTP response headers until blank line.
  String head;
  head.reserve(512);
  uint32_t start = millis();
  while (head.indexOf("\r\n\r\n") < 0) {
    int b = readByte(5000);
    if (b < 0) return false;
    head += (char)b;
    if (head.length() > 1024) return false;
    if (millis() - start > 10000) return false;
  }
  if (head.indexOf(" 101 ") < 0 && head.indexOf("101 ") < 0) {
    Serial.printf("[mqtt] WS handshake fail. response: %.*s\n",
                  head.length() > 240 ? 240 : (int)head.length(), head.c_str());
    return false;
  }
  _wsOpen = true;
  return true;
}

bool MqttWs::wsSend(uint8_t opcode, const uint8_t* data, size_t len) {
  uint8_t hdr[14];
  size_t o = 0;
  hdr[o++] = 0x80 | opcode;                    // FIN + opcode
  uint8_t mask[4] = { (uint8_t)esp_random(), (uint8_t)esp_random(),
                      (uint8_t)esp_random(), (uint8_t)esp_random() };
  // Client frames MUST be masked.
  if (len <= 125) {
    hdr[o++] = 0x80 | (uint8_t)len;
  } else if (len <= 0xFFFF) {
    hdr[o++] = 0x80 | 126;
    hdr[o++] = (uint8_t)(len >> 8);
    hdr[o++] = (uint8_t)(len & 0xFF);
  } else {
    hdr[o++] = 0x80 | 127;
    uint64_t v = len;
    for (int i = 7; i >= 0; i--) hdr[o++] = (uint8_t)(v >> (i * 8));
  }
  hdr[o++] = mask[0]; hdr[o++] = mask[1]; hdr[o++] = mask[2]; hdr[o++] = mask[3];
  if (!writeAll(hdr, o)) return false;

  // Mask the payload.
  if (len) {
    for (size_t i = 0; i < len; i++) {
      uint8_t m = data[i] ^ mask[i & 3];
      if (!_tls.write(&m, 1)) return false;
    }
  }
  return true;
}

bool MqttWs::wsRecv(uint8_t* out, size_t max, size_t* got, uint8_t* opcode) {
  *got = 0;
  uint8_t h0, h1;
  if (!readN(&h0, 1, 60000)) return false;
  if (!readN(&h1, 1, 60000)) return false;
  const uint8_t op = h0 & 0x0F;
  const bool masked = (h1 & 0x80) != 0;
  if (opcode) *opcode = op;

  size_t len = h1 & 0x7F;
  if (len == 126) {
    uint8_t ext[2]; if (!readN(ext, 2, 60000)) return false;
    len = ((size_t)ext[0] << 8) | ext[1];
  } else if (len == 127) {
    uint8_t ext[8]; if (!readN(ext, 8, 60000)) return false;
    len = 0;
    for (int i = 0; i < 8; i++) len = (len << 8) | ext[i];
  }

  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked) { if (!readN(mask, 4, 60000)) return false; }

  size_t take = (len < max) ? len : max;
  if (take && !readN(out, take, 60000)) return false;
  // Skip any remaining bytes of an oversized frame.
  for (size_t i = take; i < len; i++) readByte(60000);

  if (masked && take) {
    for (size_t i = 0; i < take; i++) out[i] ^= mask[i & 3];
  }
  *got = take;
  return true;
}

// Reads one inbound WebSocket frame from the TLS stream and returns its
// de-framed payload (one MQTT packet per WS message, as brokers send them).
bool MqttWs::wsReadPayload(uint8_t* out, size_t max, size_t* got, uint8_t* opcode,
                           uint32_t timeoutMs) {
  *got = 0;
  const uint32_t deadline = millis() + timeoutMs;
  auto rd = [&](uint8_t* p) -> bool {
    uint32_t now = millis();
    if (now >= deadline) return false;
    uint32_t budget = deadline - now;
    if (budget > 2000) budget = 2000;
    int v = readByte(budget);
    if (v < 0) return false;
    *p = (uint8_t)v;
    return true;
  };

  uint8_t h0, h1;
  if (!rd(&h0) || !rd(&h1)) return false;
  if (opcode) *opcode = h0 & 0x0F;
  const bool masked = (h1 & 0x80) != 0;
  size_t len = h1 & 0x7F;
  if (len == 126) {
    uint8_t e[2]; if (!rd(&e[0]) || !rd(&e[1])) return false;
    len = ((size_t)e[0] << 8) | e[1];
  } else if (len == 127) {
    uint8_t e[8]; for (int k = 0; k < 8; k++) if (!rd(&e[k])) return false;
    len = 0;
    for (int k = 0; k < 8; k++) len = (len << 8) | e[k];
  }
  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked) { for (int k = 0; k < 4; k++) if (!rd(&mask[k])) return false; }

  size_t take = (len < max) ? len : max;
  for (size_t k = 0; k < take; k++) if (!rd(&out[k])) return false;
  for (size_t k = take; k < len; k++) { uint8_t d; if (!rd(&d)) return false; } // drain
  if (masked && take) for (size_t k = 0; k < take; k++) out[k] ^= mask[k & 3];
  *got = take;
  return true;
}

// ---------------------------------------------------------------------------
// MQTT packet builders (plain bytes, no WS yet)
// ---------------------------------------------------------------------------
bool MqttWs::mqttSendConnect() {
  uint8_t buf[512];
  size_t o = 0;
  buf[o++] = 0x10; // CONNECT

  // Variable header
  const char* proto = "MQTT";
  buf[o++] = 0x00; buf[o++] = 0x04;
  memcpy(buf + o, proto, 4); o += 4;
  buf[o++] = 0x04;                              // level 3.1.1
  uint8_t flags = 0x02;                          // clean session
  if (_username.length()) flags |= 0x80;
  if (_password.length()) flags |= 0x40;
  buf[o++] = flags;
  buf[o++] = 0x00; buf[o++] = 0x3C;             // keepalive 60 s

  auto putStr = [&](const String& s) {
    buf[o++] = (uint8_t)(s.length() >> 8);
    buf[o++] = (uint8_t)(s.length() & 0xFF);
    memcpy(buf + o, s.c_str(), s.length()); o += s.length();
  };
  putStr(_clientId);
  if (_username.length()) putStr(_username);
  if (_password.length()) putStr(_password);

  // Move remaining length to front.
  const size_t bodyLen = o - 1;
  uint8_t rl[4]; size_t rlN = 0;
  size_t v = bodyLen;
  do { uint8_t b = v % 128; v /= 128; if (v) b |= 0x80; rl[rlN++] = b; } while (v);
  memmove(buf + 1 + rlN, buf + 1, bodyLen);
  memcpy(buf + 1, rl, rlN);
  const size_t total = 1 + rlN + bodyLen;
  return wsSend(0x2 /*binary*/, buf, total);
}

bool MqttWs::mqttSendPublish(uint16_t packetId, const char* topic,
                             const uint8_t* payload, size_t len) {
  const size_t tl = strlen(topic);
  size_t bodyLen = 2 + tl + 2 + len;
  uint8_t rl[4]; size_t rlN = 0;
  size_t v = bodyLen;
  do { uint8_t b = v % 128; v /= 128; if (v) b |= 0x80; rl[rlN++] = b; } while (v);

  const size_t total = 1 + rlN + bodyLen;
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) return false;
  size_t o = 0;
  buf[o++] = 0x32;                              // PUBLISH QoS1
  memcpy(buf + o, rl, rlN); o += rlN;
  buf[o++] = (uint8_t)(tl >> 8); buf[o++] = (uint8_t)(tl & 0xFF);
  memcpy(buf + o, topic, tl); o += tl;
  buf[o++] = (uint8_t)(packetId >> 8); buf[o++] = (uint8_t)(packetId & 0xFF);
  memcpy(buf + o, payload, len); o += len;

  bool ok = wsSend(0x2, buf, o);
  free(buf);
  return ok;
}

bool MqttWs::mqttSendPing() {
  const uint8_t ping[2] = { 0xC0, 0x00 };
  return wsSend(0x2, ping, sizeof(ping));
}

// ---------------------------------------------------------------------------
// MQTT receive
// ---------------------------------------------------------------------------
// Reads one MQTT packet from one WS message and acts on it.
// Returns true when a packet was handled.
bool MqttWs::mqttWaitPacket(uint32_t timeoutMs, bool* handled) {
  *handled = false;
  uint8_t buf[1024]; size_t got = 0; uint8_t op = 0;
  if (!wsReadPayload(buf, sizeof(buf), &got, &op, timeoutMs)) return false;
  if (got < 2) return false;

  size_t i = 1; size_t rl = 0, mult = 1; uint8_t b;
  do {
    if (i >= got) return false;
    b = buf[i++];
    rl += (size_t)(b & 0x7F) * mult;
    mult *= 128;
  } while (b & 0x80);
  if (got < i + rl) return false;

  const uint8_t type = buf[0] >> 4;
  *handled = true;
  switch (type) {
    case 2:  // CONNACK (ignored here; mqttWaitFor handles it)
    case 13: // PINGRESP
      return true;
    case 4: { // PUBACK
      if (rl >= 2 && _ack) _ack(be16(buf + i), true);
      return true;
    }
    case 3: { // PUBLISH (inbound; we don't subscribe yet, but stay alive)
      return true;
    }
    case 12: { // PINGREQ from server -> PONG
      const uint8_t resp[2] = { 0xD0, 0x00 };
      wsSend(0x2, resp, sizeof(resp));
      return true;
    }
    case 8: // SUBACK
    case 5:
    case 6:
    case 7:
    default:
      return true; // acknowledge nothing, keep moving
  }
}

bool MqttWs::mqttWaitFor(uint8_t wantType, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    uint8_t buf[1024]; size_t got = 0; uint8_t op = 0;
    uint32_t remaining = timeoutMs - (uint32_t)(millis() - start);
    if (remaining == 0) break;
    if (!wsReadPayload(buf, sizeof(buf), &got, &op, remaining)) break;
    if (got < 2) continue;

    size_t i = 1; size_t rl = 0, mult = 1; uint8_t b;
    do {
      if (i >= got) { rl = 0; break; }
      b = buf[i++];
      rl += (size_t)(b & 0x7F) * mult;
      mult *= 128;
    } while (b & 0x80);

    const uint8_t type = buf[0] >> 4;
    if (type == wantType) {
      if (wantType == 2) {            // CONNACK: body[0]=ack, body[1]=rc
        return (rl >= 2 && i + 1 < got && buf[i + 1] == 0);
      }
      if (wantType == 4 && _ack && rl >= 2) _ack(be16(buf + i), true); // PUBACK
      return true;                    // PINGRESP
    }
    // Handle others opportunistically.
    if (type == 12) { const uint8_t resp[2] = { 0xD0, 0x00 }; wsSend(0x2, resp, 2); }
    if (type == 4 && _ack && rl >= 2) _ack(be16(buf + i), true);
  }
  return false;
}

bool MqttWs::mqttConnect() {
  if (!mqttSendConnect()) return false;
  return mqttWaitFor(2, 8000); // CONNACK
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool MqttWs::connect(uint32_t timeoutMs) {
  disconnect();
  if (_host.length() == 0) return false;

  if (_ca && _ca[0]) {
    // Explicit CA (caller supplied).
    _tls.setCACert(_ca);
  } else if (_insecure) {
    // Local testing only — never for the production broker.
    _tls.setInsecure();
  } else {
    // Production: verify against the bundled public root (GTS Root R4),
    // which is what mqtt.rabbitsayhello.me / hackathon.rabbitsayhello.me use.
    _tls.setCACert(CA_GTS_ROOT_R4);
  }
  _tls.setTimeout(5);

  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    // (Re)open TCP + TLS to wss endpoint.
    if (!_tls.connect(_host.c_str(), _port)) {
      Serial.printf("[mqtt] tcp/tls connect failed %s:%u (errno %d)\n",
                    _host.c_str(), (unsigned)_port, errno);
      _tls.stop();
      delay(500);
      continue;
    }
    if (!wsHandshake()) {
      _tls.stop();
      delay(500);
      continue;
    }
    if (!mqttConnect()) {
      Serial.println("[mqtt] mqtt CONNACK failed/timeout");
      _tls.stop();
      _wsOpen = false;
      delay(500);
      continue;
    }
    _connected = true;
    Serial.println("[mqtt] connected over WSS");
    return true;
  }
  _connected = false;
  return false;
}

bool MqttWs::publish(const char* topic, const uint8_t* payload, size_t len) {
  if (!connected() || !topic) return false;
  _packetId++;
  if (_packetId == 0) _packetId = 1;
  if (!mqttSendPublish(_packetId, topic, payload, len)) return false;
  // QoS 1 -> wait PUBACK.
  bool ok = mqttWaitFor(4, 6000);
  if (_ack) _ack(_packetId, ok);
  return ok;
}

bool MqttWs::publish(const char* topic, const String& payload) {
  return publish(topic, (const uint8_t*)payload.c_str(), payload.length());
}

void MqttWs::loop() {
  if (!connected()) return;
  bool handled = false;
  while (_tls.available() > 0) {
    mqttWaitPacket(2000, &handled);
  }
}
