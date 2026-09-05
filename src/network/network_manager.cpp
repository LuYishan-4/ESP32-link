// lib/network/network_manager.cpp
#include "network_manager.h"

#include <time.h>
#include <string.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>   // WIFI_POWER_8_5dBm for C3 SuperMini TX-power workaround
#include <stdlib.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include "../config/certs.h"

// ---------------------------------------------------------------------------
// Backend (MQTT over WSS) helpers
// ---------------------------------------------------------------------------

namespace {

const char kEnrollUrl[] =
    "https://hackathon.rabbitsayhello.me/v1/device/master-enrollments";
const char kSlaveEnrollUrl[] =
    "https://hackathon.rabbitsayhello.me/v1/device/slave-enrollments";

bool parseWssUrl(const char* url, String& host, uint16_t& port, String& path) {
  String u = url ? url : "";
  if (!u.startsWith("wss://")) return false;
  String rest = u.substring(6);
  int slash = rest.indexOf('/');
  host = rest;
  path = "/";
  if (slash >= 0) { host = rest.substring(0, slash); path = rest.substring(slash); }
  port = 443;
  int colon = host.indexOf(':');
  if (colon >= 0) {
    port = (uint16_t)atoi(host.substring(colon + 1).c_str());
    host = host.substring(0, colon);
    if (port == 0) port = 443;
  }
  return host.length() > 0;
}

String rfc3339(time_t t) {
  struct tm* g = gmtime(&t);
  char b[32];
  snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           g->tm_year + 1900, g->tm_mon + 1, g->tm_mday,
           g->tm_hour, g->tm_min, g->tm_sec);
  return String(b);
}

String makeMessageId(uint32_t t, uint32_t seq) {
  char b[33];
  snprintf(b, sizeof(b), "%08x%08x%08x%08x",
           t, seq, (uint32_t)esp_random(), (uint32_t)esp_random());
  return String(b);
}

} // namespace

// ---------------------------------------------------------------------------
// Verbose WiFi diagnostics (used heavily by SETUP mode)
// ---------------------------------------------------------------------------

static const char* wifiStatusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "IDLE(0)";
    case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL(1)";
    case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED(2)";
    case WL_CONNECTED:       return "CONNECTED(3)";
    case WL_CONNECT_FAILED:  return "CONNECT_FAILED(4)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST(5)";
    case WL_DISCONNECTED:    return "DISCONNECTED(6)";
    default:                 return "UNKNOWN";
  }
}

// Map wl_status_t codes and common WiFi event reason codes to readable text.
static const char* reasonName(int reason) {
  switch (reason) {
    case 1:  return "UNSPECIFIED";
    case 2:  return "AUTH_EXPIRE";
    case 3:  return "AUTH_LEAVE";
    case 4:  return "ASSOC_EXPIRE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 16: return "GROUP_KEY_UPDATE_TIMEOUT";
    case 200:return "BEACON_TIMEOUT";
    case 201:return "NO_AP_FOUND";
    case 202:return "AUTH_FAIL";
    case 203:return "ASSOC_FAIL";
    case 204:return "HANDSHAKE_TIMEOUT";
    default: return "?";
  }
}

static void logWifiEvent(WiFiEvent_t ev) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_START:          Serial.println("[wifi] event STA_START"); break;
    case ARDUINO_EVENT_WIFI_STA_STOP:           Serial.println("[wifi] event STA_STOP"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:         Serial.printf("[wifi] event STA_GOT_IP ip=%s\n", WiFi.localIP().toString().c_str()); break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:        Serial.println("[wifi] event STA_LOST_IP"); break;
    case ARDUINO_EVENT_WIFI_AP_START:           Serial.println("[wifi] event AP_START"); break;
    case ARDUINO_EVENT_WIFI_AP_STOP:            Serial.println("[wifi] event AP_STOP"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("[wifi] event AP_CLIENT_JOINED"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("[wifi] event AP_CLIENT_LEFT"); break;
    default: break;
  }
}

static void installWifiEventLog() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  WiFi.onEvent([](WiFiEvent_t ev, WiFiEventInfo_t info) {
    switch (ev) {
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.printf("[wifi] event STA_CONNECTED ch=%d auth=%d\n",
                      info.wifi_sta_connected.channel,
                      (int)info.wifi_sta_connected.authmode);
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        int r = (int)info.wifi_sta_disconnected.reason;
        Serial.printf("[wifi] event STA_DISCONNECTED reason=%d (%s) rssi=%d\n",
                      r, reasonName(r), (int)info.wifi_sta_disconnected.rssi);
        break;
      }
      default:
        logWifiEvent(ev);
    }
  });
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void NetworkHandler::begin(bool setupMode) {
  _bootMs    = millis();
  _setupMode = setupMode;
  _sensor.begin();
  loadConfig();

  if (_setupMode) { startSetupMode(); return; }

  if (_cfg.role == ROLE_MASTER) startMaster();
  else                          startSlave();
}

void NetworkHandler::loop() {
  if (_setupMode) { handleSetupLoop(); return; }
  if (_cfg.role == ROLE_MASTER) handleMasterLoop();
  else                          handleSlaveLoop();
}

