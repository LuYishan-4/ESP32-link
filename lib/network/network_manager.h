// lib/network/network_manager.h
// Owns the role state machine (MASTER / SLAVE), relay promotion, packet
// build/parse, telemetry aggregation and host upload.
#pragma once

#include <Arduino.h>
#include <functional>
#include <stdint.h>
#include <stddef.h>

#include "config/config.h"
#include "config/config_store.h"
#include "ap_controller.h"
#include "sta_controller.h"
#include "hardware/sensor_driver.h"
#include "datapacket/datapacket.h"

enum class NetState : uint8_t {
  BOOT, CONFIG_LOAD, MASTER_INIT, SLAVE_INIT, RUNNING
};

// Recent telemetry snapshot (served by /api/data and forwarded over WS).
struct TelemetryEntry {
  uint32_t time       = 0;
  uint8_t  mac[6]     = {0};
  char     nodeId[DP_NODEID_LEN + 1] = {0};
  char     path[DP_TLM_PATH_LEN + 1] = {0};
  float    ph = 0.0f, light = 0.0f, moisture = 0.0f;
  uint8_t  sensorType = 0;
};

class NetworkManager {
public:
  using DataCallback = std::function<void(const uint8_t* packet, size_t len)>;

  void begin();
  void loop();

  NodeConfig& config() { return _cfg; }
  NetState    state() const { return _state; }
  const char* roleName() const { return _cfg.role == ROLE_SLAVE ? "slave" : "master"; }

  bool saveConfig();
  void applyConfig();                       // reload persisted config and (re)start role
  void requestRoleSwitch(uint8_t role);     // persist; Wi-Fi mode change needs reboot

  // Status for /api/status.
  bool     staConnected() const { return _sta.isConnected(); }
  int8_t   staRssi() const { return _sta.rssi(); }
  bool     apRunning() const { return _ap.isRunning(); }
  int      connectedChildren() const { return _ap.clientCount(); }
  bool     relayActive() const { return _relayActive; }
  String   apSsid() const { return _apSsid; }
  String   ipAddress() const;
  uint32_t uptimeSec() const { return (millis() - _bootMs) / 1000; }

  // Slave: scan for NODE_<targetId>.
  int scanTargets(ScanEntry* out, int maxEntries) { return _sta.scanMatching(out, maxEntries); }

  // Advanced-password gated actions.
  bool verifyAdvancedPassword(const char* plain) {
    if (!_cfg.advPwSet) return true;
    return ConfigStore::checkAdvancedPassword(plain, _cfg);
  }
  bool setAdvancedPassword(const char* plain) { return ConfigStore::setAdvancedPassword(_cfg, plain); }
  bool setHotspotPassword(const char* psk);
  bool provision(const char* apPsk, bool applyToAll);

  void reboot() { ESP.restart(); }

  SensorReading latestReading() const { return _lastReading; }

  // Telemetry ring accessors for /api/data.
  size_t telemetryCount() const { return _ringCount; }
  bool   telemetryAt(size_t i, TelemetryEntry& out) const;

  void setDataCallback(DataCallback cb) { _dataCb = cb; }

private:
  NodeConfig _cfg;
  NetState   _state = NetState::BOOT;
  uint32_t   _bootMs = 0;

  ApController _ap;
  StaController _sta;
  SensorDriver _sensor;

  bool   _relayActive = false;
  String _apSsid;

  SensorReading _lastReading;

  uint32_t _lastSensorMs    = 0;
  uint32_t _lastTelemetryMs = 0;
  uint32_t _lastHeartbeatMs = 0;
  bool     _staWasConnected = false;

  DataCallback _dataCb;

  static constexpr uint32_t SENSOR_INTERVAL_MS    = 5000;
  static constexpr uint32_t TELEMETRY_INTERVAL_MS = 10000;
  static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 15000;

  static constexpr size_t TELEMETRY_RING = 16;
  TelemetryEntry _ring[TELEMETRY_RING];
  size_t _ringHead  = 0;
  size_t _ringCount = 0;

  void loadConfig();
  void startMaster();
  void startSlave();
  void updateRelay();
  void handleMasterLoop();
  void handleSlaveLoop();

  // Packet handling.
  void processPacket(const uint8_t* packet, size_t len);
  bool verifyPacket(const uint8_t* packet, size_t len) const;
  size_t buildPacket(uint8_t type, const char* nodeId,
                     const uint8_t* payload, size_t payloadLen,
                     uint8_t* out, size_t outCap);

  void handleHello(const uint8_t* p, size_t len);
  void handleHeartbeat(const uint8_t* p, size_t len);
  void handleSensorData(const uint8_t* p, size_t len);
  void handleProvision(const uint8_t* p, size_t len);
  void handleProvisionAck(const uint8_t* p, size_t len);
  void handleConfigSet(const uint8_t* p, size_t len);

  void sendHello();
  void sendHeartbeat();
  void sendOwnTelemetry();

  void pushTelemetry(const uint8_t* p, size_t len, bool toHost);
  void uploadToHost(const TelemetryEntry& e, size_t payloadSize);
  void notifyDataCallback(const uint8_t* packet, size_t len);
};
