// lib/hardware/sensor_driver.h
#pragma once

#include <stdint.h>

struct SensorReading {
  float   ph         = 0.0f;
  float   light      = 0.0f;
  float   moisture   = 0.0f;
  uint8_t sensorType = 1;
  bool    valid      = false;
};

class SensorDriver {
public:
  bool begin();
  SensorReading read();

private:
  float _lastPh       = 7.0f;
  float _lastLight    = 0.0f;
  float _lastMoisture = 0.0f;
};