void NetworkHandler::startSetupMode() {
  _state = NetState::SETUP;
  _ap.stop();
  installWifiEventLog();

  Serial.println();
  Serial.println("[setup] ================= SETUP MODE =================");
  String setupSsid = String(AP_SSID_PREFIX) + _cfg.nodeId;   // NODE_<id>
  _apSsid = setupSsid;
  Serial.printf("[setup] config AP ssid = '%s' | target STA net = '%s' (len %u)\n",
                setupSsid.c_str(), SETUP_WIFI_SSID, (unsigned)strlen(SETUP_WIFI_SSID));

  // --- 1) Standalone STA + explicit scan so we SEE whether ESP32_Host exists.
  WiFi.mode(WIFI_STA);
  // ESP32-C3 SuperMini hardware/design workaround: at default TX power the
  // radio doesn't reliably broadcast a softAP (and can fail STA auth with
  // AUTH_EXPIRE). Lowering TX power fixes both. See espressif/arduino-esp32#6551.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  Serial.printf("[setup] TX power set to %d dBm\n", (int)WiFi.getTxPower());
  delay(300);
  Serial.printf("[setup] mode after set=STA. Scanning for visible APs...\n");
  int n = WiFi.scanNetworks();
  Serial.printf("[setup] scan finished: %d AP(s) visible\n", n);
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    bool hit = (s == SETUP_WIFI_SSID);
    Serial.printf(hit ? "[setup]   >>> TARGET '%s' ch=%d rssi=%ddBm auth=%d\n"
                      : "[setup]       other '%s' ch=%d rssi=%ddBm auth=%d\n",
                  s.c_str(), WiFi.channel(i), WiFi.RSSI(i),
                  (int)WiFi.encryptionType(i));
  }
  WiFi.scanDelete();
  if (n <= 0) Serial.println("[setup]   (nothing visible - is any AP broadcasting?)");

  // --- 2) Try to join the fixed STA network (bounded, ~10 s).
  bool haveWifi = false;
  if (SETUP_WIFI_SSID[0]) {
    Serial.printf("[setup] begin() STA -> '%s' ...\n", SETUP_WIFI_SSID);
    WiFi.setAutoReconnect(false);
    WiFi.begin(SETUP_WIFI_SSID, SETUP_WIFI_PWD);
    for (int i = 0; i < 40; i++) {
      delay(250);
      if (WiFi.status() == WL_CONNECTED) break;
      if ((i % 4) == 0)
        Serial.printf("[setup]   ...waiting, status=%s\n", wifiStatusName(WiFi.status()));
    }
    haveWifi = (WiFi.status() == WL_CONNECTED);
    if (haveWifi) {
      _setupWifiAnnounced = true;
      Serial.printf("[setup] STA CONNECTED ip=%s ch=%d rssi=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
    } else {
      Serial.printf("[setup] STA NOT connected, final status=%s\n",
                    wifiStatusName(WiFi.status()));
    }
  } else {
    Serial.println("[setup] SETUP_WIFI_SSID empty -> STA step skipped");
  }

  // --- 3) Bring up the visible open config AP on the STA channel (or ch 1).
  WiFi.mode(WIFI_AP_STA);
  uint8_t ch = haveWifi ? (uint8_t)WiFi.channel() : 1;
  bool apOk = WiFi.softAP(setupSsid.c_str(), nullptr, ch, 0, 4);
  Serial.printf("[setup] softAP('%s', open, ch=%u) return=%d\n",
                setupSsid.c_str(), ch, apOk ? 1 : 0);
  delay(200);
  if (apOk) {
    Serial.printf("[setup] AP UP  ip=%s mac=%s  (getMode=0x%02x)\n",
                  WiFi.softAPIP().toString().c_str(), WiFi.softAPmacAddress().c_str(),
                  (unsigned)WiFi.getMode());
  } else {
    Serial.printf("[setup] AP FAILED to start (getMode=0x%02x)\n", (unsigned)WiFi.getMode());
  }
  Serial.println("[setup] ===============================================");
}

void NetworkHandler::handleSetupLoop() {
  // Live status every 5s so a silent failure is always visible on the monitor.
  static uint32_t lastStat = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastStat >= 5000) {
    lastStat = nowMs;
    wl_status_t st = WiFi.status();
    bool apMode = (WiFi.getMode() & WIFI_AP) != 0;
    int  clients = apMode ? WiFi.softAPgetStationNum() : -1;
    Serial.printf("[setup] LIVE  STA=%s rssi=%d | AP_on=%d clients=%d | getMode=0x%02x\n",
                  wifiStatusName(st), WiFi.RSSI(), apMode ? 1 : 0, clients,
                  (unsigned)WiFi.getMode());
    if (st == WL_CONNECTED)
      Serial.printf("[setup]   STA ip=%s | AP ip=%s ch=%d\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.softAPIP().toString().c_str(), WiFi.channel());
  }

  if (SETUP_WIFI_SSID[0] && WiFi.status() == WL_CONNECTED && !_setupWifiAnnounced) {
    _setupWifiAnnounced = true;
    Serial.printf("[setup] connected to '%s', ip=%s\n",
                  SETUP_WIFI_SSID, WiFi.localIP().toString().c_str());
  }
}

void NetworkHandler::loadConfig() {
  _state = NetState::CONFIG_LOAD;
  if (!ConfigStore::load(_cfg)) {
    Serial.println("[cfg] no stored config, using defaults");
    ConfigStore::save(_cfg);
  }
  Serial.printf("[cfg] role=%s id=%s target=%s\n", roleName(), _cfg.nodeId, _cfg.targetId);
}

bool NetworkHandler::saveConfig() {
  return ConfigStore::save(_cfg);
}

void NetworkHandler::applyConfig() {
  loadConfig();
  if (_cfg.role == ROLE_MASTER) startMaster();
  else                          startSlave();
}

void NetworkHandler::requestRoleSwitch(uint8_t role) {
  _cfg.role = role;
  ConfigStore::save(_cfg);
}

