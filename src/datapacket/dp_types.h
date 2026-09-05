// lib/datapacket/dp_types.h
// Packet TYPE ids exchanged between MASTER / SLAVE / relay nodes.
#ifndef DP_TYPES_H
#define DP_TYPES_H

#define DP_TYPE_HELLO          0x01   // node -> parent: announce presence after connecting
#define DP_TYPE_HEARTBEAT      0x02   // periodic keepalive
#define DP_TYPE_SENSOR_DATA    0x03   // soil-sensor telemetry, see dp_telemetry.h
#define DP_TYPE_CMD            0x04   // master -> node: control command
#define DP_TYPE_ACK            0x05   // generic acknowledgement
#define DP_TYPE_CONFIG_GET     0x06   // request current network config
#define DP_TYPE_CONFIG_SET     0x07   // apply network config, see dp_config.h
#define DP_TYPE_PROVISION      0x08   // advanced/batch provisioning, see dp_provision.h
#define DP_TYPE_PROVISION_ACK  0x09

#endif // DP_TYPES_H
