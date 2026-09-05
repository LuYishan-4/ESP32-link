#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <functional>
#include <ctime>

struct JSONConf{
    // Latest sensor reading (parsed from GET /api/data items).
    float soilMoisture = 0.0f;
    float illumination = 0.0f;
    float ph = 0.0f;
    std::vector<std::string> pathList;   // relay path hops, e.g. {"M0007","R0012","N0031"}
    unsigned int id = 0;
    std::string nodeId;
    std::time_t lastUpdateTime = 0;

    // ESP32 node settings (parsed from GET /api/config).
    std::string role;              // "master" | "slave"
    std::string targetId;          // slave: which NODE_<id> to join
    std::string upstreamSsid;      // master: upstream site-router SSID
    std::string upstreamPsk;
    std::string ipMode;            // "dhcp" | "static"
    std::string ip;
    std::string gateway;
    std::string subnet;
    int    relay = 0;              // 0 = off, 1 = manual, 2 = auto
    int    relayThreshold = 0;
    std::string apPsk;
    bool   advSet = false;         // an advanced password is configured

    // Telemetry host (SQL) upload settings — ESP32 /api/config host_* keys.
    bool   hostEnabled = false;
    std::string hostUrl;           // full URL incl. IP/port/path
    std::string hostToken;         // API token (Authorization: Bearer)
};

class DataService {
public:
    static DataService* instance();
    JSONConf values;
   void ensureInitialized(const std::string& projectPath = "");
    bool load(const std::string& projectPath = "");

    // ---- ESP32 fetch helpers (parse layer only; callers do the HTTP GET) ----
    // Parse the body of GET /api/config and update `values` with the node
    // settings. Returns false when the body is not valid config JSON.
    bool applyConfigJson(const std::string& json);
    // Parse the body of GET /api/data and keep the newest item in `values`.
    // Returns false when the body is invalid or contains no telemetry item.
    bool applyDataJson(const std::string& json);
private:
    DataService();
    DataService(const DataService&) = delete;
    DataService& operator=(const  DataService&) = delete;
   struct BindItem {
        std::string key;
        std::string defaultValue;
        std::function<void(const std::string&)> updater;
    };
    std::vector<BindItem> schema_;
    std::unordered_map<std::string, std::string> data_;
};
#define DataServiceImp (DataService::instance()->values)