void NetworkHandler::startMaster() {
  _state = NetState::MASTER_INIT;
  WiFi.mode(WIFI_AP_STA);
  // C3 SuperMini: lower TX power so softAP is reliably broadcast (see #6551).
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  if (_cfg.ipMode == DP_CFG_IPMODE_STATIC) {
    WiFi.config(IPAddress(_cfg.staticIp), IPAddress(_cfg.gateway), IPAddress(_cfg.subnet));
  }

  // Optional upstream station for internet access (phone/laptop hotspot or a
  // router). Required to reach the remote backend (MQTT over WSS).
  if (_cfg.upstreamSsid[0]) {
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);   // keep STA alive for stable uploads
    WiFi.begin(_cfg.upstreamSsid, _cfg.upstreamPsk);
  }

  _apSsid = String(AP_SSID_PREFIX) + _cfg.nodeId;
  if (_ap.begin(_apSsid.c_str(), _cfg.apPsk)) {
    _ap.beginDataServer();
    Serial.printf("[ap] started %s\n", _apSsid.c_str());
  } else {
    Serial.println("[ap] failed to start");
  }

  _relayActive = false;
  _state = NetState::RUNNING;

  // Best-effort SNTP so telemetry unix timestamps are meaningful.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void NetworkHandler::startSlave() {
  _state = NetState::SLAVE_INIT;
  WiFi.mode(WIFI_STA);
  // C3 SuperMini: lower TX power so STA auth doesn't fail with AUTH_EXPIRE.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  _sta.begin(_cfg.targetId, _cfg.apPsk);
  _relayActive = false;
  _state = NetState::RUNNING;
  _sta.forceReconnect();
}

void NetworkHandler::updateRelay() {
  if (_cfg.role != ROLE_SLAVE) return;

  bool want = _cfg.relayEnabled || (_cfg.relayAuto && _sta.isConnected());

  if (want && !_relayActive && _sta.isConnected()) {
    _apSsid = String(AP_SSID_PREFIX) + _cfg.targetId;
    WiFi.mode(WIFI_AP_STA);
    if (_ap.begin(_apSsid.c_str(), _cfg.apPsk)) {
      _ap.beginDataServer();
      _relayActive = true;
      Serial.printf("[relay] promoted, broadcasting %s\n", _apSsid.c_str());
    }
  } else if (!want && _relayActive) {
    _ap.stop();
    _relayActive = false;
    if (_sta.isConnected()) WiFi.mode(WIFI_STA);
  }
}

// ---------------------------------------------------------------------------
// Loop handlers
// ---------------------------------------------------------------------------

void NetworkHandler::handleMasterLoop() {
  uint8_t buf[DP_MAX_PACKET_SIZE];
  int len = _ap.readFrame(buf, sizeof(buf));
  while (len > 0) {
    processPacket(buf, (size_t)len);
    len = _ap.readFrame(buf, sizeof(buf));
  }

  // Master: backend (MQTT over WSS) enrollment / connect / batch upload.
  backendLoop();
}

void NetworkHandler::handleSlaveLoop() {
  _sta.loop();
  updateRelay();

  // Relay: accept children's data frames.
  if (_relayActive) {
    uint8_t buf[DP_MAX_PACKET_SIZE];
    int len = _ap.readFrame(buf, sizeof(buf));
    while (len > 0) {
      processPacket(buf, (size_t)len);
      len = _ap.readFrame(buf, sizeof(buf));
    }
  }

  // HELLO once per (re)connection.
  bool nowConnected = _sta.isConnected();
  if (nowConnected && !_staWasConnected) {
    sendHello();
    _lastTelemetryMs = 0;   // report immediately after joining
  }
  _staWasConnected = nowConnected;

  uint32_t now = millis();

  if (now - _lastSensorMs >= SENSOR_INTERVAL_MS) {
    _lastSensorMs = now;
    _lastReading  = _sensor.read();
  }

  if (nowConnected) {
    if (now - _lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
      _lastTelemetryMs = now;
      sendOwnTelemetry();
    }
    if (now - _lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
      _lastHeartbeatMs = now;
      sendHeartbeat();
    }
  }
}

// ---------------------------------------------------------------------------
// Packet build / verify
// ---------------------------------------------------------------------------

size_t NetworkHandler::buildPacket(uint8_t type, const char* nodeId,
                                   const uint8_t* payload, size_t payloadLen,
                                   uint8_t* out, size_t outCap) {
  if (payloadLen > DP_MAX_PAYLOAD) payloadLen = DP_MAX_PAYLOAD;
  size_t total = DP_HEADER_SIZE + payloadLen + 4;
  if (!out || outCap < total) return 0;

  out[DP_OFFSET_MAGIC]   = DP_MAGIC;
  out[DP_OFFSET_VERSION] = DP_VERSION;
  out[DP_OFFSET_TYPE]    = type;
  memset(out + DP_OFFSET_NODEID, 0, DP_NODEID_LEN);
  if (nodeId) strncpy((char*)(out + DP_OFFSET_NODEID), nodeId, DP_NODEID_LEN);
  if (payload && payloadLen) memcpy(out + DP_OFFSET_PAYLOAD, payload, payloadLen);

  uint32_t crc = DP_CHECKSUM(out, total - 4);
  memcpy(out + total - 4, &crc, 4);
  return total;
}

