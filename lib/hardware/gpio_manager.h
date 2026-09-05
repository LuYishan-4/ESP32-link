// lib/hardware/gpio_manager.h
#pragma once

#include <stdint.h>

class GpioManager {
public:
  static void begin();
  static void loop();

  static void setLed(bool on);
  static void blink(uint8_t times, uint16_t onMs = 120, uint16_t offMs = 120);
  static void setSensorPower(bool on);
};
