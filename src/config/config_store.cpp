// lib/config/config_store.cpp
#include "config_store.h"

#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <string.h>

#define CFG_NS "mesh"

void ConfigStore::hashPassword(const char* plain, uint8_t out[DP_ADV_PW_HASH_LEN]) {
  memset(out, 0, DP_ADV_PW_HASH_LEN);
  if (!plain) return;
  (void)mbedtls_sha256((const unsigned char*)plain, strlen(plain), out, 0);
}

bool ConfigStore::checkAdvancedPassword(const char* plain, const NodeConfig& cfg) {
  if (!cfg.advPwSet) return false;
  if (!plain) return false;
  uint8_t hash[DP_ADV_PW_HASH_LEN];
  hashPassword(plain, hash);
  return memcmp(hash, cfg.advPwHash, DP_ADV_PW_HASH_LEN) == 0;
}

bool ConfigStore::setAdvancedPassword(NodeConfig& cfg, const char* plain) {
  if (!plain || !plain[0]) return false;
  hashPassword(plain, cfg.advPwHash);
  cfg.advPwSet = true;
  return save(cfg);
}

bool ConfigStore::load(NodeConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(CFG_NS, true)) return false;

  cfg.role = prefs.getUChar("role", cfg.role);

  String s = prefs.getString("nodeId", cfg.nodeId);
  strlcpy(cfg.nodeId, s.c_str(), sizeof(cfg.nodeId));

  s = prefs.getString("targetId", cfg.targetId);
  strlcpy(cfg.targetId, s.c_str(), sizeof(cfg.targetId));

  s = prefs.getString("upSsid", cfg.upstreamSsid);
  strlcpy(cfg.upstreamSsid, s.c_str(), sizeof(cfg.upstreamSsid));

  s = prefs.getString("upPsk", cfg.upstreamPsk);
  strlcpy(cfg.upstreamPsk, s.c_str(), sizeof(cfg.upstreamPsk));

  cfg.ipMode = prefs.getUChar("ipMode", cfg.ipMode);

  prefs.getBytes("staticIp", cfg.staticIp, 4);
  prefs.getBytes("gateway", cfg.gateway, 4);
  prefs.getBytes("subnet", cfg.subnet, 4);

  if (prefs.getBytes("advPwHash", cfg.advPwHash, DP_ADV_PW_HASH_LEN) == DP_ADV_PW_HASH_LEN) {
    cfg.advPwSet = prefs.getBool("advPwSet", false);
  } else {
    cfg.advPwSet = false;
    memset(cfg.advPwHash, 0, DP_ADV_PW_HASH_LEN);
  }

  s = prefs.getString("apPsk", cfg.apPsk);
  strlcpy(cfg.apPsk, s.c_str(), sizeof(cfg.apPsk));

  cfg.relayEnabled   = prefs.getBool("relay", cfg.relayEnabled);
  cfg.relayAuto      = prefs.getBool("relayAuto", cfg.relayAuto);
  cfg.relayThreshold = prefs.getUShort("relayThr", cfg.relayThreshold);

  cfg.hostEnabled = prefs.getBool("hostEnabled", cfg.hostEnabled);

  // Keys added after earlier firmware versions: guard so first boot after an
  // upgrade doesn't log nvs_get_str NOT_FOUND errors.
  if (prefs.isKey("hostUrl")) {
    s = prefs.getString("hostUrl", cfg.hostUrl);
    strlcpy(cfg.hostUrl, s.c_str(), sizeof(cfg.hostUrl));
  }
  if (prefs.isKey("hostToken")) {
    s = prefs.getString("hostToken", cfg.hostToken);
    strlcpy(cfg.hostToken, s.c_str(), sizeof(cfg.hostToken));
  }
  if (prefs.isKey("masterId")) {
    s = prefs.getString("masterId", cfg.masterId);
    strlcpy(cfg.masterId, s.c_str(), sizeof(cfg.masterId));
  }
  if (prefs.isKey("enrollTok")) {
    s = prefs.getString("enrollTok", cfg.enrollToken);
    strlcpy(cfg.enrollToken, s.c_str(), sizeof(cfg.enrollToken));
  }
  if (prefs.isKey("mqttUser")) {
    s = prefs.getString("mqttUser", cfg.mqttUser);
    strlcpy(cfg.mqttUser, s.c_str(), sizeof(cfg.mqttUser));
  }
  if (prefs.isKey("nodeLabel")) {
    s = prefs.getString("nodeLabel", cfg.nodeLabel);
    strlcpy(cfg.nodeLabel, s.c_str(), sizeof(cfg.nodeLabel));
  }
  if (prefs.isKey("slaveTok")) {
    s = prefs.getString("slaveTok", cfg.slaveToken);
    strlcpy(cfg.slaveToken, s.c_str(), sizeof(cfg.slaveToken));
  }

  prefs.end();
  return true;
}

bool ConfigStore::save(const NodeConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(CFG_NS, false)) return false;

  prefs.putUChar("role", cfg.role);
  prefs.putString("nodeId", cfg.nodeId);
  prefs.putString("targetId", cfg.targetId);
  prefs.putString("upSsid", cfg.upstreamSsid);
  prefs.putString("upPsk", cfg.upstreamPsk);
  prefs.putUChar("ipMode", cfg.ipMode);
  prefs.putBytes("staticIp", cfg.staticIp, 4);
  prefs.putBytes("gateway", cfg.gateway, 4);
  prefs.putBytes("subnet", cfg.subnet, 4);
  prefs.putBytes("advPwHash", cfg.advPwHash, DP_ADV_PW_HASH_LEN);
  prefs.putBool("advPwSet", cfg.advPwSet);
  prefs.putString("apPsk", cfg.apPsk);
  prefs.putBool("relay", cfg.relayEnabled);
  prefs.putBool("relayAuto", cfg.relayAuto);
  prefs.putUShort("relayThr", cfg.relayThreshold);

  prefs.putBool("hostEnabled", cfg.hostEnabled);
  prefs.putString("hostUrl", cfg.hostUrl);
  prefs.putString("hostToken", cfg.hostToken);
  prefs.putString("masterId", cfg.masterId);
  prefs.putString("enrollTok", cfg.enrollToken);
  prefs.putString("mqttUser", cfg.mqttUser);
  prefs.putString("nodeLabel", cfg.nodeLabel);
  prefs.putString("slaveTok", cfg.slaveToken);

  prefs.end();
  return true;
}

bool ConfigStore::erase() {
  Preferences prefs;
  if (!prefs.begin(CFG_NS, false)) return false;
  prefs.clear();
  prefs.end();
  return true;
}

bool ConfigStore::getChunkKey(char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  Preferences prefs;
  if (!prefs.begin(CFG_NS, true)) { out[0] = '\0'; return false; }
  if (!prefs.isKey("chunkKey")) { out[0] = '\0'; prefs.end(); return false; }
  String s = prefs.getString("chunkKey", "");
  prefs.end();
  strlcpy(out, s.c_str(), outLen);
  return out[0] != '\0';
}

bool ConfigStore::saveChunkKey(const char* key) {
  if (!key || !key[0]) return false;
  Preferences prefs;
  if (!prefs.begin(CFG_NS, false)) return false;
  prefs.putString("chunkKey", key);
  prefs.end();
  return true;
}