bool NetworkHandler::verifyPacket(const uint8_t* packet, size_t len) const {
  if (!packet || len < DP_HEADER_SIZE + 4) return false;
  if (packet[DP_OFFSET_MAGIC]   != DP_MAGIC)   return false;
  if (packet[DP_OFFSET_VERSION] != DP_VERSION) return false;

  uint32_t expected = DP_CHECKSUM(packet, len - 4);
  uint32_t actual;
  memcpy(&actual, packet + len - 4, 4);
  return expected == actual;
}

void NetworkHandler::processPacket(const uint8_t* packet, size_t len) {
  if (!verifyPacket(packet, len)) return;

  uint8_t type = packet[DP_OFFSET_TYPE];
  switch (type) {
    case DP_TYPE_HELLO:        handleHello(packet, len);        break;
    case DP_TYPE_HEARTBEAT:    handleHeartbeat(packet, len);    break;
    case DP_TYPE_SENSOR_DATA:  handleSensorData(packet, len);   break;
    case DP_TYPE_PROVISION:    handleProvision(packet, len);    break;
    case DP_TYPE_PROVISION_ACK:handleProvisionAck(packet, len); break;
    case DP_TYPE_CONFIG_SET:   handleConfigSet(packet, len);    break;
    default: break; // ACK / CMD / unknown — intentionally ignored
  }
}

// ---------------------------------------------------------------------------
// Packet handlers
// ---------------------------------------------------------------------------

void NetworkHandler::handleHello(const uint8_t* p, size_t len) {
  char nodeId[DP_NODEID_LEN + 1];
  memcpy(nodeId, p + DP_OFFSET_NODEID, DP_NODEID_LEN);
  nodeId[DP_NODEID_LEN] = '\0';
  Serial.printf("[mesh] hello from %s\n", nodeId);

  // A slave may attach {label, token}; the master then enrolls it automatically.
  const size_t payloadLen = (len > DP_HEADER_SIZE + 4) ? len - DP_HEADER_SIZE - 4 : 0;
  if (payloadLen > 0) {
    DynamicJsonDocument doc(256);
    if (!deserializeJson(doc, (const char*)(p + DP_OFFSET_PAYLOAD), payloadLen)) {
      const char* label = doc["label"] | "";
      const char* token = doc["token"] | "";
      if (token && token[0]) onSlaveHello(nodeId, label, token);
    }
  }

  uint8_t ack[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_ACK, _cfg.nodeId, nullptr, 0, ack, sizeof(ack));
  _ap.broadcastFrame(ack, n);
}

void NetworkHandler::handleHeartbeat(const uint8_t* p, size_t len) {
  (void)p; (void)len;
  uint8_t ack[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_ACK, _cfg.nodeId, nullptr, 0, ack, sizeof(ack));
  _ap.broadcastFrame(ack, n);
}

void NetworkHandler::handleSensorData(const uint8_t* p, size_t len) {
  if (len < DP_OFFSET_PAYLOAD + DP_TLM_PAYLOAD_SIZE) return;

  // Prepend this node to the relay path, then forward/aggregate.
  char letter = (_cfg.role == ROLE_MASTER) ? 'M' : (_relayActive ? 'R' : 'N');
  char selfPath[DP_TLM_PATH_LEN + 1];
  const char* existing = (const char*)(p + DP_TLM_OFF_PATH);
  if (_cfg.role == ROLE_MASTER) {
    snprintf(selfPath, sizeof(selfPath), "%c%s", letter, _cfg.nodeId);
  } else {
    snprintf(selfPath, sizeof(selfPath), "%c%s>%s", letter, _cfg.nodeId, existing);
  }
  selfPath[DP_TLM_PATH_LEN] = '\0';

  uint8_t buf[DP_MAX_PACKET_SIZE];
  memcpy(buf, p, len);
  memset(buf + DP_TLM_OFF_PATH, 0, DP_TLM_PATH_LEN);
  strncpy((char*)(buf + DP_TLM_OFF_PATH), selfPath, DP_TLM_PATH_LEN);

  // Recompute CRC over header + payload (trailing 4 bytes are the old CRC).
  uint32_t crc = DP_CHECKSUM(buf, len - 4);
  memcpy(buf + len - 4, &crc, 4);

  if (_cfg.role == ROLE_MASTER) {
    pushTelemetry(buf, len, true);
    notifyDataCallback(buf, len);
  } else {
    pushTelemetry(buf, len, false);
    notifyDataCallback(buf, len);
    _sta.sendFrame(buf, len);   // forward upstream
  }
}

void NetworkHandler::handleProvision(const uint8_t* p, size_t len) {
  if (len < DP_OFFSET_PAYLOAD + DP_PROV_PAYLOAD_SIZE) return;

  // Verify the Advanced Password hash carried in the packet.
  if (_cfg.advPwSet &&
      memcmp(p + DP_PROV_OFF_ADV_PW_HASH, _cfg.advPwHash, DP_ADV_PW_HASH_LEN) != 0) {
    Serial.println("[mesh] provision rejected (bad advanced password)");
    return;
  }

  char psk[DP_AP_PSK_LEN + 1];
  memcpy(psk, p + DP_PROV_OFF_AP_PSK, DP_AP_PSK_LEN);
  psk[DP_AP_PSK_LEN] = '\0';

  strlcpy(_cfg.apPsk, psk, sizeof(_cfg.apPsk));
  ConfigStore::save(_cfg);
  Serial.printf("[mesh] provision applied (psk len %u)\n", (unsigned)strlen(_cfg.apPsk));

  uint8_t applyAll = p[DP_PROV_OFF_APPLY_TO_ALL];

  // Re-forward down the relay tree when requested.
  if (applyAll && _ap.isRunning()) {
    _ap.broadcastFrame(p, len);
  }

  // ACK upstream.
  uint8_t ack[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_PROVISION_ACK, _cfg.nodeId, nullptr, 0, ack, sizeof(ack));
  if (_cfg.role == ROLE_SLAVE) _sta.sendFrame(ack, n);
  else                         _ap.broadcastFrame(ack, n);
}

