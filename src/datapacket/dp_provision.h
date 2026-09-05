// lib/datapacket/dp_provision.h
// Advanced-password + batch-provisioning payload layout.
// The Advanced Password hash is produced with mbedtls/sha256.h (bundled in
// ESP-IDF) — no custom hash implementation needed.
#ifndef DP_PROVISION_H
#define DP_PROVISION_H

#include "dp_common.h"

#define DP_ADV_PW_HASH_LEN     32     // SHA-256 digest length, never stored/sent in clear
#define DP_AP_SSID_PREFIX_LEN   8     // unified hotspot SSID prefix, e.g. "NODE_"
#define DP_AP_PSK_LEN          64     // hotspot password to apply

#define DP_PROV_OFF_ADV_PW_HASH   (DP_OFFSET_PAYLOAD + 0)                       // 32 bytes
#define DP_PROV_OFF_AP_SSID_PFX   (DP_PROV_OFF_ADV_PW_HASH + DP_ADV_PW_HASH_LEN)// 8 bytes
#define DP_PROV_OFF_AP_PSK        (DP_PROV_OFF_AP_SSID_PFX + DP_AP_SSID_PREFIX_LEN) // 64 bytes
#define DP_PROV_OFF_APPLY_TO_ALL  (DP_PROV_OFF_AP_PSK + DP_AP_PSK_LEN)          // uint8 bool: broadcast to children

#define DP_PROV_PAYLOAD_SIZE      (DP_PROV_OFF_APPLY_TO_ALL + 1 - DP_OFFSET_PAYLOAD)

#endif // DP_PROVISION_H
