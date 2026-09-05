# ESP32 Master/Slave Hotspot Mesh

Self-organizing, ID-grouped ESP32 mesh firmware (Arduino framework) with a web
control interface. Two roles — **MASTER** (SoftAP `NODE_<id>` + uplink) and
**SLAVE** (scans for `NODE_<targetId>`, connects as STA, optionally promotes to
a relay that re-broadcasts the same SSID).

## Layout

```
src/main.cpp        PlatformIO entry point
main.ino            Arduino IDE entry point (same content)
lib/datapacket/     wire protocol (#define macros, split by concern)
lib/network/        Wi-Fi roles, relay promotion, packet build/parse
lib/hardware/       pin map, GPIO, soil sensor driver
lib/webservice/     AsyncWebServer + ArduinoJson REST/WS API, HTML UI
lib/math/           header-only calibration / filters / conversions
lib/config/         NVS persistence (Preferences) + Advanced Password hash
```

## Build (PlatformIO)

```sh
pio run -t upload
```

The web UI is served at `http://<node-ip>/` (SoftAP default gateway
`192.168.4.1`). Key endpoints: `/api/config`, `/api/status`, `/api/scan`,
`/api/relay`, `/api/hotspot`, `/api/provision`, `/api/data`, `/api/reboot`,
plus a live WebSocket at `/ws`.

## Notes

- The Master prints one `INSERT INTO sensor_readings ...` per telemetry packet
  to Serial (SQL mapping per the architecture §9). Define `HOST_INGEST_URL` in
  `lib/config/config.h` to additionally POST JSON to a host ingest endpoint.
- Node-to-node data runs over TCP port `5555`; the Advanced Password is stored
  and forwarded only as a SHA-256 hash (`mbedtls`).

