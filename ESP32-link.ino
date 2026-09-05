// main.ino — Arduino IDE entry point (equivalent to src/main.cpp for PlatformIO).
// When using the Arduino IDE, copy the lib/ folder next to this sketch or
// prefer the PlatformIO build in this repository.
#include <Arduino.h>
#include <ArduinoOTA.h>

#include "lib/network/network_manager.h"
#include "lib/webservice/web_server.h"
#include "lib/hardware/gpio_manager.h"

NetworkHandler      g_net;
WebServerController g_web;
GpioManager        g_gpio;

void setup() {
  Serial.begin(115200);
  delay(200);
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
  delay(5);
} 
