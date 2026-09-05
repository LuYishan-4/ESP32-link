// lib/dataservice/dataservice.cpp
// Pure standard C++ (no Qt): loads a local JSON config file and parses the
// JSON bodies returned by the ESP32 HTTP endpoints (/api/config, /api/data).
#include "dataservice.hpp"

#include <fstream>
#include <sstream>
#include <utility>
#include "json.hpp"

DataService *DataService::instance() {
  static DataService inst;
  return &inst;
}

DataService::DataService() {
  schema_ = {
      {"soilMoisture", "0.0", [this](const std::string &v) { values.soilMoisture = std::stof(v); }},
      {"illumination", "0.0", [this](const std::string &v) { values.illumination = std::stof(v); }},
      {"ph", "0.0", [this](const std::string &v) { values.ph = std::stof(v); }},
      {"id", "0", [this](const std::string &v) { values.id = std::stoi(v); }},
      {"nodeId", "", [this](const std::string &v) { values.nodeId = v; }},
      {"lastUpdateTime", "0", [this](const std::string &v) { values.lastUpdateTime = std::stol(v); }}
  };
  for (const auto &item : schema_) {
    item.updater(item.defaultValue);
  }
}

void DataService::ensureInitialized(const std::string &projectPath) {
  static bool initialized = false;
  if (!initialized) {
    load(projectPath);
    initialized = true;
  }
}

namespace {

// Read a whole file into `out`; returns false when it cannot be opened.
bool readFile(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// Object member -> string; "" when absent or not a string.
std::string memberString(const js::Value &obj, const char *key) {
  const js::Value *v = obj.find(key);
  return (v && v->isString()) ? v->str : std::string();
}

} // namespace

bool DataService::load(const std::string &projectPath) {
  std::string configPath = "CursorData.json";
  if (!projectPath.empty()) configPath = projectPath + "/CursorData.json";

  std::string text;
  if (!readFile(configPath, text)) return false;

  std::unique_ptr<js::Value> root = js::parse(text);
  if (!root || !root->isObject()) return false;

  const js::Value &obj = *root;
  for (const auto &f : obj.fields) {
    if (!f.first.empty()) data_[f.first] = js::scalarString(*f.second);
  }
  for (const auto &item : schema_) {
    auto it = data_.find(item.key);
    if (it != data_.end()) {
      try {
        item.updater(it->second);
      } catch (...) {
        return false;
      }
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// ESP32 fetch helpers — parse layer only.
//
// The ESP32 web server exposes these endpoints (see lib/webservice/api_routes):
//   GET /api/config -> node settings object
//   GET /api/data   -> {"items":[telemetry,...]} (oldest -> newest)
// These functions take the raw HTTP response body and fold it into `values`.
// The actual HTTP GET is left to the caller (your own HTTP client).
// ---------------------------------------------------------------------------

bool DataService::applyConfigJson(const std::string &json) {
  std::unique_ptr<js::Value> root = js::parse(json);
  if (!root || !root->isObject()) return false;
  const js::Value &o = *root;
  if (o.fields.empty()) return false;

  // Keys match configToJson() on the ESP32 (/api/config).
  values.role           = memberString(o, "role");
  values.nodeId         = memberString(o, "node_id");
  values.targetId       = memberString(o, "target_id");
  values.upstreamSsid   = memberString(o, "upstream_ssid");
  values.upstreamPsk    = memberString(o, "upstream_psk");
  values.ipMode         = memberString(o, "ip_mode");   // "dhcp" | "static"
  values.ip             = memberString(o, "ip");
  values.gateway        = memberString(o, "gateway");
  values.subnet         = memberString(o, "subnet");
  values.apPsk          = memberString(o, "ap_psk");

  if (const js::Value *v = o.find("relay"))           if (v->isNumber()) values.relay          = static_cast<int>(v->num);
  if (const js::Value *v = o.find("relay_threshold")) if (v->isNumber()) values.relayThreshold = static_cast<int>(v->num);
  if (const js::Value *v = o.find("adv_set"))         if (v->isBool())   values.advSet = v->b;
  return true;
}

bool DataService::applyDataJson(const std::string &json) {
  std::unique_ptr<js::Value> root = js::parse(json);
  if (!root || !root->isObject()) return false;
  const js::Value *items = root->find("items");
  if (!items || !items->isArray()) return false;

  // Keep only the newest reading (largest `time`; items arrive oldest first).
  const js::Value *best = nullptr;
  double bestTime = -1.0;
  for (size_t i = 0; i < items->size(); ++i) {
    const js::Value *o = items->at(i);
    if (!o || !o->isObject()) continue;
    const js::Value *t = o->find("time");
    const double tv = (t && t->isNumber()) ? t->num : 0.0;
    if (tv >= bestTime) { bestTime = tv; best = o; }
  }
  if (!best) return false;

  auto dbl = [&](const char *key) -> double {
    const js::Value *v = best->find(key);
    return (v && v->isNumber()) ? v->num : 0.0;
  };

  // Keys match the telemetry ring items on the ESP32 (/api/data).
  values.ph             = static_cast<float>(dbl("ph"));
  values.illumination   = static_cast<float>(dbl("light"));
  values.soilMoisture   = static_cast<float>(dbl("moisture"));
  values.lastUpdateTime = static_cast<std::time_t>(dbl("time"));

  const std::string nodeId = memberString(*best, "node_id");
  if (!nodeId.empty()) values.nodeId = nodeId;

  // Relay path e.g. "M0007>R0012>N0031" -> pathList {M0007, R0012, N0031}.
  values.pathList.clear();
  const std::string path = memberString(*best, "path");
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = path.find('>', start);
    const size_t len = (end == std::string::npos) ? path.size() - start : end - start;
    if (len > 0) values.pathList.push_back(path.substr(start, len));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

