// lib/network/network_manager.cpp
#include "network_manager.h"

#include <time.h>
#include <string.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

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

  String setupSsid = String(AP_SSID_PREFIX) + _cfg.nodeId;   // NODE_<id>, open
  _apSsid = setupSsid;

  // ESP32-C3 has a SINGLE radio shared by AP and STA. Starting the softAP and
  // then trying to associate the STA on a different channel makes the radio
  // thrash channels: the STA gets AUTH_EXPIRE and the softAP beacon ends up on
  // a channel no client is listening on (=> AP "invisible"). So join the fixed
  // STA network FIRST, then bring the config AP up on the SAME channel.
  bool haveWifi = false;
  WiFi.mode(WIFI_STA);

  if (SETUP_WIFI_SSID[0]) {
    Serial.printf("[setup] joining fixed config wifi '%s'\n", SETUP_WIFI_SSID);
    WiFi.setAutoReconnect(false);
    WiFi.begin(SETUP_WIFI_SSID, SETUP_WIFI_PWD);
    // Bounded wait so we don't block boot forever (~10 s max).
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
    haveWifi = (WiFi.status() == WL_CONNECTED);
    if (haveWifi) {
      _setupWifiAnnounced = true;
      Serial.printf("[setup] connected to '%s', ip=%s\n",
                    SETUP_WIFI_SSID, WiFi.localIP().toString().c_str());
    } else {
      Serial.printf("[setup] STA failed to join '%s' (status %d), continuing with AP only\n",
                    SETUP_WIFI_SSID, (int)WiFi.status());
    }
  } else {
    Serial.println("[setup] note: SETUP_WIFI_SSID empty, batch WiFi not joined");
  }

  // Bring up the visible, open config AP on the STA's channel (or channel 1).
  WiFi.mode(WIFI_AP_STA);
  uint8_t ch = haveWifi ? (uint8_t)WiFi.channel() : 1;
  bool apOk = WiFi.softAP(setupSsid.c_str(), nullptr, ch, 0, 4);
  Serial.printf("[setup] AP '%s' %s (ch %u, ip %s)\n",
                setupSsid.c_str(), apOk ? "UP" : "FAILED", ch,
                apOk ? WiFi.softAPIP().toString().c_str() : "-");
}

void NetworkHandler::handleSetupLoop() {
  // The ESP32 STA auto-reconnects to the fixed WiFi once credentials are set;
  // nothing extra to drive here. Web server & WS run independently in .ino.
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

  if (_cfg.ipMode == DP_CFG_IPMODE_STATIC) {
    WiFi.config(IPAddress(_cfg.staticIp), IPAddress(_cfg.gateway), IPAddress(_cfg.subnet));
  }

  // Optional upstream station (site router) for internet / SQL upload access.
  if (_cfg.upstreamSsid[0]) {
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
  (void)len;
  char nodeId[DP_NODEID_LEN + 1];
  memcpy(nodeId, p + DP_OFFSET_NODEID, DP_NODEID_LEN);
  nodeId[DP_NODEID_LEN] = '\0';
  Serial.printf("[mesh] hello from %s\n", nodeId);

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
  uint8_t packet[DP_MAX_PACKET_SIZE];
  size_t n = buildPacket(DP_TYPE_HELLO, _cfg.nodeId, nullptr, 0, packet, sizeof(packet));
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

#if defined(HOST_INGEST_URL)
  if (HOST_INGEST_URL[0] != '\0') {   // runtime check: only POST if a URL is configured
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
  http.begin(HOST_INGEST_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  Serial.printf("[host] ingest %s -> HTTP %d\n", e.nodeId, code);
  http.end();
  }
#endif
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
