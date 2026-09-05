// tools/ds_verify.cpp — feed an ESP32 HTTP response body (captured to a file)
// into DataService and print the decoded values. Pure std C++, no Qt.
//
// Build:
//   g++ -std=c++17 -I lib/dataservice tools/ds_verify.cpp lib/dataservice/dataservice.cpp -o /tmp/ds_verify
//
// Usage (get bodies from the ESP32 web API, then decode with dataservice):
//   curl -s http://192.168.4.1/api/config > /tmp/config.json
//   curl -s http://192.168.4.1/api/data   > /tmp/data.json
//   /tmp/ds_verify /tmp/config.json   # prints node settings / host upload cfg
//   /tmp/ds_verify /tmp/data.json     # prints the newest telemetry item
#include "dataservice.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static std::string readAll(const char *path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return std::string();
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <config.json|data.json>\n", argv[0]);
    return 2;
  }
  const std::string text = readAll(argv[1]);
  if (text.empty()) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }

  DataService *ds = DataService::instance();
  const bool isData = text.find("\"items\"") != std::string::npos;
  const bool ok = isData ? ds->applyDataJson(text) : ds->applyConfigJson(text);
  if (!ok) {
    std::fprintf(stderr, "parse/apply failed (%s)\n",
                 isData ? "data" : "config");
    return 1;
  }

  const JSONConf &v = ds->values;
  if (isData) {
    std::printf("LATEST TELEMETRY (newest of /api/data items)\n");
    std::printf("  node_id      = %s\n", v.nodeId.c_str());
    std::printf("  ph           = %.2f\n", v.ph);
    std::printf("  illumination = %.2f\n", v.illumination);
    std::printf("  soilMoisture = %.2f\n", v.soilMoisture);
    std::printf("  lastUpdate   = %lld\n", (long long)v.lastUpdateTime);
    std::printf("  path hops    = %zu", v.pathList.size());
    for (const auto &h : v.pathList) std::printf(" [%s]", h.c_str());
    std::printf("\n");
  } else {
    std::printf("CONFIG (/api/config)\n");
    std::printf("  role=%s  node_id=%s  target_id=%s\n",
                v.role.c_str(), v.nodeId.c_str(), v.targetId.c_str());
    std::printf("  host_enabled=%d  host_url=%s  host_token=%s\n",
                v.hostEnabled ? 1 : 0, v.hostUrl.c_str(), v.hostToken.c_str());
  }
  return 0;
}
