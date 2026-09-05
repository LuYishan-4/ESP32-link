// lib/datapacket/dp_common.h
// Shared packet header layout + thin wrapper around ESP32's built-in ROM CRC32.
#ifndef DP_COMMON_H
#define DP_COMMON_H

#include <stdint.h>
#include <stddef.h>

// ESP32 built-in ROM CRC32 — no custom checksum implementation needed.
// Header location differs slightly across ESP-IDF versions, so fall back gracefully.
#if __has_include("esp_rom_crc.h")
  #include "esp_rom_crc.h"
#elif __has_include("esp32/rom/crc.h")
  #include "esp32/rom/crc.h"
  #ifndef esp_rom_crc32_le
    #define esp_rom_crc32_le crc32_le
  #endif
#else
  #include "rom/crc.h"
  #ifndef esp_rom_crc32_le
    #define esp_rom_crc32_le crc32_le
  #endif
#endif

#define DP_MAGIC              0xA5
#define DP_VERSION            1

#define DP_NODEID_LEN          8     // fixed-width Node ID string (own ID or Master ID)
#define DP_HEADER_SIZE        (1+1+1+DP_NODEID_LEN)   // magic+version+type+nodeID

#define DP_OFFSET_MAGIC        0
#define DP_OFFSET_VERSION      1
#define DP_OFFSET_TYPE         2
#define DP_OFFSET_NODEID       3
#define DP_OFFSET_PAYLOAD      DP_HEADER_SIZE

// NOTE: bumped from the draft's 96 to 128 — the dp_config (117 B) and
// dp_provision (105 B) payload layouts defined below exceed 96 bytes.
#define DP_MAX_PAYLOAD        128
#define DP_MAX_PACKET_SIZE    (DP_HEADER_SIZE + DP_MAX_PAYLOAD + 4) // +4 trailing CRC32

// Node-to-node TCP transport framing. Frames are [len(2, LE)][packet].
// The length prefix is a transport concern only and is NOT part of the
// checksummed DP payload.
#define DP_DATA_PORT           5555
#define DP_FRAME_LEN_BYTES     2
#define DP_MAX_FRAME_SIZE      (DP_FRAME_LEN_BYTES + DP_MAX_PACKET_SIZE)

// Thin macro wrapper — delegates to ESP-IDF's own CRC32, nothing hand-rolled.
#define DP_CHECKSUM(buf, len)  esp_rom_crc32_le(0, (const uint8_t*)(buf), (len))

#endif // DP_COMMON_H
