// lib/datapacket/dp_config.h
// Field layout for DP_TYPE_CONFIG_SET / DP_TYPE_CONFIG_GET.
// Mirrors the Master/Slave settings page (§7).
#ifndef DP_CONFIG_H
#define DP_CONFIG_H

#include "dp_common.h"

#define DP_CFG_IPMODE_DHCP      0x00
#define DP_CFG_IPMODE_STATIC    0x01

#define DP_CFG_SSID_LEN        32     // upstream SSID to connect to (Master node only)
#define DP_CFG_PSK_LEN         64     // upstream Wi-Fi password
#define DP_CFG_TARGETID_LEN    DP_NODEID_LEN   // target Node/Master ID (Slave node only)

#define DP_CFG_OFF_IPMODE       (DP_OFFSET_PAYLOAD + 0)   // uint8: DHCP or STATIC
#define DP_CFG_OFF_STATIC_IP    (DP_CFG_OFF_IPMODE + 1)   // 4 bytes
#define DP_CFG_OFF_GATEWAY      (DP_CFG_OFF_STATIC_IP + 4)// 4 bytes
#define DP_CFG_OFF_SUBNET       (DP_CFG_OFF_GATEWAY + 4)  // 4 bytes
#define DP_CFG_OFF_SSID         (DP_CFG_OFF_SUBNET + 4)   // DP_CFG_SSID_LEN bytes
#define DP_CFG_OFF_PSK          (DP_CFG_OFF_SSID + DP_CFG_SSID_LEN)     // DP_CFG_PSK_LEN bytes
#define DP_CFG_OFF_TARGETID     (DP_CFG_OFF_PSK + DP_CFG_PSK_LEN)       // DP_CFG_TARGETID_LEN bytes

#define DP_CFG_PAYLOAD_SIZE     (DP_CFG_OFF_TARGETID + DP_CFG_TARGETID_LEN - DP_OFFSET_PAYLOAD)

#endif // DP_CONFIG_H
