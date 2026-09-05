// lib/datapacket/dp_telemetry.h
// Field layout for a DP_TYPE_SENSOR_DATA packet.
// Mirrors the columns of the host `sensor_readings` SQL table (see §9).
#ifndef DP_TELEMETRY_H
#define DP_TELEMETRY_H

#include "dp_common.h"

#define DP_TLM_MAC_LEN          6     // raw Wi-Fi MAC of the reporting node
#define DP_TLM_PATH_LEN        32     // relay path string, e.g. "M0007>R0012>N0031"

#define DP_TLM_OFF_TIME         (DP_OFFSET_PAYLOAD + 0)   // uint32 unix timestamp
#define DP_TLM_OFF_MAC          (DP_OFFSET_PAYLOAD + 4)   // 6 bytes
#define DP_TLM_OFF_NODEID       (DP_OFFSET_PAYLOAD + 10)  // DP_NODEID_LEN bytes
#define DP_TLM_OFF_PATH         (DP_OFFSET_PAYLOAD + 10 + DP_NODEID_LEN)         // DP_TLM_PATH_LEN bytes
#define DP_TLM_OFF_PH           (DP_OFFSET_PAYLOAD + 10 + DP_NODEID_LEN + DP_TLM_PATH_LEN)          // float, soil pH
#define DP_TLM_OFF_LIGHT        (DP_TLM_OFF_PH + 4)       // float, light level (lux)
#define DP_TLM_OFF_MOISTURE     (DP_TLM_OFF_LIGHT + 4)    // float, soil moisture (%)
#define DP_TLM_OFF_SENSORTYPE   (DP_TLM_OFF_MOISTURE + 4) // uint8, sensor model/type id
#define DP_TLM_PAYLOAD_SIZE     (DP_TLM_OFF_SENSORTYPE + 1 - DP_OFFSET_PAYLOAD)

#endif // DP_TELEMETRY_H
