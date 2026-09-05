// lib/hardware/sensor_driver.cpp
#include "sensor_driver.h"
#include "board_config.h"
#include "math/calibration.hpp"
#include "math/filters.hpp"
#include <Arduino.h>

static math::MovingAverage<uint16_t, 8> s_phFilter;
static math::MovingAverage<uint16_t, 8> s_lightFilter;
static math::MovingAverage<uint16_t, 8> s_moistFilter;

bool SensorDriver::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(BOARD_PH_ADC_PIN, ADC_11db);
  analogSetPinAttenuation(BOARD_LIGHT_ADC_PIN, ADC_11db);
  analogSetPinAttenuation(BOARD_MOIST_ADC_PIN, ADC_11db);

  pinMode(BOARD_PH_ADC_PIN, INPUT);
  pinMode(BOARD_LIGHT_ADC_PIN, INPUT);
  pinMode(BOARD_MOIST_ADC_PIN, INPUT);

  s_phFilter.reset();
  s_lightFilter.reset();
  s_moistFilter.reset();

  return true;
}

SensorReading SensorDriver::read() {
  SensorReading r;

  uint16_t rawPh     = analogRead(BOARD_PH_ADC_PIN);
  uint16_t rawLight  = analogRead(BOARD_LIGHT_ADC_PIN);
  uint16_t rawMoist  = analogRead(BOARD_MOIST_ADC_PIN);

  s_phFilter.add(rawPh);
  s_lightFilter.add(rawLight);
  s_moistFilter.add(rawMoist);

  r.ph         = math::adcToPh(s_phFilter.get());
  r.light      = math::adcToLux(s_lightFilter.get());
  r.moisture   = math::clamp(math::adcToMoisturePercent(s_moistFilter.get()), 0.0f, 100.0f);
  r.sensorType = BOARD_SENSOR_TYPE_ID;
  r.valid      = true;

  _lastPh       = r.ph;
  _lastLight    = r.light;
  _lastMoisture = r.moisture;

  return r;
}
