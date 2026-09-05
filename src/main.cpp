// main.cpp — PlatformIO entry point.
// Modules live under src/ (config, datapacket, hardware, math, network, webservice).
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "network/network_manager.h"
#include "webservice/web_server.h"
#include "hardware/gpio_manager.h"

NetworkHandler      g_net;
WebServerController g_web;
GpioManager        g_gpio;

void setup() {
  Serial.begin(115200);
  // ESP32-C3 uses the on-chip USB-Serial/JTAG for `Serial`. On a hardware reset
  // (RST/EN) the USB re-enumerates, so early boot prints get dropped unless we
  // give the host time to re-attach first.
  delay(1000);
  Serial.println("\n\nESP32 Master/Slave Hotspot Mesh boot");

  g_gpio.begin();
  // Boot-mode selection: if the setup GPIO is held at boot we enter SETUP
  // (batch-config) mode, otherwise normal Master/Slave operation.
  bool setupMode = g_gpio.setupModeRequested();
  g_net.begin(setupMode);
  g_web.begin(g_net);

  // OTA firmware update (gated behind the Advanced Password in the UI flow).
  ArduinoOTA.setHostname("esp32-mesh");
  ArduinoOTA
    .onStart([]() { Serial.println("[ota] start"); })
    .onEnd([]() { Serial.println("[ota] done"); })
    .onError([](ota_error_t e) { Serial.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();
}

void loop() {
  g_net.loop();
  g_gpio.loop();
  ArduinoOTA.handle();

  // Periodic heartbeat so the serial monitor always shows output regardless of
  // the reset timing (ESP32-C3 native USB drops the very first boot lines when
  // pressing RST). Confirms Serial works and shows the active mode.
  static uint32_t lastLog = 0;
  if (millis() - lastLog >= 5000) {
    lastLog = millis();
    Serial.printf("[main] uptime=%lus mode=%s\n", millis() / 1000,
                  g_net.setupMode() ? "SETUP" : "NORMAL");
  }

  delay(5);
} 
