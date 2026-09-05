// lib/config/config_store.h
#pragma once

#include <stdint.h>
#include "config.h"

namespace ConfigStore {

bool load(NodeConfig& cfg);
bool save(const NodeConfig& cfg);
bool erase();

// SHA-256 via mbedtls (bundled with ESP-IDF). `out` must hold DP_ADV_PW_HASH_LEN bytes.
void hashPassword(const char* plain, uint8_t out[DP_ADV_PW_HASH_LEN]);

// Compare a plaintext candidate against cfg.advPwHash.
bool checkAdvancedPassword(const char* plain, const NodeConfig& cfg);

// Hash `plain` into cfg.advPwHash, mark it set, and persist.
bool setAdvancedPassword(NodeConfig& cfg, const char* plain);

// HostLink "chunk key" (received from the Windows host via UDP set_key).
// Stored as a standalone NVS value (same namespace) — not part of NodeConfig.
bool getChunkKey(char* out, size_t outLen);
bool saveChunkKey(const char* key);

} // namespace ConfigStore