void NetworkHandler::handleProvisionAck(const uint8_t* p, size_t len) {
  char nodeId[DP_NODEID_LEN + 1];
  memcpy(nodeId, p + DP_OFFSET_NODEID, DP_NODEID_LEN);
  nodeId[DP_NODEID_LEN] = '\0';
  Serial.printf("[mesh] provision ACK from %s\n", nodeId);

  if (_cfg.role == ROLE_SLAVE) _sta.sendFrame(p, len);   // forward upstream to master
}

void NetworkHandler::handleConfigSet(const uint8_t* p, size_t len) {
  if (len < DP_OFFSET_PAYLOAD + DP_CFG_PAYLOAD_SIZE) return;

  _cfg.ipMode = p[DP_CFG_OFF_IPMODE];
  memcpy(_cfg.staticIp, p + DP_CFG_OFF_STATIC_IP, 4);
  memcpy(_cfg.gateway,  p + DP_CFG_OFF_GATEWAY, 4);
  memcpy(_cfg.subnet,   p + DP_CFG_OFF_SUBNET, 4);

  memcpy(_cfg.upstreamSsid, p + DP_CFG_OFF_SSID, DP_CFG_SSID_LEN);
  _cfg.upstreamSsid[DP_CFG_SSID_LEN] = '\0';
  memcpy(_cfg.upstreamPsk, p + DP_CFG_OFF_PSK, DP_CFG_PSK_LEN);
  _cfg.upstreamPsk[DP_CFG_PSK_LEN] = '\0';
  memcpy(_cfg.targetId, p + DP_CFG_OFF_TARGETID, DP_CFG_TARGETID_LEN);
  _cfg.targetId[DP_CFG_TARGETID_LEN] = '\0';

  ConfigStore::save(_cfg);
  Serial.println("[mesh] config applied from parent");

  uint8_t ack[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_ACK, _cfg.nodeId, nullptr, 0, ack, sizeof(ack));
  if (_cfg.role == ROLE_SLAVE) _sta.sendFrame(ack, n);
  else                         _ap.broadcastFrame(ack, n);
}

// ---------------------------------------------------------------------------
// Outbound packets
// ---------------------------------------------------------------------------

void NetworkHandler::sendHello() {
  uint8_t payload[DP_MAX_PAYLOAD];
  size_t payloadLen = 0;

  // Attach label + transfer token so the master can auto-enroll this slave.
  if (_cfg.slaveToken[0] != '\0') {
    StaticJsonDocument<256> doc;
    doc["label"] = _cfg.nodeLabel;
    doc["token"] = _cfg.slaveToken;
    payloadLen = serializeJson(doc, (char*)payload, sizeof(payload));
  }

  uint8_t packet[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_HELLO, _cfg.nodeId, payload, payloadLen,
                         packet, sizeof(packet));
  _sta.sendFrame(packet, n);
}

void NetworkHandler::sendHeartbeat() {
  uint8_t packet[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_HEARTBEAT, _cfg.nodeId, nullptr, 0, packet, sizeof(packet));
  _sta.sendFrame(packet, n);
}

void NetworkHandler::sendOwnTelemetry() {
  if (!_lastReading.valid) return;

  uint8_t payload[DP_MAX_PAYLOAD];
  memset(payload, 0, sizeof(payload));

  uint32_t t = (uint32_t)time(nullptr);
  memcpy(payload + (DP_TLM_OFF_TIME - DP_OFFSET_PAYLOAD), &t, 4);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  memcpy(payload + (DP_TLM_OFF_MAC - DP_OFFSET_PAYLOAD), mac, 6);

  memcpy(payload + (DP_TLM_OFF_NODEID - DP_OFFSET_PAYLOAD), _cfg.nodeId, DP_NODEID_LEN);

  char letter = (_cfg.role == ROLE_MASTER) ? 'M' : (_relayActive ? 'R' : 'N');
  char path[DP_TLM_PATH_LEN + 1];
  snprintf(path, sizeof(path), "%c%s", letter, _cfg.nodeId);
  memcpy(payload + (DP_TLM_OFF_PATH - DP_OFFSET_PAYLOAD), path, DP_TLM_PATH_LEN);

  float ph = _lastReading.ph;
  float light = _lastReading.light;
  float moist = _lastReading.moisture;
  memcpy(payload + (DP_TLM_OFF_PH - DP_OFFSET_PAYLOAD), &ph, 4);
  memcpy(payload + (DP_TLM_OFF_LIGHT - DP_OFFSET_PAYLOAD), &light, 4);
  memcpy(payload + (DP_TLM_OFF_MOISTURE - DP_OFFSET_PAYLOAD), &moist, 4);
  payload[DP_TLM_OFF_SENSORTYPE - DP_OFFSET_PAYLOAD] = _lastReading.sensorType;

  uint8_t packet[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_SENSOR_DATA, _cfg.nodeId, payload,
                         DP_TLM_PAYLOAD_SIZE, packet, sizeof(packet));
  _sta.sendFrame(packet, n);

  pushTelemetry(packet, n, false);
  notifyDataCallback(packet, n);
}

// ---------------------------------------------------------------------------
// Telemetry aggregation + host upload
// ---------------------------------------------------------------------------

