// lib/hardware/gpio_manager.h
#pragma once

#include <stdint.h>

class GpioManager {
public:
  void begin();
  void loop();

  void setLed(bool on);
  void blink(uint8_t times, uint16_t onMs = 120, uint16_t offMs = 120);
  void setSensorPower(bool on);

  // Boot-time probe: reads the setup-mode GPIO and returns true when the user
  // wants to enter SETUP (batch-config) mode instead of normal M/S operation.
  bool setupModeRequested();
};
