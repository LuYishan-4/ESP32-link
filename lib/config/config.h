// lib/config/config.h
// Persistent node configuration — the single struct mirrored 1:1 by the
// settings page (§7) and stored via Preferences.h (NVS).
#pragma once

#include <stdint.h>
#include "datapacket/dp_common.h"
#include "datapacket/dp_config.h"
#include "datapacket/dp_provision.h"

#define ROLE_MASTER  0
#define ROLE_SLAVE   1

// Unified hotspot SSID prefix. A relay re-broadcasts NODE_<targetId> so the
// whole group stays one logical network (§2.2 / §10).
#define AP_SSID_PREFIX     "NODE_"
#define AP_SSID_PREFIX_LEN (sizeof(AP_SSID_PREFIX) - 1)

// Optional: when non-empty, the Master POSTs telemetry JSON here in addition
// to printing the SQL INSERT to Serial. Example: "http://192.168.1.100/api/ingest"
#ifndef HOST_INGEST_URL
#define HOST_INGEST_URL ""
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
};