void NetworkHandler::pushTelemetry(const uint8_t* p, size_t len, bool toHost) {
  if (!p || len < DP_OFFSET_PAYLOAD + DP_TLM_PAYLOAD_SIZE) return;

  TelemetryEntry e;
  memcpy(&e.time, p + DP_TLM_OFF_TIME, 4);
  memcpy(e.mac, p + DP_TLM_OFF_MAC, DP_TLM_MAC_LEN);

  memcpy(e.nodeId, p + DP_TLM_OFF_NODEID, DP_NODEID_LEN);
  e.nodeId[DP_NODEID_LEN] = '\0';
  memcpy(e.path, p + DP_TLM_OFF_PATH, DP_TLM_PATH_LEN);
  e.path[DP_TLM_PATH_LEN] = '\0';

  memcpy(&e.ph, p + DP_TLM_OFF_PH, 4);
  memcpy(&e.light, p + DP_TLM_OFF_LIGHT, 4);
  memcpy(&e.moisture, p + DP_TLM_OFF_MOISTURE, 4);
  e.sensorType = p[DP_TLM_OFF_SENSORTYPE];

  _ring[_ringHead] = e;
  _ringHead = (_ringHead + 1) % TELEMETRY_RING;
  if (_ringCount < TELEMETRY_RING) _ringCount++;

  if (toHost) uploadToHost(e, len);
}

void NetworkHandler::uploadToHost(const TelemetryEntry& e, size_t payloadSize) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);

  // §9 mapping — one INSERT row per packet.
  char sql[320];
  snprintf(sql, sizeof(sql),
    "INSERT INTO sensor_readings "
    "(time,node_mac,node_id,path,ph_value,light_level,soil_moisture,sensor_type,payload_size) "
    "VALUES (FROM_UNIXTIME(%lu),'%s','%s','%s',%.2f,%.2f,%.2f,%u,%u);",
    (unsigned long)e.time, macStr, e.nodeId, e.path,
    e.ph, e.light, e.moisture, (unsigned)e.sensorType, (unsigned)payloadSize);
  Serial.println(sql);

  // Legacy per-reading HTTP ingest — only used for http(s) URLs. The current
  // backend uses MQTT over WSS (see backendLoop()), so wss:// URLs are skipped.
  if (_cfg.hostEnabled && _cfg.hostUrl[0] != '\0' &&
      strncmp(_cfg.hostUrl, "wss://", 6) != 0) {
    StaticJsonDocument<512> doc;
    doc["time"]          = e.time;
    doc["node_mac"]      = macStr;
    doc["node_id"]       = e.nodeId;
    doc["path"]          = e.path;
    doc["ph_value"]      = e.ph;
    doc["light_level"]   = e.light;
    doc["soil_moisture"] = e.moisture;
    doc["sensor_type"]   = e.sensorType;
    doc["payload_size"]  = payloadSize;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    if (!http.begin(_cfg.hostUrl)) {
      Serial.println("[host] begin failed (check URL)");
      return;
    }
    http.addHeader("Content-Type", "application/json");
    if (_cfg.hostToken[0] != '\0') {
      http.addHeader("Authorization", String("Bearer ") + _cfg.hostToken);
    }
    int code = http.POST(body);
    Serial.printf("[host] ingest %s -> HTTP %d\n", e.nodeId, code);
    http.end();
  }
}

void NetworkHandler::notifyDataCallback(const uint8_t* packet, size_t len) {
  if (_dataCb) _dataCb(packet, len);
}

bool NetworkHandler::telemetryAt(size_t i, TelemetryEntry& out) const {
  if (i >= _ringCount) return false;
  size_t idx = (_ringHead + TELEMETRY_RING - _ringCount + i) % TELEMETRY_RING;
  out = _ring[idx];
  return true;
}

// ---------------------------------------------------------------------------
// Slave auto-binding: when a slave HELLOs with its token, the master calls
// POST /v1/device/slave-enrollments on its behalf (spec §7).
// ---------------------------------------------------------------------------

bool NetworkHandler::isSlaveEnrolled(const char* id) const {
  for (size_t i = 0; i < _enrolledN; i++) {
    if (strcmp(_enrolled[i], id) == 0) return true;
  }
  return false;
}

void NetworkHandler::onSlaveHello(const char* id, const char* label, const char* token) {
  if (_cfg.role != ROLE_MASTER || !_cfg.hostEnabled) return;
  if (!id || !id[0] || !token || !token[0]) return;
  if (isSlaveEnrolled(id)) return;

  // Queue the slave so we can enroll it once the master has its own MQTT
  // credentials (master enrollment must happen first).
  auto pushPending = [&]() {
    for (size_t i = 0; i < _pendN; i++) {
      if (strcmp(_pendId[i], id) == 0) return;  // already queued
    }
    if (_pendN < 4) {
      strlcpy(_pendId[_pendN], id, sizeof(_pendId[_pendN]));
      strlcpy(_pendLabel[_pendN], label ? label : "", sizeof(_pendLabel[_pendN]));
      strlcpy(_pendTok[_pendN], token, sizeof(_pendTok[_pendN]));
      _pendN++;
    }
  };

  if (_cfg.hostToken[0] == '\0') {
    pushPending();          // master not enrolled yet -> try later
  } else if (!slaveEnroll(id, label, token)) {
    pushPending();          // transient failure -> retry later
  }
}

