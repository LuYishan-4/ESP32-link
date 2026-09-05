#!/usr/bin/env python3
"""
host_ingest_test.py — quick host receiver to test the ESP32 master's
telemetry host upload (the "Host upload (SQL)" settings in the web UI).

It prints every POST together with its Authorization header and, when the
token matches, inserts one row into a local SQLite DB (sensor_readings.db).
Column names match the firmware's §9 SQL schema and the JSON payload.

Usage:
    python3 host_ingest_test.py [port] [token]

    port  - listen port (default 8000)
    token - expected API token; when given, requests without a matching
            "Authorization: Bearer <token>" header get HTTP 401.
"""
import http.server
import json
import sqlite3
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
TOKEN = sys.argv[2] if len(sys.argv) > 2 else ""


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length)
        auth = self.headers.get("Authorization", "")
        print(f"\nPOST {self.path}")
        print(f"Authorization: {auth or '(none)'}")
        print(f"Body: {body.decode(errors='replace')}")

        if TOKEN and auth != f"Bearer {TOKEN}":
            print("  -> token mismatch, returning 401")
            self.send_response(401)
            self.end_headers()
            return

        try:
            d = json.loads(body)
            con = sqlite3.connect("sensor_readings.db")
            con.execute(
                "CREATE TABLE IF NOT EXISTS sensor_readings("
                "time INTEGER, node_mac TEXT, node_id TEXT, path TEXT, "
                "ph_value REAL, light_level REAL, soil_moisture REAL, "
                "sensor_type INT, payload_size INT)")
            con.execute(
                "INSERT INTO sensor_readings VALUES(?,?,?,?,?,?,?,?,?)",
                (d.get("time"), d.get("node_mac"), d.get("node_id"),
                 d.get("path", ""), d.get("ph_value"), d.get("light_level"),
                 d.get("soil_moisture"), d.get("sensor_type"),
                 d.get("payload_size")))
            con.commit()
            con.close()
            print("  -> row written to sensor_readings.db")
        except Exception as exc:  # noqa: BLE001 - report and keep serving
            print("  -> storage skipped:", exc)

        self.send_response(200)
        self.end_headers()

    def log_message(self, *args):  # silence default logging
        pass


if __name__ == "__main__":
    print(f"[host] listening on 0.0.0.0:{PORT}, token={TOKEN!r}")
    http.server.HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
