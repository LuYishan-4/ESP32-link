// src/main.cpp — PlatformIO entry point.
// setup()/loop() only; all real work lives in lib/.
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "network/network_manager.h"
#include "webservice/web_server.h"
#include "hardware/gpio_manager.h"

NetworkManager      g_net;
WebServerController g_web;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\nESP32 Master/Slave Hotspot Mesh boot");

  GpioManager::begin();
  g_net.begin();
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
  GpioManager::loop();
  ArduinoOTA.handle();
  delay(5);
}
