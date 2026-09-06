// lib/webservice/api_routes.cpp
#include "api_routes.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <string.h>

#include "../network/network_manager.h"

static String ipToStr(const uint8_t ip[4]) {
  char b[16];
  snprintf(b, sizeof(b), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return String(b);
}

static void parseIp(const char* s, uint8_t out[4]) {
  if (!s || !*s) return;
  IPAddress ip;
  if (ip.fromString(s)) {
    for (int i = 0; i < 4; ++i) out[i] = ip[i];
  }
}

static bool advancedOk(NetworkHandler& net, JsonObject& o) {
  const char* pw = o["password"] | "";
  return net.verifyAdvancedPassword(pw);
}

// ---------------------------------------------------------------------------
// JSON builders
// ---------------------------------------------------------------------------

String apiStatusJson(NetworkHandler& net) {
  DynamicJsonDocument doc(512);
  doc["type"]          = "status";
  doc["role"]          = net.roleName();
  doc["state"]         = (uint8_t)net.state();
  doc["node_id"]       = net.config().nodeId;
  doc["target_id"]     = net.config().targetId;
  doc["sta_connected"] = net.staConnected();
  doc["sta_rssi"]      = net.staRssi();
  doc["ap_running"]    = net.apRunning();
  doc["children"]      = net.connectedChildren();
  doc["relay_active"]  = net.relayActive();
  doc["ip"]            = net.ipAddress();
  doc["ap_ssid"]       = net.apSsid();
  doc["free_heap"]     = ESP.getFreeHeap();
  doc["uptime"]        = net.uptimeSec();

  String out;
  serializeJson(doc, out);
  return out;
}

String telemetryToJson(const uint8_t* p, size_t len) {
  if (!p || len < DP_OFFSET_PAYLOAD + DP_TLM_PAYLOAD_SIZE) return String();

  uint32_t t;
  memcpy(&t, p + DP_TLM_OFF_TIME, 4);

  char nodeId[DP_NODEID_LEN + 1];
  memcpy(nodeId, p + DP_TLM_OFF_NODEID, DP_NODEID_LEN);
  nodeId[DP_NODEID_LEN] = '\0';

  char path[DP_TLM_PATH_LEN + 1];
  memcpy(path, p + DP_TLM_OFF_PATH, DP_TLM_PATH_LEN);
  path[DP_TLM_PATH_LEN] = '\0';

  float ph, light, moist;
  memcpy(&ph, p + DP_TLM_OFF_PH, 4);
  memcpy(&light, p + DP_TLM_OFF_LIGHT, 4);
  memcpy(&moist, p + DP_TLM_OFF_MOISTURE, 4);

  StaticJsonDocument<256> doc;
  doc["type"]        = "telemetry";
  doc["time"]        = t;
  doc["node_id"]     = nodeId;
  doc["path"]        = path;
  doc["ph"]          = ph;
  doc["light"]       = light;
  doc["moisture"]    = moist;
  doc["sensor_type"] = p[DP_TLM_OFF_SENSORTYPE];

  String out;
  serializeJson(doc, out);
  return out;
}

static String configToJson(NetworkHandler& net) {
  NodeConfig& c = net.config();
  DynamicJsonDocument doc(2048);

  doc["role"]           = (c.role == ROLE_SLAVE) ? "slave" : "master";
  doc["node_id"]        = c.nodeId;
  doc["target_id"]      = c.targetId;
  doc["upstream_ssid"]  = c.upstreamSsid;
  doc["upstream_psk"]   = c.upstreamPsk;
  doc["ip_mode"]        = (c.ipMode == DP_CFG_IPMODE_STATIC) ? "static" : "dhcp";
  doc["ip"]             = ipToStr(c.staticIp);
  doc["gateway"]        = ipToStr(c.gateway);
  doc["subnet"]         = ipToStr(c.subnet);
  doc["relay"]          = c.relayAuto ? 2 : (c.relayEnabled ? 1 : 0);
  doc["relay_threshold"]= c.relayThreshold;
  doc["ap_psk"]         = c.apPsk;
  doc["adv_set"]        = c.advPwSet;

  // Telemetry host upload (MQTT WSS backend) settings.
  doc["host_enabled"]   = c.hostEnabled;
  doc["host_url"]       = c.hostUrl;
  doc["host_token"]     = c.hostToken;
  doc["master_id"]      = c.masterId;
  doc["enroll_token"]   = c.enrollToken;
  doc["mqtt_user"]      = c.mqttUser;
  doc["node_label"]     = c.nodeLabel;
  doc["slave_token"]    = c.slaveToken;

  String out;
  serializeJson(doc, out);
  return out;
}

static String scanToJson(NetworkHandler& net) {
  ScanEntry entries[16];
  int n = net.scanTargets(entries, 16);

  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.createNestedArray("items");
  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = entries[i].ssid;
    o["rssi"] = entries[i].rssi;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             entries[i].bssid[0], entries[i].bssid[1], entries[i].bssid[2],
             entries[i].bssid[3], entries[i].bssid[4], entries[i].bssid[5]);
    o["bssid"] = mac;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void registerApiRoutes(AsyncWebServer& server, AsyncWebSocket& ws, NetworkHandler& net) {
  (void)ws;

  server.on("/api/status", HTTP_GET, [&net](AsyncWebServerRequest* req) {
    req->send(200, "application/json", apiStatusJson(net));
  });

  server.on("/api/config", HTTP_GET, [&net](AsyncWebServerRequest* req) {
    req->send(200, "application/json", configToJson(net));
  });

  server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest*) {}, nullptr,
    [&net](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"json\"}");
        return;
      }
      JsonObject o = doc.as<JsonObject>();
      NodeConfig& c = net.config();

      if (o.containsKey("role"))        c.role = (strcmp(o["role"] | "", "slave") == 0) ? ROLE_SLAVE : ROLE_MASTER;
      if (o.containsKey("node_id"))     strlcpy(c.nodeId, o["node_id"] | "", sizeof(c.nodeId));
      if (o.containsKey("target_id"))   strlcpy(c.targetId, o["target_id"] | "", sizeof(c.targetId));
      if (o.containsKey("upstream_ssid")) strlcpy(c.upstreamSsid, o["upstream_ssid"] | "", sizeof(c.upstreamSsid));
      if (o.containsKey("upstream_psk"))  strlcpy(c.upstreamPsk, o["upstream_psk"] | "", sizeof(c.upstreamPsk));
      if (o.containsKey("ip_mode"))     c.ipMode = (strcmp(o["ip_mode"] | "", "static") == 0) ? DP_CFG_IPMODE_STATIC : DP_CFG_IPMODE_DHCP;
      if (o.containsKey("ip"))          parseIp(o["ip"] | "", c.staticIp);
      if (o.containsKey("gateway"))     parseIp(o["gateway"] | "", c.gateway);
      if (o.containsKey("subnet"))      parseIp(o["subnet"] | "", c.subnet);
      if (o.containsKey("relay")) {
        int r = o["relay"] | 0;
        c.relayAuto = (r == 2);
        c.relayEnabled = (r == 1 || r == 2);
      }
      if (o.containsKey("relay_threshold")) c.relayThreshold = o["relay_threshold"] | c.relayThreshold;

      if (o.containsKey("host_enabled")) c.hostEnabled = o["host_enabled"] | false;
      if (o.containsKey("host_url"))     strlcpy(c.hostUrl, o["host_url"] | "", sizeof(c.hostUrl));
      if (o.containsKey("host_token"))   strlcpy(c.hostToken, o["host_token"] | "", sizeof(c.hostToken));
      if (o.containsKey("master_id"))    strlcpy(c.masterId, o["master_id"] | "", sizeof(c.masterId));
      if (o.containsKey("enroll_token")) strlcpy(c.enrollToken, o["enroll_token"] | "", sizeof(c.enrollToken));
      if (o.containsKey("mqtt_user"))    strlcpy(c.mqttUser, o["mqtt_user"] | "", sizeof(c.mqttUser));
      if (o.containsKey("node_label"))   strlcpy(c.nodeLabel, o["node_label"] | "", sizeof(c.nodeLabel));
      if (o.containsKey("slave_token"))  strlcpy(c.slaveToken, o["slave_token"] | "", sizeof(c.slaveToken));

      net.saveConfig();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  // Persisted config is already applied to the live NodeConfig by POST; this
  // re-initializes the role / AP / STA so Wi-Fi & role changes take effect now.
  server.on("/api/apply", HTTP_POST,
    [](AsyncWebServerRequest*) {}, nullptr,
    [&net](AsyncWebServerRequest* req, uint8_t*, size_t, size_t, size_t) {
      net.requestApply();   // applied from the main loop (safe)
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/api/scan", HTTP_GET, [&net](AsyncWebServerRequest* req) {
    req->send(200, "application/json", scanToJson(net));
  });

  server.on("/api/relay", HTTP_POST,
    [](AsyncWebServerRequest*) {}, nullptr,
    [&net](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false}");
        return;
      }
      JsonObject o = doc.as<JsonObject>();
      NodeConfig& c = net.config();
      if (o.containsKey("enabled"))   c.relayEnabled = o["enabled"] | false;
      if (o.containsKey("auto"))      c.relayAuto = o["auto"] | false;
      if (o.containsKey("threshold")) c.relayThreshold = o["threshold"] | c.relayThreshold;
      net.saveConfig();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/api/hotspot", HTTP_GET, [&net](AsyncWebServerRequest* req) {
    DynamicJsonDocument doc(256);
    doc["ap_ssid"] = net.apSsid();
    doc["prefix"]  = AP_SSID_PREFIX;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/hotspot", HTTP_POST,
    [](AsyncWebServerRequest*) {}, nullptr,
    [&net](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false}");
        return;
      }
      JsonObject o = doc.as<JsonObject>();
      if (!advancedOk(net, o)) {
        req->send(403, "application/json", "{\"ok\":false,\"err\":\"auth\"}");
        return;
      }
      bool ok = net.setHotspotPassword(o["psk"] | "");
      req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

  server.on("/api/advanced", HTTP_POST,
    [](AsyncWebServerRequest*) {}, nullptr,
    [&net](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false}");
        return;
      }
      JsonObject o = doc.as<JsonObject>();
      if (!advancedOk(net, o)) {
        req->send(403, "application/json", "{\"ok\":false,\"err\":\"auth\"}");
        return;
      }
      if (o.containsKey("new_password")) {
        bool ok = net.setAdvancedPassword(o["new_password"] | "");
        req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
      } else {
        req->send(200, "application/json", "{\"ok\":true}");
      }
    });

  server.on("/api/provision", HTTP_POST,
    [](AsyncWebServerRequest*) {}, nullptr,
    [&net](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, (const char*)data, len)) {
        req->send(400, "application/json", "{\"ok\":false}");
        return;
      }
      JsonObject o = doc.as<JsonObject>();
      if (!advancedOk(net, o)) {
        req->send(403, "application/json", "{\"ok\":false,\"err\":\"auth\"}");
        return;
      }
      bool ok = net.provision(o["psk"] | "", o["apply_to_all"] | true);
      req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

  server.on("/api/data", HTTP_GET, [&net](AsyncWebServerRequest* req) {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("items");
    TelemetryEntry e;
    for (size_t i = 0; i < net.telemetryCount() && i < 32; ++i) {
      if (!net.telemetryAt(i, e)) break;
      JsonObject o = arr.createNestedObject();
      o["time"]          = e.time;
      o["node_id"]       = e.nodeId;
      o["path"]          = e.path;
      o["ph"]            = e.ph;
      o["light"]         = e.light;
      o["moisture"]      = e.moisture;
      o["sensor_type"]   = e.sensorType;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/reboot", HTTP_POST, [&net](AsyncWebServerRequest* req) {
    net.requestReboot();   // restarted from the main loop (safe)
    req->send(200, "application/json", "{\"ok\":true}");
  });
}