bool NetworkHandler::slaveEnroll(const char* id, const char* label, const char* token) {
  WiFiClientSecure sec;
  sec.setCACert(CA_GTS_ROOT_R4);
  sec.setTimeout(10);

  HTTPClient http;
  if (!http.begin(sec, kSlaveEnrollUrl)) return false;
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument req(384);
  req["slave_id"] = id;
  req["master_id"] = _cfg.masterId;
  req["node_label"] = label ? label : "";
  req["transfer_token"] = token;
  String body;
  serializeJson(req, body);

  const int code = http.POST(body);
  http.end();

  if (code == 201 || code == 409) {   // 409 = already bound -> treat as done
    if (_enrolledN < 4) {
      strlcpy(_enrolled[_enrolledN], id, sizeof(_enrolled[_enrolledN]));
      _enrolledN++;
    }
    Serial.printf("[backend] slave %s auto-bound (HTTP %d)\n", id, code);
    return true;
  }
  Serial.printf("[backend] slave %s enroll HTTP %d\n", id, code);
  return false;
}

void NetworkHandler::flushPendingSlaves() {
  if (_pendN == 0 || _cfg.hostToken[0] == '\0') return;

  // Snapshot then clear; re-queue only transient failures.
  char ids[4][DP_NODEID_LEN + 1];
  char labels[4][DP_LABEL_LEN + 1];
  char toks[4][DP_ENROLL_LEN + 1];
  const size_t n = _pendN;
  for (size_t i = 0; i < n; i++) {
    strlcpy(ids[i], _pendId[i], sizeof(ids[i]));
    strlcpy(labels[i], _pendLabel[i], sizeof(labels[i]));
    strlcpy(toks[i], _pendTok[i], sizeof(toks[i]));
  }
  _pendN = 0;

  for (size_t i = 0; i < n; i++) {
    if (!isSlaveEnrolled(ids[i])) {
      if (!slaveEnroll(ids[i], labels[i], toks[i])) {
        // Keep for the next periodic retry (409/201 handling inside).
        if (_pendN < 4) {
          strlcpy(_pendId[_pendN], ids[i], sizeof(_pendId[_pendN]));
          strlcpy(_pendLabel[_pendN], labels[i], sizeof(_pendLabel[_pendN]));
          strlcpy(_pendTok[_pendN], toks[i], sizeof(_pendTok[_pendN]));
          _pendN++;
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Backend reporting (MQTT over WSS) — master only. Called from handleMasterLoop.
// ---------------------------------------------------------------------------

void NetworkHandler::backendLoop() {
  if (_cfg.role != ROLE_MASTER || !_cfg.hostEnabled) return;
  if (_cfg.hostUrl[0] == '\0') return;

  const uint32_t now = millis();
  if (now < _mNextActMs) {
    if (_mqtt.connected()) _mqtt.loop();
    return;
  }

  const bool internet = (WiFi.status() == WL_CONNECTED);
  if (!internet) {
    if (!_mNetLogged) {
      Serial.println("[backend] no internet yet - set master 'Wi-Fi \xe4\xb8\x8a\xe8\xa1\x8c' to a phone/laptop hotspot or router");
      _mNetLogged = true;
    }
    _mNextActMs = now + 10000;
    return;
  }
  if (_mNetLogged) {
    Serial.println("[backend] internet up");
    _mNetLogged = false;
  }

  // 1) Enroll once: need an enrollment token to obtain the MQTT password.
  if (_cfg.hostToken[0] == '\0') {
    if (_cfg.enrollToken[0] == '\0') {
      Serial.println("[backend] no MQTT password and no enrollment token set");
      _mNextActMs = now + 30000;
      return;
    }
    if (backendEnroll()) {
      Serial.println("[backend] enrolled: MQTT credentials saved to NVS");
      flushPendingSlaves();
    } else {
      Serial.println("[backend] enroll failed, will retry");
      _mNextActMs = now + 15000;
      return;
    }
  }

  // 2) Keep the WSS MQTT connection alive.
  if (!_mqtt.connected()) {
    if (backendEnsureConnect()) {
      if (!_mStarted) {           // first-ever connect: baseline = now
        _mStarted = true;
        _mSentUntil = (uint32_t)time(nullptr);
      }
      // On later reconnects keep _mSentUntil so unsent readings are re-sent.
      _mBatchNextMs = millis() + 5000;   // first batch shortly after connect
    } else {
      _mNextActMs = now + 15000;
      return;
    }
  }
  _mqtt.loop();

  // 3) Every 5 minutes publish a QoS1 batch of accumulated readings.
  if (now >= _mBatchNextMs) {
    _mBatchNextMs = now + 5UL * 60UL * 1000UL;
    if ((time_t)time(nullptr) > 1600000000) {
      backendPublishBatch();
    } else {
      Serial.println("[backend] clock not synced yet, batch skipped");
    }
  }
  if (_pendN > 0 && now >= _mSlaveNextMs) {
    flushPendingSlaves();
    _mSlaveNextMs = now + 60000;
  }
  _mNextActMs = millis() + 1000;
}

bool NetworkHandler::backendEnroll() {
  WiFiClientSecure sec;
  sec.setCACert(CA_GTS_ROOT_R4);
  sec.setTimeout(10);

  HTTPClient http;
  if (!http.begin(sec, kEnrollUrl)) {
    Serial.println("[backend] enroll begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument req(256);
  req["master_id"] = _cfg.masterId;
  req["enrollment_token"] = _cfg.enrollToken;
  String body;
  serializeJson(req, body);

  const int code = http.POST(body);
  if (code != 201) {
    Serial.printf("[backend] enroll HTTP %d\n", code);
    http.end();
    return false;
  }

  DynamicJsonDocument resp(512);
  if (deserializeJson(resp, http.getString())) { http.end(); return false; }
  http.end();

  const char* user = resp["mqtt_username"] | "";
  const char* pass = resp["mqtt_password"] | "";
  if (!pass || !pass[0]) return false;

  strlcpy(_cfg.mqttUser, (user && user[0]) ? user : _cfg.masterId,
          sizeof(_cfg.mqttUser));
  strlcpy(_cfg.hostToken, pass, sizeof(_cfg.hostToken));
  _cfg.enrollToken[0] = '\0';   // one-time token is consumed
  ConfigStore::save(_cfg);
  return true;
}

bool NetworkHandler::backendEnsureConnect() {
  String host, path;
  uint16_t port = 443;
  if (!parseWssUrl(_cfg.hostUrl, host, port, path)) {
    Serial.printf("[backend] invalid broker url: %s\n", _cfg.hostUrl);
    return false;
  }
  const char* user = _cfg.mqttUser[0] ? _cfg.mqttUser : _cfg.masterId;

  _mqtt.begin(host.c_str(), port, path.c_str());
  _mqtt.setAuth(_cfg.masterId, user, _cfg.hostToken); // clientId = master_id
  if (_mqtt.connect(15000)) {
    Serial.printf("[backend] MQTT connected: wss://%s:%u%s (client %s)\n",
                  host.c_str(), (unsigned)port, path.c_str(), _cfg.masterId);
    return true;
  }
  Serial.println("[backend] MQTT connect failed");
  return false;
}

void NetworkHandler::backendPublishBatch() {
  const uint32_t nowSec = (uint32_t)time(nullptr);

  DynamicJsonDocument batch(4096);
  batch["message_id"] = makeMessageId(nowSec, 0);
  batch["measured_at"] = rfc3339((time_t)nowSec);
  batch["firmware_version"] = "esp32-master-1.0.0";
  JsonArray readings = batch.createNestedArray("readings");

  size_t count = 0;
  for (size_t i = 0; i < _ringCount && count < 16; i++) {
    TelemetryEntry e;
    if (!telemetryAt(i, e)) continue;
    if (e.time <= _mSentUntil) continue;   // already published

    JsonObject r = readings.createNestedObject();
    r["slave_id"] = e.nodeId;
    r["ph"] = e.ph;
    r["ec_ms_per_cm"] = 0.0;                       // no EC sensor on board yet
    r["light_lux"] = e.light;
    r["soil_moisture_percent"] = e.moisture;
    r["calibration_version"] = "1.0";
    r["firmware_version"] = "esp32-slave-1.0.0";
    count++;
  }

  if (count == 0) {
    _mSentUntil = nowSec;
    return;
  }

  String topic = String("farm/v1/masters/") + _cfg.masterId + "/telemetry";
  String payload;
  serializeJson(batch, payload);

  const bool ok = _mqtt.publish(topic.c_str(), payload);
  Serial.printf("[backend] publish %s batch(%u) topic=%s payload=%s\n",
                ok ? "OK" : "FAIL", (unsigned)count, topic.c_str(),
                payload.length() > 120 ? payload.substring(0, 120).c_str()
                                       : payload.c_str());
  if (ok) _mSentUntil = nowSec;
}

// ---------------------------------------------------------------------------
// Advanced actions
// ---------------------------------------------------------------------------

bool NetworkHandler::setHotspotPassword(const char* psk) {
  if (!psk || strlen(psk) < 8) return false;
  strlcpy(_cfg.apPsk, psk, sizeof(_cfg.apPsk));
  ConfigStore::save(_cfg);

  // Apply live if an AP is currently running (master or relay).
  if (_ap.isRunning()) {
    _ap.stop();
    if (_ap.begin(_apSsid.c_str(), _cfg.apPsk)) {
      _ap.beginDataServer();
    }
  }
  return true;
}

bool NetworkHandler::provision(const char* apPsk, bool applyToAll) {
  if (!apPsk || !apPsk[0]) return false;
  strlcpy(_cfg.apPsk, apPsk, sizeof(_cfg.apPsk));
  ConfigStore::save(_cfg);

  uint8_t payload[DP_MAX_PAYLOAD];
  memset(payload, 0, sizeof(payload));

  memcpy(payload + (DP_PROV_OFF_ADV_PW_HASH - DP_OFFSET_PAYLOAD), _cfg.advPwHash, DP_ADV_PW_HASH_LEN);
  memcpy(payload + (DP_PROV_OFF_AP_SSID_PFX - DP_OFFSET_PAYLOAD), AP_SSID_PREFIX, AP_SSID_PREFIX_LEN);
  memcpy(payload + (DP_PROV_OFF_AP_PSK - DP_OFFSET_PAYLOAD), _cfg.apPsk, strlen(_cfg.apPsk));
  payload[DP_PROV_OFF_APPLY_TO_ALL - DP_OFFSET_PAYLOAD] = applyToAll ? 1 : 0;

  uint8_t packet[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_PROVISION, _cfg.nodeId, payload,
                         DP_PROV_PAYLOAD_SIZE, packet, sizeof(packet));
  if (n == 0) return false;

  if (_cfg.role == ROLE_MASTER) _ap.broadcastFrame(packet, n);
  else                          _sta.sendFrame(packet, n);
  return true;
}

String NetworkHandler::ipAddress() const {
  if (_cfg.role == ROLE_SLAVE) {
    IPAddress ip = _sta.ip();
    return ip.toString();
  }
  if (_ap.isRunning()) return _ap.ip().toString();
  return WiFi.localIP().toString();
}
