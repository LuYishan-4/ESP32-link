// lib/config/config.h
// Persistent node configuration — the single struct mirrored 1:1 by the
// settings page (§7) and stored via Preferences.h (NVS).
#pragma once

#include <stdint.h>
#include "../datapacket/dp_common.h"
#include "../datapacket/dp_config.h"
#include "../datapacket/dp_provision.h"

#define ROLE_MASTER  0
#define ROLE_SLAVE   1

// Unified hotspot SSID prefix. A relay re-broadcasts NODE_<targetId> so the
// whole group stays one logical network (§2.2 / §10).
#define AP_SSID_PREFIX     "NODE_"
#define AP_SSID_PREFIX_LEN (sizeof(AP_SSID_PREFIX) - 1)

// Host (SQL) upload — where the Master POSTs each telemetry packet as JSON.
// Configured at runtime from the Settings page (URL/IP + API token), not as a
// compile-time macro. When disabled the Master only prints the SQL INSERT to
// Serial (§9).
constexpr size_t DP_HOST_URL_LEN   = 160;   // e.g. "http://192.168.1.50:3000/api/ingest"
constexpr size_t DP_HOST_TOKEN_LEN = 128;   // sent as "Authorization: Bearer <token>"

// Fixed WiFi network the device joins when it boots into SETUP mode (GPIO held
// at boot). This lets a technician's laptop on the same network reach every
// device's web panel for batch configuration. Fill in the real values or pass
// them via build flags (-DSETUP_WIFI_SSID=...).
#ifndef SETUP_WIFI_SSID
#define SETUP_WIFI_SSID "NODE_SETUP"
#endif
#ifndef SETUP_WIFI_PWD
#define SETUP_WIFI_PWD   "NODE_SETUP_DEFAULT_PASSWORD"
#endif

struct NodeConfig {
  uint8_t  role            = ROLE_MASTER;

  char     nodeId[DP_NODEID_LEN + 1]          = "00000001";  // own ID
  char     targetId[DP_NODEID_LEN + 1]        = "00000001";  // slave: which NODE_<id> to join

  char     upstreamSsid[DP_CFG_SSID_LEN + 1]  = "";          // master: site router SSID
  char     upstreamPsk[DP_CFG_PSK_LEN + 1]    = "";          // master: site router password

  uint8_t  ipMode        = DP_CFG_IPMODE_DHCP;
  uint8_t  staticIp[4]   = {0, 0, 0, 0};
  uint8_t  gateway[4]    = {0, 0, 0, 0};
  uint8_t  subnet[4]     = {255, 255, 255, 0};

  uint8_t  advPwHash[DP_ADV_PW_HASH_LEN] = {0};              // SHA-256, never plaintext
  bool     advPwSet      = false;

  char     apPsk[DP_AP_PSK_LEN + 1] = "12345678";           // group hotspot password

  bool     relayEnabled  = false;
  bool     relayAuto     = false;
  uint16_t relayThreshold = 3;                               // auto-promote when AP nears N clients

  // Master: telemetry host upload (SQL ingest).
  bool    hostEnabled = false;
  char    hostUrl[DP_HOST_URL_LEN + 1]     = "";   // full URL incl. host IP/port/path
  char    hostToken[DP_HOST_TOKEN_LEN + 1] = "";   // API token (Authorization: Bearer)
};
